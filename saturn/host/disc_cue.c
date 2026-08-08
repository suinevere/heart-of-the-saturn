/*----------------------
 | disc_cue.c
 | Description: Host implementation of disc.h against a real Sega CD
 |   bin/cue rip. Owns every FILE * this sub-project touches -- discfmt.c is
 |   deliberately stdio-free, so all of the actual disc I/O (opening the
 |   cue's directory, opening the data track, seeking and reading sectors)
 |   lives here and nowhere else.
 |
 |   disc_open caches the root ISO9660 directory once and validates the
 |   19-file manifest against it. disc_read_file then does all real lookups
 |   through discfmt_iso_find against that cached directory -- the manifest
 |   below is not a lookup table, it exists only so a bad dump fails loudly
 |   at startup instead of quietly corrupting a room three minutes later.
 |
 |   Design: docs/superpowers/specs/2026-07-31-hota-bincue-disc-backend-design.md
 | Author: suinevere
 | Dependencies: stdio.h, stdlib.h, string.h, dirent-free (the caller
 |   resolves the cd directory's cue sheet, not this file); discfmt.h,
 |   disc.h, disc_manifest.h. Also SDL.h/SDL_mixer.h, for Mix_HookMusic -- disc_play_track
 |   streams CD-DA straight off an audio track's FILE * through the same
 |   device sound.c already opened, so this file crosses the platform
 |   boundary SDL-side as well as stdio-side. client.h for cls.nosound: on
 |   that path Mix_OpenAudioDevice is never called in main.c, so a
 |   disc_play_track that skipped this check would fopen a track file and
 |   hand it to a mixer with no open device for nothing.
 ----------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "discfmt.h"
#include "disc.h"
#include "disc_manifest.h"
#include "client.h"

#define DISC_SECTOR_USER_BYTES 2048

static FILE *disc_data_fp = NULL;
static uint8_t *disc_root_dir = NULL;
static uint32_t disc_root_dir_len = 0;
static int disc_opened = 0;

/*----------------------
 | disc_cue / disc_cue_dir
 | Description: The parsed cue table and the cue's own directory, cached
 |   here rather than re-parsed, so disc_play_track can look up an audio
 |   track's filename by cue track number the same way disc_open already
 |   looked up the data track's. disc_cue_dir is what track filenames
 |   resolve against, not the process CWD -- see disc_dirname.
 | Author: suinevere
 ----------------------*/
static DiscCue disc_cue;
static char disc_cue_dir[512];

/*----------------------
 | disc_music_fp / disc_music_loop / disc_music_eof
 | Description: State for the single currently-hooked CD-DA track.
 |   disc_music_fp is NULL whenever nothing has ever been hooked, or after
 |   disc_stop_track/disc_play_track has closed it -- which is also what
 |   disc_music_callback checks before touching the file -- so a callback
 |   invocation racing a not-yet-completed disc_play_track sees either "no
 |   file yet" (silence) or "the new file" (Mix_HookMusic is only called
 |   after disc_music_fp is set), never a half-updated pair.
 |
 |   disc_music_eof is the one exception to "NULL means nothing is hooked":
 |   the callback's own EOF self-unhook (see disc_music_callback) cannot
 |   fclose the handle itself -- that would risk the main thread touching a
 |   handle mid-close -- so it sets this flag instead and leaves disc_music_fp
 |   open. disc_play_track and disc_stop_track both close on disc_music_fp !=
 |   NULL regardless of this flag, so the file is never leaked past the next
 |   call into either; disc_music_eof exists so that window is a named,
 |   checkable state instead of a silent invariant violation.
 | Author: suinevere
 ----------------------*/
static FILE *disc_music_fp = NULL;
static int disc_music_loop = 0;
static int disc_music_eof = 0;

/*----------------------
 | disc_manifest_entry_t / disc_manifest
 | Description: The 19 blobs the engine reads, demoted from the old deleted
 |   iso-loader's lookup table to an integrity check. Every {name, lba,
 |   size} here is a straight offset/2048 conversion of that table's byte
 |   offsets against the real disc (verified: offset == lba*2048 for all 19,
 |   exactly, no drift) -- so this is not new knowledge, it is the same 19
 |   facts in the form disc_open can check against a live directory read.
 |   The rows themselves come from DISC_MANIFEST_LIST in disc_manifest.h,
 |   because test_vm_memory.c derives the map's size bound from the same
 |   sizes and a second copy of the table would let the two disagree.
 | Author: suinevere
 ----------------------*/
