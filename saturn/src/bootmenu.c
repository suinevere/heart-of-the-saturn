/*----------------------
 | bootmenu.c
 | Description: The boot sequence's state machine. See bootmenu.h for the
 |   contract and the design spec for why the timings are what they are.
 | Author: suinevere
 | Dependencies: bootmenu.h
 | Globals: N/A
 ----------------------*/
#include "bootmenu.h"

/*----------------------
 | boot_ramp
 | Description: CD-DA volume approaching a deadline: full until the fade
 |   window opens, then stepping down to silence as the deadline arrives.
 |   Integer division means it reaches 0 shortly before the deadline rather
 |   than exactly on it, which is the right direction to err -- the track is
 |   already inaudible when the screen changes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: elapsed -- ms since the thing being timed started; deadline -- ms
 |         at which it ends
 | Returns: 0..BOOT_VOLUME_MAX
 ----------------------*/
static uint8_t boot_ramp(uint32_t elapsed, uint32_t deadline)
{
    uint32_t remaining;

    if (elapsed >= deadline)
    {
        return 0u;
    }

    remaining = deadline - elapsed;

    if (remaining >= BOOT_FADE_MS)
    {
        return (uint8_t)BOOT_VOLUME_MAX;
    }

    return (uint8_t)((remaining * BOOT_VOLUME_MAX) / BOOT_FADE_MS);
}

void bootmenu_init(bootmenu_state *st, uint32_t now_ms)
{
    st->phase_start_ms = now_ms;
    st->music_start_ms = now_ms;
    st->idle_start_ms = now_ms;
    st->in_menu = 0;
    st->highlight = (int)BOOT_ENTRY_OUT_OF_THIS_WORLD;
    st->music_started = 0;
}

void bootmenu_step(bootmenu_state *st, uint32_t now_ms, uint32_t pressed,
                   boot_frame *out)
{
    uint32_t phase_ms;
    uint32_t music_ms;
    uint8_t volume;
    uint8_t capped;

    out->music_restart = 0;
    out->start_game = 0;

    if (!st->music_started)
    {
        st->music_started = 1;
        out->music_restart = 1;
    }

    if (!st->in_menu)
    {
        phase_ms = now_ms - st->phase_start_ms;

        if (pressed != 0u || phase_ms >= BOOT_OPENING_MS)
        {
            st->in_menu = 1;
            st->idle_start_ms = now_ms;
        }
    }
    else if (pressed != 0u)
    {
        int highlight_before_move = st->highlight;

        st->idle_start_ms = now_ms;

        if ((pressed & BOOT_KEY_MOVE) != 0u)
        {
            st->highlight = (st->highlight == (int)BOOT_ENTRY_OUT_OF_THIS_WORLD)
                          ? (int)BOOT_ENTRY_HEART_OF_THE_ALIEN
                          : (int)BOOT_ENTRY_OUT_OF_THIS_WORLD;
        }

        if ((pressed & BOOT_KEY_CONFIRM) != 0u
            && highlight_before_move == (int)BOOT_ENTRY_HEART_OF_THE_ALIEN)
        {
            out->start_game = 1;
        }
    }

    if (st->in_menu && now_ms - st->idle_start_ms >= BOOT_MENU_IDLE_MS)
    {
        st->in_menu = 0;
        st->phase_start_ms = now_ms;
        st->music_start_ms = now_ms;
        out->music_restart = 1;
    }

    phase_ms = now_ms - st->phase_start_ms;
    music_ms = now_ms - st->music_start_ms;

    if (st->in_menu)
    {
        out->screen = BOOT_SCREEN_MENU;
        volume = boot_ramp(now_ms - st->idle_start_ms, BOOT_MENU_IDLE_MS);
    }
    else
    {
        out->screen = (boot_screen)(phase_ms / BOOT_STILL_MS);
        volume = (uint8_t)BOOT_VOLUME_MAX;
    }

    capped = boot_ramp(music_ms, BOOT_MUSIC_CAP_MS);

    if (capped < volume)
    {
        volume = capped;
    }

    out->highlight = (boot_entry)st->highlight;
    out->music_volume = volume;
}
