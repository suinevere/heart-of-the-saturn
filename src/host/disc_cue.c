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
 |   disc.h
 ----------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "discfmt.h"
#include "disc.h"

#define DISC_SECTOR_USER_BYTES 2048

static FILE *disc_data_fp = NULL;
static uint8_t *disc_root_dir = NULL;
static uint32_t disc_root_dir_len = 0;
static int disc_opened = 0;

/*----------------------
 | disc_manifest_entry_t / disc_manifest
 | Description: The 19 blobs the engine reads, demoted from the old deleted
 |   iso-loader's lookup table to an integrity check. Every {name, lba,
 |   size} here is a straight offset/2048 conversion of that table's byte
 |   offsets against the real disc (verified: offset == lba*2048 for all 19,
 |   exactly, no drift) -- so this is not new knowledge, it is the same 19
 |   facts in the form disc_open can check against a live directory read.
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
    { "END1.BIN",     2593, 432128 },
    { "END2.BIN",     2804, 432128 },
    { "END3.BIN",     3015, 432128 },
    { "END4.BIN",     3226, 432128 },
    { "GAME2.BIN",    1082, 409600 },
    { "INTRO1.BIN",    238, 432128 },
    { "INTRO2.BIN",    449, 432128 },
    { "INTRO3.BIN",    660, 432128 },
    { "INTRO4.BIN",    871, 432128 },
    { "MAKE2MB.BIN",  3650, 436224 },
    { "MID2.BIN",     3863, 432128 },
    { "ROOMS1.BIN",   4501, 370688 },
    { "ROOMS2.BIN",   1282, 370688 },
    { "ROOMS3.BIN",   1463, 370688 },
    { "ROOMS4.BIN",   1644, 370688 },
    { "ROOMS5.BIN",   1825, 370688 },
    { "ROOMS6.BIN",   4139, 370688 },
    { "ROOMS7.BIN",   2006, 160826 },
    { "ROOMS8.BIN",   4320, 370688 }
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
}

int disc_open(const char *cue_path)
{
    char dir[512];
    char *cue_text = NULL;
    size_t cue_len = 0;
    DiscCue cue;
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

    disc_dirname(cue_path, dir, sizeof(dir));

    if (!disc_read_whole_file(cue_path, &cue_text, &cue_len))
    {
        fprintf(stderr, "disc_open: can't read cue file '%s'\n", cue_path);
        return 0;
    }

    if (!discfmt_cue_parse(cue_text, cue_len, &cue, &single_file))
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

        return 0;
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
        fprintf(stderr, "disc_open: '%s' has no data track\n", cue_path);
        return 0;
    }

    if (dir[0] != '\0')
    {
        snprintf(data_path, sizeof(data_path), "%s/%s", dir, cue.tracks[data_track].filename);
    }
    else
    {
        snprintf(data_path, sizeof(data_path), "%s", cue.tracks[data_track].filename);
    }

    disc_data_fp = fopen(data_path, "rb");
    if (disc_data_fp == NULL)
    {
        fprintf(stderr, "disc_open: can't open data track '%s'\n", data_path);
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
 | disc_play_track / disc_stop_track
 | Description: No-op stubs for this task -- see disc.h. Nothing calls
 |   these yet; music still runs through music.c/SDL_mixer until Task 6.
 | Author: suinevere
 ----------------------*/
void disc_play_track(int engine_index, int loop)
{
    (void)engine_index;
    (void)loop;
}

void disc_stop_track(void)
{
}
