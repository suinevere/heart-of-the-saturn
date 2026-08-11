/*----------------------
 | extract_disc.c
 | Description: Standalone host tool that pulls the 19 game-data blobs and
 |   the 41 CD-DA audio tracks out of the original bin/cue rip and writes
 |   them into the Saturn disc layout (saturn/cd/data/ BIN files,
 |   saturn/cd/music/trackNN.wav, saturn/cd/music/tracklist).
 |
 |   Links src/discfmt.c directly instead of reimplementing the cue-parsing,
 |   ISO9660-lookup and MODE1 sector arithmetic: this tool and the runtime
 |   (src/host/disc_cue.c) must never be able to disagree about what the
 |   disc contains, or a passing extraction could still hand the runtime a
 |   file that looks right at the wrong offset. The per-sector data-track
 |   read loop and the cue-relative path resolution below intentionally
 |   mirror disc_cue.c's disc_read_file/disc_open rather than inventing a
 |   second way to do the same thing.
 |
 |   Deliberately no SDL and no #include of disc_cue.c: this is a build-time
 |   host tool, not part of the engine, and disc_cue.c is SDL-linked.
 |
 |   Usage: extract_disc <path-to.cue> <saturn-cd-dir>
 |
 |   Design: docs/superpowers/specs/2026-07-31-hota-bincue-disc-backend-design.md
 | Author: suinevere
 | Dependencies: stdio.h, stdlib.h, string.h, stdint.h, errno.h; direct.h
 |   (Windows) / sys/stat.h (POSIX) for directory creation; discfmt.h/
 |   discfmt.c for every disc-format fact.
 ----------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "discfmt.h"

#define SECTOR_USER_BYTES 2048
#define WAV_HEADER_BYTES  44
#define COPY_BUF_BYTES    (1 << 20) /* 1 MiB streaming buffer for audio tracks */

/*----------------------
 | kDataManifest
 | Description: The 19 blob names the engine reads, names only -- unlike
 |   disc_cue.c's disc_manifest, this carries no hardcoded LBA/size. Every
 |   size and offset here comes fresh out of discfmt_iso_find against the
 |   real disc being extracted, which is the whole point of linking
 |   discfmt.c instead of copying disc_cue.c's table: a differently-mastered
 |   dump would still extract correctly.
 | Author: suinevere
 ----------------------*/
static const char *const kDataManifest[] = {
    "END1.BIN", "END2.BIN", "END3.BIN", "END4.BIN",
    "GAME2.BIN",
    "INTRO1.BIN", "INTRO2.BIN", "INTRO3.BIN", "INTRO4.BIN",
    "MAKE2MB.BIN",
    "MID2.BIN",
    "ROOMS1.BIN", "ROOMS2.BIN", "ROOMS3.BIN", "ROOMS4.BIN",
    "ROOMS5.BIN", "ROOMS6.BIN", "ROOMS7.BIN", "ROOMS8.BIN"
};
#define DATA_MANIFEST_COUNT (sizeof(kDataManifest) / sizeof(kDataManifest[0]))

/*----------------------
 | dirname_of
 | Description: Splits the directory off a cue path, accepting both '/' and
 |   '\\' -- mirrors disc_cue.c's disc_dirname, since a cue's FILE lines are
 |   relative to the cue's own directory, not this process's CWD.
 | Author: suinevere
 ----------------------*/
static void dirname_of(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *last = slash;
    size_t len;

    if (bslash != NULL && (last == NULL || bslash > last))
    {
        last = bslash;
    }

    if (last == NULL)
    {
        out[0] = '\0';
        return;
    }

    len = (size_t)(last - path);
    if (len >= out_size)
    {
        len = out_size - 1;
    }

    memcpy(out, path, len);
    out[len] = '\0';
}

/*----------------------
 | read_whole_file
 | Description: Slurps a small text file (the cue sheet) into a malloc'd
 |   buffer for discfmt_cue_parse, which takes a buffer and a length rather
 |   than a FILE * by design (discfmt.c stays stdio-free).
 | Author: suinevere
 ----------------------*/
static int read_whole_file(const char *path, char **out_text, size_t *out_len)
{
    FILE *fp;
    long size;
    char *buf;

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return 0;
    }

    size = ftell(fp);
    if (size < 0)
    {
        fclose(fp);
        return 0;
    }

    rewind(fp);

    buf = (char *)malloc((size_t)size > 0 ? (size_t)size : 1);
    if (buf == NULL)
    {
        fclose(fp);
        return 0;
    }

    if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size)
    {
        fclose(fp);
        free(buf);
        return 0;
    }

    fclose(fp);
    *out_text = buf;
    *out_len = (size_t)size;
    return 1;
}

