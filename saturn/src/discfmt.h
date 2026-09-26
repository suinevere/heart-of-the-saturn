#ifndef DISCFMT_H
#define DISCFMT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t discfmt_mode1_user_offset(uint32_t lba);

uint32_t discfmt_sector_span(uint32_t size);

int discfmt_iso_name_eq(const char *iso_name, uint8_t iso_len, const char *want);

int discfmt_cue_track_for_music(int engine_index);

#define DISCFMT_MAX_TRACKS 99

#define DISCFMT_RAW_SECTOR 2352

typedef struct {
    int number;
    int is_audio;
    int pregap_sectors;
    char filename[256];
} DiscCueTrack;

typedef struct {
    int count;
    DiscCueTrack tracks[DISCFMT_MAX_TRACKS];
} DiscCue;

int discfmt_cue_parse(const char *text, size_t len, DiscCue *out, int *single_file);

int discfmt_iso_root(const uint8_t *pvd_user, uint32_t *lba, uint32_t *len);

int discfmt_iso_find(const uint8_t *dir, uint32_t dir_len, const char *name, uint32_t *lba, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