typedef struct
{
    const char *name;
    uint32_t lba;
    uint32_t size;
} disc_manifest_entry_t;

static const disc_manifest_entry_t disc_manifest[] =
{
#define DISC_MANIFEST_ROW(name, lba, size) { name, lba, size },
    DISC_MANIFEST_LIST(DISC_MANIFEST_ROW)
#undef DISC_MANIFEST_ROW
};

#define DISC_MANIFEST_COUNT (sizeof(disc_manifest) / sizeof(disc_manifest[0]))

/*----------------------
 | disc_dirname
 | Description: Splits the directory off cue_path, accepting both '/' and
 |   '\\' since the engine is launched from an arbitrary CWD on Windows and
 |   the cue's own directory (not the process CWD) is what the FILE lines
 |   inside it are relative to. Empty output means "no directory component",
 |   i.e. use the filename as-is.
 | Author: suinevere
 ----------------------*/
static void disc_dirname(const char *path, char *out, size_t out_size)
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
 | disc_read_whole_file
 | Description: Slurps a small text file (the cue sheet) into a malloc'd
 |   buffer. discfmt_cue_parse takes a buffer and a length rather than a
 |   FILE *, by design (discfmt.c stays stdio-free), so this is the one
 |   place that bridges the two.
 | Author: suinevere
 ----------------------*/
static int disc_read_whole_file(const char *path, char **out_text, size_t *out_len)
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

void disc_close(void)
{
    if (disc_data_fp != NULL)
    {
        fclose(disc_data_fp);
        disc_data_fp = NULL;
    }

    if (disc_root_dir != NULL)
    {
        free(disc_root_dir);
        disc_root_dir = NULL;
    }

    disc_root_dir_len = 0;
    disc_opened = 0;

    disc_cue.count = 0;
    disc_cue_dir[0] = '\0';
}