/*----------------------
 | join_path
 | Description: Joins dir and name with '/', or just copies name when dir is
 |   empty (a bare cue filename with no directory component). Returns 0 if
 |   the result would not fit out_size, so every caller fails loudly instead
 |   of silently truncating a path.
 | Author: suinevere
 ----------------------*/
static int join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    int n;

    if (dir[0] == '\0')
    {
        n = snprintf(out, out_size, "%s", name);
    }
    else
    {
        n = snprintf(out, out_size, "%s/%s", dir, name);
    }

    if (n < 0 || (size_t)n >= out_size)
    {
        return 0;
    }

    return 1;
}

/*----------------------
 | ensure_dir
 | Description: Creates path if absent; treats "already exists" as success
 |   so a re-run of the extractor over an already-populated saturn/cd is
 |   idempotent rather than a hard failure.
 | Author: suinevere
 ----------------------*/
static int ensure_dir(const char *path)
{
#ifdef _WIN32
    if (_mkdir(path) == 0)
    {
        return 1;
    }
#else
    if (mkdir(path, 0777) == 0)
    {
        return 1;
    }
#endif

    return errno == EEXIST;
}

/*----------------------
 | put_le32 / put_le16
 | Description: Little-endian field writers for the RIFF/WAVE header --
 |   every multi-byte field in it is LE regardless of host byte order.
 | Author: suinevere
 ----------------------*/
static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

/*----------------------
 | write_wav_header
 | Description: Writes the canonical 44-byte RIFF/WAVE header for a
 |   44100 Hz/16-bit/stereo PCM stream of data_size bytes, per the task
 |   brief's field table verbatim. The source CD-DA track file is already
 |   in that exact format with no header of its own (see disc_cue.c's
 |   disc_music_callback, which freads it straight into the mixer), so this
 |   header is the only thing this tool adds -- the payload that follows is
 |   copied unmodified.
 | Author: suinevere
 ----------------------*/
static int write_wav_header(FILE *fp, uint32_t data_size)
{
    uint8_t hdr[WAV_HEADER_BYTES];

    memcpy(hdr + 0, "RIFF", 4);
    put_le32(hdr + 4, 36u + data_size);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    put_le32(hdr + 16, 16u);          /* PCM fmt chunk size */
    put_le16(hdr + 20, 1u);           /* PCM */
    put_le16(hdr + 22, 2u);           /* stereo */
    put_le32(hdr + 24, 44100u);       /* sample rate */
    put_le32(hdr + 28, 176400u);      /* byte rate: 44100 * 2 * 2 */
    put_le16(hdr + 32, 4u);           /* block align: 2 channels * 16 bits */
    put_le16(hdr + 34, 16u);          /* bits per sample */
    memcpy(hdr + 36, "data", 4);
    put_le32(hdr + 40, data_size);

    return fwrite(hdr, 1, sizeof(hdr), fp) == sizeof(hdr);
}

/*----------------------
 | extract_blob
 | Description: Looks name up via discfmt_iso_find against the already-
 |   loaded root directory, then reads it off the data track and writes it
 |   to data_out_dir/name. The read loop is the same per-sector shape as
 |   disc_cue.c's disc_read_file: a straight fread across the span would
 |   pull in the 304 bytes of MODE1 sync/header/ECC between every 2048
 |   bytes of payload, corrupting everything past the first sector.
 | Author: suinevere
 ----------------------*/
