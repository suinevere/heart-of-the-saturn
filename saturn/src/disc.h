#ifndef DISC_H
#define DISC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int disc_open(const char *cue_path);

int disc_part2_available(void);

int disc_read_file(const char *name, void *out, int max_size);

void disc_play_track(int engine_index, int loop);
void disc_stop_track(void);

void disc_pause_music(void);

void disc_resume_music(void);

int disc_current_track(int *loop);

int disc_music_probe(int *status, int *fadRel, int *lenSectors);

void disc_music_restart_at(unsigned int ms);

void disc_wait_for_music_end(unsigned int cap_ms);

void disc_set_music_volume(uint8_t level);

typedef void (*disc_tick_fn)(void);

void disc_set_tick(disc_tick_fn tick);

void disc_wait_for_music(void);

void disc_close(void);

#ifdef __cplusplus
}
#endif

#endif
