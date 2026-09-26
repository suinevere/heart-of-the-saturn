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

static DiscCue disc_cue;
static char disc_cue_dir[512];

static FILE *disc_music_fp = NULL;
static int disc_music_loop = 0;
static long disc_music_start = 0;
static int disc_music_eof = 0;

static int disc_music_track = -1;

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
        if (fseek(disc_music_fp, disc_music_start, SEEK_SET) == 0)
        {
            got += fread(stream + got, 1, want - got, disc_music_fp);
        }
    }

    if (got < want)
    {
        memset(stream + got, 0, want - got);
        Mix_HookMusic(NULL, NULL);
        disc_music_eof = 1;
    }
}

void disc_play_track(int engine_index, int loop)
{
    int cue_track;
    int i;
    const char *filename = NULL;
    long pregap = 0;
    char path[768];
    FILE *fp;
    int spec_freq = 0;
    Uint16 spec_format = 0;
    int spec_channels = 0;

    if (cls.nosound != 0)
    {
        return;
    }

    if (!disc_opened)
    {
        fprintf(stderr, "disc_play_track: disc not open, can't play engine index %d\n", engine_index);
        return;
    }

    Mix_QuerySpec(&spec_freq, &spec_format, &spec_channels);
    if (spec_freq != 44100 || spec_format != AUDIO_S16 || spec_channels != 2)
    {
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
            pregap = (long)disc_cue.tracks[i].pregap_sectors * DISCFMT_RAW_SECTOR;
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

    disc_stop_track();

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "disc_play_track: can't open track file '%s'\n", path);
        return;
    }

    if (pregap > 0 && fseek(fp, pregap, SEEK_SET) != 0)
    {
        fprintf(stderr, "disc_play_track: can't seek past pregap of '%s'\n", path);
        pregap = 0;
    }

    disc_music_fp = fp;
    disc_music_loop = loop;
    disc_music_track = engine_index;
    disc_music_start = pregap;
    disc_music_eof = 0;
    Mix_HookMusic(disc_music_callback, NULL);
}

void disc_set_tick(disc_tick_fn tick)
{
    (void)tick;
}

void disc_wait_for_music(void)
{
}

static int disc_music_paused = 0;

void disc_pause_music(void)
{
    if (disc_music_paused || disc_music_fp == NULL)
    {
        return;
    }
    disc_music_paused = 1;
    Mix_HookMusic(NULL, NULL);
}

void disc_resume_music(void)
{
    if (!disc_music_paused)
    {
        return;
    }
    disc_music_paused = 0;
    Mix_HookMusic(disc_music_callback, NULL);
}

void disc_stop_track(void)
{
    disc_music_paused = 0;
    Mix_HookMusic(NULL, NULL);

    if (disc_music_fp != NULL)
    {
        fclose(disc_music_fp);
        disc_music_fp = NULL;
        disc_music_track = -1;
    }

    disc_music_eof = 0;
}

void disc_music_restart_at(unsigned int ms)
{
    (void)ms;
}

void disc_wait_for_music_end(unsigned int cap_ms)
{
    (void)cap_ms;
}

int disc_current_track(int *loop)
{
    if (loop != NULL)
    {
        *loop = disc_music_loop;
    }
    return disc_music_track;
}

void disc_set_music_volume(uint8_t level)
{
    (void)level;
}