static int extract_blob(FILE *data_fp, const uint8_t *root_dir, uint32_t root_len,
                         const char *name, const char *data_out_dir)
{
    uint32_t lba = 0, size = 0, sectors, i;
    char out_path[1024];
    FILE *out_fp;
    uint8_t buf[SECTOR_USER_BYTES];

    if (!discfmt_iso_find(root_dir, root_len, name, &lba, &size))
    {
        fprintf(stderr, "extract_disc: '%s' not found on disc\n", name);
        return 0;
    }

    if (!join_path(out_path, sizeof(out_path), data_out_dir, name))
    {
        fprintf(stderr, "extract_disc: output path too long for '%s'\n", name);
        return 0;
    }

    out_fp = fopen(out_path, "wb");
    if (out_fp == NULL)
    {
        fprintf(stderr, "extract_disc: can't create '%s'\n", out_path);
        return 0;
    }

    sectors = discfmt_sector_span(size);
    for (i = 0; i < sectors; i++)
    {
        long off = (long)discfmt_mode1_user_offset(lba + i);
        uint32_t remaining = size - i * SECTOR_USER_BYTES;
        uint32_t chunk = (remaining < SECTOR_USER_BYTES) ? remaining : SECTOR_USER_BYTES;

        if (fseek(data_fp, off, SEEK_SET) != 0 ||
            fread(buf, 1, chunk, data_fp) != chunk)
        {
            fprintf(stderr, "extract_disc: error reading '%s' at sector LBA %u\n", name, (unsigned)(lba + i));
            fclose(out_fp);
            return 0;
        }

        if (fwrite(buf, 1, chunk, out_fp) != chunk)
        {
            fprintf(stderr, "extract_disc: short write to '%s'\n", out_path);
            fclose(out_fp);
            return 0;
        }
    }

    if (fclose(out_fp) != 0)
    {
        fprintf(stderr, "extract_disc: error closing '%s'\n", out_path);
        return 0;
    }

    printf("data:  %-12s %10u bytes (LBA %u)\n", name, (unsigned)size, (unsigned)lba);
    return 1;
}

/*----------------------
 | extract_audio_track
 | Description: Copies one CD-DA track's raw PCM bytes, past its pregap and
 |   behind a 44-byte WAV header, into music_out_dir/track<NN>.wav where NN is
 |   the cue TRACK number (02..42) -- not the engine's music index -- then
 |   appends that filename to the tracklist. The source track file carries no
 |   header of its own (audio tracks in a multi-file bin/cue are raw
 |   interleaved PCM samples starting at byte 0), so this is a streamed copy,
 |   not a per-sector MODE1 read: only the data track needs that.
 |
 |   The pregap skip is the whole reason this is not a plain copy. This rip
 |   declares INDEX 00 on every audio track, meaning the first 150 sectors of
 |   each track file -- two seconds, verified digital silence, peak sample 102
 |   of 32767 at worst -- sit before INDEX 01. A drive told to play track N
 |   starts at INDEX 01 and never reaches them, because that is what a TOC's
 |   track start address means. Copying them into the WAV moved them past both
 |   cue generators downstream, neither of which emits an INDEX 00 of its own,
 |   so they became the head of the playable track and every track came in two
 |   seconds late. Dropping them here is not an edit to the audio: it
 |   reproduces what the console does.
 | Author: suinevere
 ----------------------*/
