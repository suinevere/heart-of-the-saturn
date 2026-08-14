/*----------------------
 | test_menu_clock.c
 | Description: Host unit tests for menu_clock.c. discfmt.c is linked only so
 |   the music index can be checked against the cue track it resolves to.
 | Author: suinevere
 | Dependencies: menu_clock.h, discfmt.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "menu_clock.h"
#include "discfmt.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void test_music_index_resolves_to_track_three(void)
{
    expect_int("MENU_MUSIC_INDEX maps to cue track 3",
               discfmt_cue_track_for_music(MENU_MUSIC_INDEX), 3);
}

static void test_fade_in_from_entry(void)
{
    menu_clock_state st;
    menu_clock_frame f;

    menu_clock_enter(&st, 5000u);

    menu_clock_step(&st, 5000u, 0, &f);
    expect_int("entry starts silent", (int)f.music_volume, 0);
    expect_int("entry does not ask for a restart", f.music_restart, 0);

    menu_clock_step(&st, 5000u + 500u, 0, &f);
    expect_int("half a fade in is half volume", (int)f.music_volume, 3);

    menu_clock_step(&st, 5000u + 1000u, 0, &f);
    expect_int("a full fade in is full volume", (int)f.music_volume,
               (int)MENU_VOLUME_MAX);
}

static void test_fade_out_completes_at_the_cycle(void)
{
    menu_clock_state st;
    menu_clock_frame f;

    menu_clock_enter(&st, 0u);

    menu_clock_step(&st, 39000u, 1, &f);
    expect_int("still full a second before the end", (int)f.music_volume,
               (int)MENU_VOLUME_MAX);

    menu_clock_step(&st, 39500u, 1, &f);
    expect_int("half faded out", (int)f.music_volume, 3);

    menu_clock_step(&st, 40000u, 1, &f);
    expect_int("the cycle restarts the track", f.music_restart, 1);
    expect_int("and it restarts silent", (int)f.music_volume, 0);

    menu_clock_step(&st, 41000u, 1, &f);
    expect_int("then fades back in", (int)f.music_volume,
               (int)MENU_VOLUME_MAX);
    expect_int("without restarting again", f.music_restart, 0);
}

static void test_idle_launches_the_attract(void)
{
    menu_clock_state st;
    menu_clock_frame f;

    menu_clock_enter(&st, 0u);

    menu_clock_step(&st, 14999u, 0, &f);
    expect_int("not yet idle", f.launch_attract, 0);

    menu_clock_step(&st, 15000u, 0, &f);
    expect_int("fifteen seconds launches the attract", f.launch_attract, 1);
}

static void test_input_resets_the_idle_timer(void)
{
    menu_clock_state st;
    menu_clock_frame f;

    menu_clock_enter(&st, 0u);

    menu_clock_step(&st, 14000u, 1, &f);
    expect_int("input at fourteen seconds does not launch", f.launch_attract, 0);

    menu_clock_step(&st, 28000u, 0, &f);
    expect_int("fourteen more seconds still does not launch",
               f.launch_attract, 0);

    menu_clock_step(&st, 29000u, 0, &f);
    expect_int("fifteen after the input does", f.launch_attract, 1);
}

static void test_correct_across_the_counter_wrap(void)
{
    menu_clock_state st;
    menu_clock_frame f;
    unsigned int base = 0xFFFFF000u;

    menu_clock_enter(&st, base);

    menu_clock_step(&st, base + 500u, 0, &f);
    expect_int("fade in survives the wrap", (int)f.music_volume, 3);

    menu_clock_step(&st, base + 15000u, 0, &f);
    expect_int("the idle trigger survives the wrap", f.launch_attract, 1);

    menu_clock_step(&st, base + 40000u, 1, &f);
    expect_int("the music cycle survives the wrap", f.music_restart, 1);
}

int main(void)
{
    test_music_index_resolves_to_track_three();
    test_fade_in_from_entry();
    test_fade_out_completes_at_the_cycle();
    test_idle_launches_the_attract();
    test_input_resets_the_idle_timer();
    test_correct_across_the_counter_wrap();

    if (g_fail != 0) {
        printf("menu_clock: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("menu_clock: all tests passed\n");
    return 0;
}
