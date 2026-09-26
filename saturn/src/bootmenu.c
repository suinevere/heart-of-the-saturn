#include "bootmenu.h"

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

void bootmenu_init(bootmenu_state *st, uint32_t now_ms,
                   int part1_available, int part2_available)
{
    st->music_start_ms = now_ms;
    st->highlight = (int)BOOT_ENTRY_OUT_OF_THIS_WORLD;
    st->music_started = 0;
    st->part1_available = part1_available;
    st->part2_available = part2_available;
}

void bootmenu_step(bootmenu_state *st, uint32_t now_ms, uint32_t pressed,
                   boot_frame *out)
{
    out->music_restart = 0;
    out->start_game = 0;
    out->start_part1 = 0;

    if (!st->music_started)
    {
        st->music_started = 1;
        st->music_start_ms = now_ms;
        out->music_restart = 1;
    }
    else if (now_ms - st->music_start_ms >= BOOT_MUSIC_LOOP_MS)
    {
        st->music_start_ms = now_ms;
        out->music_restart = 1;
    }

    if (pressed != 0u)
    {
        int highlight_before_move = st->highlight;

        if ((pressed & BOOT_KEY_MOVE) != 0u)
        {
            st->highlight = (st->highlight == (int)BOOT_ENTRY_OUT_OF_THIS_WORLD)
                          ? (int)BOOT_ENTRY_HEART_OF_THE_ALIEN
                          : (int)BOOT_ENTRY_OUT_OF_THIS_WORLD;
        }

        if ((pressed & BOOT_KEY_CONFIRM) != 0u)
        {
            if (highlight_before_move == (int)BOOT_ENTRY_HEART_OF_THE_ALIEN
                && st->part2_available)
            {
                out->start_game = 1;
            }
            else if (highlight_before_move == (int)BOOT_ENTRY_OUT_OF_THIS_WORLD
                     && st->part1_available)
            {
                out->start_part1 = 1;
            }
        }
    }

    out->highlight = (boot_entry)st->highlight;
    out->music_volume = boot_ramp(now_ms - st->music_start_ms, BOOT_MUSIC_LOOP_MS);
}
