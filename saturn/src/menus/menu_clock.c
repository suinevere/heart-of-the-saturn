/*----------------------
 | menu_clock.c
 | Description: The sub-title screen's clock. See menu_clock.h for the
 |   contract; this file holds only the volume staircase and the two timer
 |   comparisons that drive it.
 | Author: suinevere
 | Dependencies: menu_clock.h
 | Globals: N/A
 ----------------------*/
#include "menu_clock.h"

/*----------------------
 | menu_ramp
 | Description: CD-DA volume for a point in the music cycle: fading in from
 |   silence over the first MENU_FADE_MS, full for the middle stretch, then
 |   fading back to silence over the last MENU_FADE_MS before the cycle
 |   restarts.
 |
 |   menu_clock_step resets music_start_ms to now_ms in the same frame it
 |   reports music_restart, which zeroes elapsed and makes this function
 |   return 0 for that frame. That is deliberate: the restart and the
 |   silence must coincide, because that is the fix for the boot menu's
 |   known seam click, where the attract restart raised volume 0->7 in the
 |   same frame as disc_play_track. Reporting music_restart with a nonzero
 |   volume would reintroduce that click here.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: elapsed -- ms since the current music cycle started
 | Returns: 0..MENU_VOLUME_MAX
 ----------------------*/
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