int disc_open(const char *cue_path)
{
    char *cue_text = NULL;
    size_t cue_len = 0;
    int single_file = 0;
    int data_track = -1;
    int i;
    char data_path[768];
    uint8_t pvd_user[DISC_SECTOR_USER_BYTES];
    uint32_t root_lba = 0;
    uint32_t sectors;
    int manifest_ok = 1;

    /* Idempotent: a re-open (or a first open) always starts from a clean
       slate rather than leaking a previous FILE * / buffer. */
    disc_close();

    disc_dirname(cue_path, disc_cue_dir, sizeof(disc_cue_dir));

    if (!disc_read_whole_file(cue_path, &cue_text, &cue_len))
    {
        fprintf(stderr, "disc_open: can't read cue file '%s'\n", cue_path);
        return 0;
    }

    if (!discfmt_cue_parse(cue_text, cue_len, &disc_cue, &single_file))
    {
        free(cue_text);

        if (single_file)
        {
            fprintf(stderr, "disc_open: '%s' is a single-file cue image, which is not supported\n", cue_path);
        }
        else
        {
            fprintf(stderr, "disc_open: '%s' is not a valid cue sheet\n", cue_path);
        }

        /* discfmt_cue_parse writes directly into disc_cue as it walks the
           text, so a parse failure partway through (e.g. track N+1 rejected
           after tracks 1..N were already appended) can leave disc_cue.count
           nonzero even though this open failed. disc_close() zeroes it back
           out so a failed disc_open never leaves disc_play_track a
           partially-populated table to iterate. */
        disc_close();
        return 0;
    }

    free(cue_text);

    for (i = 0; i < disc_cue.count; i++)
    {
        if (!disc_cue.tracks[i].is_audio)
        {
            data_track = i;
            break;
        }
    }

    if (data_track < 0)
    {
        fprintf(stderr, "disc_open: '%s' has no data track\n", cue_path);
        /* disc_cue is already populated by the successful parse above --
           leaving it that way would let disc_play_track iterate a cue table
           for a disc that was never successfully opened. See disc.h for the
           contract this upholds. */
        disc_close();
        return 0;
    }

    if (disc_cue_dir[0] != '\0')
    {
        snprintf(data_path, sizeof(data_path), "%s/%s", disc_cue_dir, disc_cue.tracks[data_track].filename);
    }
    else
    {
        snprintf(data_path, sizeof(data_path), "%s", disc_cue.tracks[data_track].filename);
    }

    disc_data_fp = fopen(data_path, "rb");
    if (disc_data_fp == NULL)
    {
        fprintf(stderr, "disc_open: can't open data track '%s'\n", data_path);
        /* Same reasoning as the no-data-track branch above: disc_cue is
           already populated, and disc_opened must stay 0 on a failed open. */
        disc_close();
        return 0;
    }

    if (fseek(disc_data_fp, (long)discfmt_mode1_user_offset(16), SEEK_SET) != 0 ||
        fread(pvd_user, 1, sizeof(pvd_user), disc_data_fp) != sizeof(pvd_user))
    {
        fprintf(stderr, "disc_open: can't read PVD sector (LBA 16) from '%s'\n", data_path);
        disc_close();
        return 0;
    }

    if (!discfmt_iso_root(pvd_user, &root_lba, &disc_root_dir_len))
    {
        fprintf(stderr, "disc_open: '%s' does not carry a valid ISO9660 PVD at LBA 16\n", data_path);
        disc_close();
        return 0;
    }

    sectors = discfmt_sector_span(disc_root_dir_len);
    disc_root_dir = (uint8_t *)malloc((size_t)sectors * DISC_SECTOR_USER_BYTES);
    if (disc_root_dir == NULL)
    {
        fprintf(stderr, "disc_open: out of memory reading %u-byte root directory\n", (unsigned)disc_root_dir_len);
        disc_close();
        return 0;
    }

    /* Root directory read: one fseek/fread pair per sector, never a single
       contiguous read -- MODE1/2352 interleaves 304 bytes of sync/header/ECC
       between every 2048 bytes of payload, exactly the trap discfmt.h warns
       about for every disc read in this file. */
    for (i = 0; i < (int)sectors; i++)
    {
        long off = (long)discfmt_mode1_user_offset(root_lba + (uint32_t)i);

        if (fseek(disc_data_fp, off, SEEK_SET) != 0 ||
            fread(disc_root_dir + (size_t)i * DISC_SECTOR_USER_BYTES, 1, DISC_SECTOR_USER_BYTES, disc_data_fp) != DISC_SECTOR_USER_BYTES)
        {
            fprintf(stderr, "disc_open: error reading root directory sector %d\n", i);
            disc_close();
            return 0;
        }
    }

    disc_opened = 1;

    /* Validation pass: missing file or size mismatch is fatal (size is what
       protects every disc_read_file destination buffer); an LBA mismatch is
       only a warning, since it fingerprints this particular dump rather
       than its correctness -- a differently-mastered but perfectly playable
       disc shifts every LBA while every size stays identical. */
    for (i = 0; i < (int)DISC_MANIFEST_COUNT; i++)
    {
        uint32_t lba = 0, size = 0;

        if (!discfmt_iso_find(disc_root_dir, disc_root_dir_len, disc_manifest[i].name, &lba, &size))
        {
            fprintf(stderr, "disc_open: FATAL: manifest file '%s' not found on disc\n", disc_manifest[i].name);
            manifest_ok = 0;
            continue;
        }

        if (size != disc_manifest[i].size)
        {
            fprintf(stderr, "disc_open: FATAL: '%s' is %u bytes on disc, manifest expects %u\n",
                    disc_manifest[i].name, (unsigned)size, (unsigned)disc_manifest[i].size);
            manifest_ok = 0;
        }

        if (lba != disc_manifest[i].lba)
        {
            fprintf(stderr, "disc_open: WARNING: '%s' is at LBA %u, manifest expects %u (differently-mastered dump?)\n",
                    disc_manifest[i].name, (unsigned)lba, (unsigned)disc_manifest[i].lba);
        }
    }

    if (!manifest_ok)
    {
        disc_close();
        return 0;
    }

    return 1;
}

