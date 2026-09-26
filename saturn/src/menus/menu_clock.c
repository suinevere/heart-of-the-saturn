#include "menu_clock.h"

static unsigned char menu_ramp(unsigned int elapsed)
{
    unsigned int remaining;

    if (elapsed < MENU_FADE_MS) {
        return (unsigned char)((elapsed * MENU_VOLUME_MAX) / MENU_FADE_MS);
    }
    if (elapsed >= MENU_MUSIC_CYCLE_MS) {
        return 0u;
    }
    remaining = MENU_MUSIC_CYCLE_MS - elapsed;
    if (remaining >= MENU_FADE_MS) {
        return (unsigned char)MENU_VOLUME_MAX;
    }
    return (unsigned char)((remaining * MENU_VOLUME_MAX) / MENU_FADE_MS);
}

void menu_clock_enter(menu_clock_state *st, unsigned int now_ms)
{
    st->music_start_ms = now_ms;
    st->idle_start_ms = now_ms;
}

void menu_clock_step(menu_clock_state *st, unsigned int now_ms, int had_input,
                     menu_clock_frame *out)
{
    unsigned int music_ms;

    out->music_restart = 0;
    out->launch_attract = 0;

    if (had_input) {
        st->idle_start_ms = now_ms;
    }

    music_ms = now_ms - st->music_start_ms;
    if (music_ms >= MENU_MUSIC_CYCLE_MS) {
        st->music_start_ms = now_ms;
        music_ms = 0u;
        out->music_restart = 1;
    }

    if (now_ms - st->idle_start_ms >= MENU_IDLE_MS) {
        out->launch_attract = 1;
    }

    out->music_volume = menu_ramp(music_ms);
}