static int extract_audio_track(const char *cue_dir, const DiscCueTrack *track,
                                const char *music_out_dir, FILE *tracklist_fp)
{
    char src_path[1024];
    char out_name[64];
    char out_path[1024];
    FILE *src_fp;
    FILE *out_fp;
    long size_l;
    uint32_t size;
    uint32_t pregap;
    uint32_t remaining;
    static uint8_t buf[COPY_BUF_BYTES];

    if (track->number < 0 || track->number > 99)
    {
        fprintf(stderr, "extract_disc: track number %d out of range\n", track->number);
        return 0;
    }

    if (!join_path(src_path, sizeof(src_path), cue_dir, track->filename))
    {
        fprintf(stderr, "extract_disc: source track path too long for track %d\n", track->number);
        return 0;
    }

    src_fp = fopen(src_path, "rb");
    if (src_fp == NULL)
    {
        fprintf(stderr, "extract_disc: can't open track file '%s'\n", src_path);
        return 0;
    }

    if (fseek(src_fp, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "extract_disc: can't seek '%s'\n", src_path);
        fclose(src_fp);
        return 0;
    }

    size_l = ftell(src_fp);
    if (size_l < 0)
    {
        fprintf(stderr, "extract_disc: can't tell size of '%s'\n", src_path);
        fclose(src_fp);
        return 0;
    }

    size = (uint32_t)size_l;

    if (track->pregap_sectors < 0)
    {
        fprintf(stderr, "extract_disc: negative pregap on track %d\n", track->number);
        fclose(src_fp);
        return 0;
    }

    pregap = (uint32_t)track->pregap_sectors * DISCFMT_RAW_SECTOR;

    if (pregap >= size)
    {
        fprintf(stderr, "extract_disc: track %d is %u bytes but its cue claims a "
                        "%u-byte pregap\n",
                track->number, (unsigned)size, (unsigned)pregap);
        fclose(src_fp);
        return 0;
    }

    if (fseek(src_fp, (long)pregap, SEEK_SET) != 0)
    {
        fprintf(stderr, "extract_disc: can't seek past pregap of '%s'\n", src_path);
        fclose(src_fp);
        return 0;
    }

    size -= pregap;

    snprintf(out_name, sizeof(out_name), "track%02d.wav", track->number);

    if (!join_path(out_path, sizeof(out_path), music_out_dir, out_name))
    {
        fprintf(stderr, "extract_disc: output track path too long for '%s'\n", out_name);
        fclose(src_fp);
        return 0;
    }

    out_fp = fopen(out_path, "wb");
    if (out_fp == NULL)
    {
        fprintf(stderr, "extract_disc: can't create '%s'\n", out_path);
        fclose(src_fp);
        return 0;
    }

    if (!write_wav_header(out_fp, size))
    {
        fprintf(stderr, "extract_disc: error writing wav header for '%s'\n", out_path);
        fclose(out_fp);
        fclose(src_fp);
        return 0;
    }

    remaining = size;
    while (remaining > 0)
    {
        size_t chunk = (remaining < COPY_BUF_BYTES) ? (size_t)remaining : (size_t)COPY_BUF_BYTES;
        size_t got = fread(buf, 1, chunk, src_fp);

        if (got != chunk)
        {
            fprintf(stderr, "extract_disc: short read from '%s'\n", src_path);
            fclose(out_fp);
            fclose(src_fp);
            return 0;
        }

        if (fwrite(buf, 1, chunk, out_fp) != chunk)
        {
            fprintf(stderr, "extract_disc: short write to '%s'\n", out_path);
            fclose(out_fp);
            fclose(src_fp);
            return 0;
        }

        remaining -= (uint32_t)chunk;
    }

    fclose(src_fp);

    if (fclose(out_fp) != 0)
    {
        fprintf(stderr, "extract_disc: error closing '%s'\n", out_path);
        return 0;
    }

    if (fprintf(tracklist_fp, "%s\n", out_name) < 0)
    {
        fprintf(stderr, "extract_disc: error writing tracklist entry for '%s'\n", out_name);
        return 0;
    }

    printf("music: %-14s %10u bytes (cue track %02d, %d pregap sectors dropped)\n",
           out_name, (unsigned)size, track->number, track->pregap_sectors);
    return 1;
}