int disc_read_file(const char *name, void *out, int max_size)
{
    uint32_t lba = 0, size = 0, sectors, i;
    unsigned char *dst = (unsigned char *)out;

    if (!disc_opened || disc_data_fp == NULL)
    {
        fprintf(stderr, "disc_read_file: disc not open, can't read '%s'\n", name);
        return -1;
    }

    if (!discfmt_iso_find(disc_root_dir, disc_root_dir_len, name, &lba, &size))
    {
        fprintf(stderr, "disc_read_file: '%s' not found on disc\n", name);
        return -1;
    }

    if (max_size < 0 || size > (uint32_t)max_size)
    {
        fprintf(stderr, "disc_read_file: '%s' is %u bytes, only %d available at destination\n",
                name, (unsigned)size, max_size);
        return -1;
    }

    /* Per-sector loop, never one fread across the whole span: see disc.h /
       discfmt_mode1_user_offset for why a contiguous read is wrong here. */
    sectors = discfmt_sector_span(size);
    for (i = 0; i < sectors; i++)
    {
        long off = (long)discfmt_mode1_user_offset(lba + i);
        uint32_t remaining = size - i * DISC_SECTOR_USER_BYTES;
        uint32_t chunk = (remaining < DISC_SECTOR_USER_BYTES) ? remaining : DISC_SECTOR_USER_BYTES;

        if (fseek(disc_data_fp, off, SEEK_SET) != 0 ||
            fread(dst + (size_t)i * DISC_SECTOR_USER_BYTES, 1, chunk, disc_data_fp) != chunk)
        {
            fprintf(stderr, "disc_read_file: error reading '%s' at sector LBA %u\n", name, lba + i);
            return -1;
        }
    }

    return 0;
}

/*----------------------
 | disc_music_callback
 | Description: The Mix_HookMusic callback. Audio tracks are raw CD-DA --
 |   44100 Hz/16-bit signed/stereo/little-endian, no header -- and main.c's
 |   audio device is opened in that exact format with no
 |   SDL_AUDIO_ALLOW_ANY_CHANGE (see main.c), so this is a buffered fread
 |   straight into the mixer's buffer: no decode, no resample.
 |
 |   Runs on SDL_mixer's audio thread, not the main thread -- it must never
 |   touch disc_music_fp in a way that races disc_play_track/disc_stop_track
 |   closing it. It doesn't: those two always call Mix_HookMusic(NULL, NULL)
 |   before fclose, and SDL_mixer guarantees the outgoing hook is not
 |   invoked again once that call returns.
 | Author: suinevere
 ----------------------*/
static void disc_music_callback(void *udata, Uint8 *stream, int len)
{
    size_t want = (size_t)len;
    size_t got;

    (void)udata;

    if (disc_music_fp == NULL)
    {
        memset(stream, 0, want);
        return;
    }

    got = fread(stream, 1, want, disc_music_fp);

    if (got < want && disc_music_loop != 0)
    {
        /* CD-DA tracks carry no header, so "the start of the track" is
           just byte 0 of the file -- looping is a plain rewind. */
        if (fseek(disc_music_fp, 0, SEEK_SET) == 0)
        {
            got += fread(stream + got, 1, want - got, disc_music_fp);
        }
    }

    if (got < want)
    {
        /* EOF with nothing left to loop back to (or a failed rewind seek):
           zero-fill the remainder so the mixer never plays stale buffer
           contents, then unhook so this callback stops being invoked with
           a file that has nothing left to give it. Set disc_music_eof
           rather than fclose disc_music_fp here -- this runs on the audio
           thread and the main thread could be mid-way through reading
           disc_music_fp; the actual close happens on the main thread, the
           next time disc_play_track or disc_stop_track runs (see
           disc_music_eof's comment above). */
        memset(stream + got, 0, want - got);
        Mix_HookMusic(NULL, NULL);
        disc_music_eof = 1;
    }
}

