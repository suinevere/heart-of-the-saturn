#ifndef MENU_CLOCK_H
#define MENU_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_MUSIC_INDEX     23

#define MENU_MUSIC_CYCLE_MS  40000u

#define MENU_FADE_MS          1000u

#define MENU_IDLE_MS         15000u

#define MENU_VOLUME_MAX          7u

typedef struct {
    unsigned int music_start_ms;
    unsigned int idle_start_ms;
} menu_clock_state;

typedef struct {
    unsigned char music_volume;
    int           music_restart;
    int           launch_attract;
} menu_clock_frame;

void menu_clock_enter(menu_clock_state *st, unsigned int now_ms);

void menu_clock_step(menu_clock_state *st, unsigned int now_ms, int had_input,
                     menu_clock_frame *out);

#ifdef __cplusplus
}
#endif

#endif