int main(int argc, char **argv)
{
    const char *cue_path;
    const char *out_dir;
    char cue_dir[768];
    char *cue_text = NULL;
    size_t cue_len = 0;
    DiscCue cue;
    int single_file = 0;
    int data_track = -1;
    int i;
    char data_path[1024];
    FILE *data_fp = NULL;
    uint8_t pvd_user[SECTOR_USER_BYTES];
    uint32_t root_lba = 0, root_len = 0;
    uint32_t sectors;
    uint8_t *root_dir = NULL;
    char data_out_dir[1024];
    char music_out_dir[1024];
    char tracklist_path[1024];
    FILE *tracklist_fp = NULL;
    int rc = 0;

    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <path-to.cue> <saturn-cd-dir>\n", argv[0]);
        return 1;
    }

    cue_path = argv[1];
    out_dir = argv[2];

    dirname_of(cue_path, cue_dir, sizeof(cue_dir));

    if (!read_whole_file(cue_path, &cue_text, &cue_len))
    {
        fprintf(stderr, "extract_disc: can't read cue file '%s'\n", cue_path);
        return 1;
    }

    if (!discfmt_cue_parse(cue_text, cue_len, &cue, &single_file))
    {
        free(cue_text);

        if (single_file)
        {
            fprintf(stderr, "extract_disc: '%s' is a single-file cue image, which is not supported\n", cue_path);
        }
        else
        {
            fprintf(stderr, "extract_disc: '%s' is not a valid cue sheet\n", cue_path);
        }

        return 1;
    }

    free(cue_text);

    for (i = 0; i < cue.count; i++)
    {
        if (!cue.tracks[i].is_audio)
        {
            data_track = i;
            break;
        }
    }

    if (data_track < 0)
    {
        fprintf(stderr, "extract_disc: '%s' has no data track\n", cue_path);
        return 1;
    }

    if (!join_path(data_path, sizeof(data_path), cue_dir, cue.tracks[data_track].filename))
    {
        fprintf(stderr, "extract_disc: data track path too long\n");
        return 1;
    }

    data_fp = fopen(data_path, "rb");
    if (data_fp == NULL)
    {
        fprintf(stderr, "extract_disc: can't open data track '%s'\n", data_path);
        return 1;
    }

    if (fseek(data_fp, (long)discfmt_mode1_user_offset(16), SEEK_SET) != 0 ||
        fread(pvd_user, 1, sizeof(pvd_user), data_fp) != sizeof(pvd_user))
    {
        fprintf(stderr, "extract_disc: can't read PVD sector (LBA 16) from '%s'\n", data_path);
        fclose(data_fp);
        return 1;
    }

    if (!discfmt_iso_root(pvd_user, &root_lba, &root_len))
    {
        fprintf(stderr, "extract_disc: '%s' does not carry a valid ISO9660 PVD at LBA 16\n", data_path);
        fclose(data_fp);
        return 1;
    }

    sectors = discfmt_sector_span(root_len);
    root_dir = (uint8_t *)malloc((size_t)sectors * SECTOR_USER_BYTES);
    if (root_dir == NULL)
    {
        fprintf(stderr, "extract_disc: out of memory reading %u-byte root directory\n", (unsigned)root_len);
        fclose(data_fp);
        return 1;
    }

    /* Root directory read: one fseek/fread pair per sector -- see
       discfmt_mode1_user_offset for why a single contiguous read is wrong
       for MODE1/2352. */
    for (i = 0; i < (int)sectors; i++)
    {
        long off = (long)discfmt_mode1_user_offset(root_lba + (uint32_t)i);

        if (fseek(data_fp, off, SEEK_SET) != 0 ||
            fread(root_dir + (size_t)i * SECTOR_USER_BYTES, 1, SECTOR_USER_BYTES, data_fp) != SECTOR_USER_BYTES)
        {
            fprintf(stderr, "extract_disc: error reading root directory sector %d\n", i);
            free(root_dir);
            fclose(data_fp);
            return 1;
        }
    }

    if (!join_path(data_out_dir, sizeof(data_out_dir), out_dir, "data") ||
        !join_path(music_out_dir, sizeof(music_out_dir), out_dir, "music") ||
        !join_path(tracklist_path, sizeof(tracklist_path), music_out_dir, "tracklist"))
    {
        fprintf(stderr, "extract_disc: output directory path too long for '%s'\n", out_dir);
        free(root_dir);
        fclose(data_fp);
        return 1;
    }

    if (!ensure_dir(out_dir) || !ensure_dir(data_out_dir) || !ensure_dir(music_out_dir))
    {
        fprintf(stderr, "extract_disc: can't create output directories under '%s'\n", out_dir);
        free(root_dir);
        fclose(data_fp);
        return 1;
    }

    /* ---- The 19 game-data blobs. ---- */
    for (i = 0; i < (int)DATA_MANIFEST_COUNT; i++)
    {
        if (!extract_blob(data_fp, root_dir, root_len, kDataManifest[i], data_out_dir))
        {
            rc = 1;
            goto cleanup;
        }
    }

    /* ---- The 41 CD-DA tracks, plus the tracklist that pins their order.
       shared.mk numbers disc tracks sequentially starting at 2, in this
       file's order -- see discfmt_cue_track_for_music's header comment for
       why that mapping (engine_index + 2) is the one fact that must never
       drift, and the task brief for why silently falling back to `find`
       order would be audible but invisible in a build log. */
    tracklist_fp = fopen(tracklist_path, "wb");
    if (tracklist_fp == NULL)
    {
        fprintf(stderr, "extract_disc: can't create '%s'\n", tracklist_path);
        rc = 1;
        goto cleanup;
    }

    if (fprintf(tracklist_fp,
                "# Track order pins disc-track assignment: shared.mk numbers tracks\n"
                "# sequentially from 2 in the order listed here. Do not resort this file.\n") < 0)
    {
        fprintf(stderr, "extract_disc: error writing tracklist header\n");
        rc = 1;
        goto cleanup;
    }

    for (i = 0; i < cue.count; i++)
    {
        if (!cue.tracks[i].is_audio)
        {
            continue;
        }

        if (!extract_audio_track(cue_dir, &cue.tracks[i], music_out_dir, tracklist_fp))
        {
            rc = 1;
            goto cleanup;
        }
    }

cleanup:
    if (tracklist_fp != NULL && fclose(tracklist_fp) != 0 && rc == 0)
    {
        fprintf(stderr, "extract_disc: error closing '%s'\n", tracklist_path);
        rc = 1;
    }

    free(root_dir);
    fclose(data_fp);

    return rc;
}