/*----------------------
 | disc_play_track
 | Description: Looks engine_index up through discfmt_cue_track_for_music
 |   and the cached cue table from disc_open, then hooks disc_music_callback
 |   onto the resulting audio track file. See disc.h for the seam and
 |   disc_music_callback above for the streaming format.
 |
 |   cls.nosound short-circuits this before any fopen: on that path main.c
 |   never calls Mix_OpenAudioDevice, so there is no device for
 |   Mix_HookMusic to feed and opening the track file would be pure waste.
 |
 |   Also re-verifies the negotiated device spec via Mix_QuerySpec on every
 |   call, independent of main.c's own startup check -- this is the raw
 |   fread streamer with no resampler, so 44100 Hz/AUDIO_S16/stereo is a
 |   hard precondition, not an optimization. Checking here rather than
 |   trusting a flag set once at startup means this function is correct on
 |   its own even if that startup check is ever refactored away.
 | Author: suinevere
 ----------------------*/
void disc_play_track(int engine_index, int loop)
{
    int cue_track;
    int i;
    const char *filename = NULL;
    char path[768];
    FILE *fp;
    int spec_freq = 0;
    Uint16 spec_format = 0;
    int spec_channels = 0;

    if (cls.nosound != 0)
    {
        return;
    }

    /* Same precondition disc_read_file guards on (disc.h documents both):
       without it, a disc_open that failed past cue parsing but before this
       guard existed would still iterate disc_cue and could stream a track
       off a disc that was never successfully opened. */
    if (!disc_opened)
    {
        fprintf(stderr, "disc_play_track: disc not open, can't play engine index %d\n", engine_index);
        return;
    }

    Mix_QuerySpec(&spec_freq, &spec_format, &spec_channels);
    if (spec_freq != 44100 || spec_format != AUDIO_S16 || spec_channels != 2)
    {
        /* decode.c calls disc_play_track once per music bytecode opcode, so
           on a device that stays mismatched this check would otherwise fire
           every time -- a three-line stderr block per opcode. Print the
           full warning once; every call after that still refuses to play,
           just silently, since the first message already said everything
           there is to say. */
        static int warned = 0;

        if (!warned)
        {
            fprintf(stderr, "disc_play_track: audio device is freq=%d format=0x%x channels=%d, "
                            "not 44100/AUDIO_S16(0x%x)/2 -- refusing to play track (would be "
                            "pitched/timed wrong)\n",
                    spec_freq, (unsigned)spec_format, spec_channels, (unsigned)AUDIO_S16);
            warned = 1;
        }

        return;
    }

    cue_track = discfmt_cue_track_for_music(engine_index);
    if (cue_track == 0)
    {
        fprintf(stderr, "disc_play_track: engine index %d has no cue track mapping\n", engine_index);
        return;
    }

    for (i = 0; i < disc_cue.count; i++)
    {
        if (disc_cue.tracks[i].is_audio && disc_cue.tracks[i].number == cue_track)
        {
            filename = disc_cue.tracks[i].filename;
            break;
        }
    }

    if (filename == NULL)
    {
        fprintf(stderr, "disc_play_track: cue track %d (engine index %d) not found in cue sheet\n", cue_track, engine_index);
        return;
    }

    if (disc_cue_dir[0] != '\0')
    {
        snprintf(path, sizeof(path), "%s/%s", disc_cue_dir, filename);
    }
    else
    {
        snprintf(path, sizeof(path), "%s", filename);
    }

    /* Unhook-then-close the previous track before opening the new one --
       exactly the disc_stop_track sequence, so there is no window where
       the audio thread could still be reading through a handle about to
       be fclose'd. */
    disc_stop_track();

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "disc_play_track: can't open track file '%s'\n", path);
        return;
    }

    disc_music_fp = fp;
    disc_music_loop = loop;
    disc_music_eof = 0;
    Mix_HookMusic(disc_music_callback, NULL);
}

/*----------------------
 | disc_stop_track
 | Description: Mix_HookMusic(NULL, NULL) before fclose, always, with no
 |   path that skips it -- see disc_music_callback. Safe to call with
 |   nothing playing (disc_music_fp == NULL): Mix_HookMusic(NULL, NULL) is
 |   a harmless no-op unhook and the fclose is skipped.
 | Author: suinevere
 ----------------------*/
void disc_stop_track(void)
{
    Mix_HookMusic(NULL, NULL);

    if (disc_music_fp != NULL)
    {
        fclose(disc_music_fp);
        disc_music_fp = NULL;
    }

    disc_music_eof = 0;
}
