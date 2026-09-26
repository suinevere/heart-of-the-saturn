#include <stdio.h>
#include "bootmenu.h"
#include "discfmt.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static uint32_t held_edges(uint32_t current, uint32_t *previous)
{
    uint32_t pressed = current & ~*previous;
    *previous = current;
    return pressed;
}

static bootmenu_state g_st;

static void at_page_with(int part1, int part2)
{
    boot_frame f;
    bootmenu_init(&g_st, 0u, part1, part2);
    bootmenu_step(&g_st, 0u, 0u, &f);
}

static void test_music_index_resolves_to_track_seventeen(void)
{
    expect_int("BOOT_MUSIC_INDEX maps to cue track 17 (track17.wav, 1:09)",
               discfmt_cue_track_for_music(BOOT_MUSIC_INDEX), 17);
}

static void test_first_step_starts_music(void)
{
    boot_frame f;
    bootmenu_init(&g_st, 1000u, 1, 1);
    bootmenu_step(&g_st, 1000u, 0u, &f);
    expect_int("the first step starts the music", f.music_restart, 1);
    expect_int("at full volume", (int)f.music_volume, (int)BOOT_VOLUME_MAX);
    bootmenu_step(&g_st, 1100u, 0u, &f);
    expect_int("and only the first", f.music_restart, 0);
}

static void test_cursor_starts_on_part_one(void)
{
    boot_frame f;
    at_page_with(1, 1);
    bootmenu_step(&g_st, 100u, 0u, &f);
    expect_int("the cursor starts on OUT OF THIS WORLD",
               (int)f.highlight, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
}

static void test_every_direction_toggles(void)
{
    static const uint32_t KEYS[4] = {
        BOOT_KEY_RIGHT, BOOT_KEY_LEFT, BOOT_KEY_DOWN, BOOT_KEY_UP
    };
    boot_frame f;
    int i;

    at_page_with(1, 1);
    for (i = 0; i < 4; i++) {
        bootmenu_step(&g_st, (uint32_t)(100 + i * 100), KEYS[i], &f);
        expect_int("a direction moves to the other logo", (int)f.highlight,
                   (i % 2 == 0) ? (int)BOOT_ENTRY_HEART_OF_THE_ALIEN
                                : (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
    }
}

static void test_held_direction_moves_once(void)
{
    boot_frame f;
    uint32_t previous = 0u;
    int frame;

    at_page_with(1, 1);
    for (frame = 0; frame < 5; frame++) {
        bootmenu_step(&g_st, (uint32_t)(100 + frame * 100),
                      held_edges(BOOT_KEY_RIGHT, &previous), &f);
        expect_int("holding right moves once", (int)f.highlight,
                   (int)BOOT_ENTRY_HEART_OF_THE_ALIEN);
    }
}

static void test_move_and_confirm_on_one_frame(void)
{
    boot_frame f;
    at_page_with(1, 1);
    bootmenu_step(&g_st, 100u, BOOT_KEY_RIGHT | BOOT_KEY_A, &f);
    expect_int("the move still happens", (int)f.highlight,
               (int)BOOT_ENTRY_HEART_OF_THE_ALIEN);
    expect_int("but the confirm is for the logo lit before it", f.start_part1, 1);
    expect_int("not the one lit after", f.start_game, 0);
}

static void test_part_one_starts_when_available(void)
{
    boot_frame f;
    at_page_with(1, 1);
    bootmenu_step(&g_st, 100u, BOOT_KEY_A, &f);
    expect_int("A on OUT OF THIS WORLD starts Part I", f.start_part1, 1);
    expect_int("and not Part II", f.start_game, 0);
}

static void test_part_one_inert_when_unavailable(void)
{
    boot_frame f;
    at_page_with(0, 1);
    bootmenu_step(&g_st, 100u, BOOT_KEY_A, &f);
    expect_int("A on a missing Part I does nothing", f.start_part1, 0);
    expect_int("the cursor stays", (int)f.highlight,
               (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
}

static void test_part_two_starts_the_game(void)
{
    boot_frame f;
    at_page_with(1, 1);
    bootmenu_step(&g_st, 100u, BOOT_KEY_RIGHT, &f);
    bootmenu_step(&g_st, 200u, BOOT_KEY_B, &f);
    expect_int("B on HEART OF THE ALIEN starts the game", f.start_game, 1);
}

static void test_part_two_inert_when_unavailable(void)
{
    boot_frame f;
    at_page_with(1, 0);
    bootmenu_step(&g_st, 100u, BOOT_KEY_RIGHT, &f);
    bootmenu_step(&g_st, 200u, BOOT_KEY_C, &f);
    expect_int("C on a missing Part II does nothing", f.start_game, 0);
    expect_int("the cursor stays", (int)f.highlight,
               (int)BOOT_ENTRY_HEART_OF_THE_ALIEN);
}

static void test_music_fades_and_loops(void)
{
    boot_frame f;
    at_page_with(1, 1);

    bootmenu_step(&g_st, BOOT_MUSIC_LOOP_MS - BOOT_FADE_MS - 1u, 0u, &f);
    expect_int("full volume before the fade window", (int)f.music_volume,
               (int)BOOT_VOLUME_MAX);

    bootmenu_step(&g_st, BOOT_MUSIC_LOOP_MS - BOOT_FADE_MS / 2u, 0u, &f);
    if (f.music_volume == 0u || f.music_volume >= BOOT_VOLUME_MAX) {
        g_fail++;
        printf("FAIL mid-fade volume is between silence and full: %u\n",
               (unsigned)f.music_volume);
    }

    bootmenu_step(&g_st, BOOT_MUSIC_LOOP_MS - 1u, 0u, &f);
    expect_int("near silence at the end of the pass", (int)f.music_volume, 0);
    expect_int("not yet restarted", f.music_restart, 0);

    bootmenu_step(&g_st, BOOT_MUSIC_LOOP_MS, 0u, &f);
    expect_int("the track restarts", f.music_restart, 1);
    expect_int("at full volume", (int)f.music_volume, (int)BOOT_VOLUME_MAX);
}

static void test_input_does_not_hold_back_the_loop(void)
{
    boot_frame f;
    at_page_with(1, 1);
    bootmenu_step(&g_st, BOOT_MUSIC_LOOP_MS - 10u, BOOT_KEY_RIGHT, &f);
    bootmenu_step(&g_st, BOOT_MUSIC_LOOP_MS, 0u, &f);
    expect_int("a press does not delay the restart", f.music_restart, 1);
}

int main(void)
{
    test_music_index_resolves_to_track_seventeen();
    test_first_step_starts_music();
    test_cursor_starts_on_part_one();
    test_every_direction_toggles();
    test_held_direction_moves_once();
    test_move_and_confirm_on_one_frame();
    test_part_one_starts_when_available();
    test_part_one_inert_when_unavailable();
    test_part_two_starts_the_game();
    test_part_two_inert_when_unavailable();
    test_music_fades_and_loops();
    test_input_does_not_hold_back_the_loop();

    if (g_fail != 0) {
        printf("%d bootmenu check(s) failed\n", g_fail);
        return 1;
    }

    printf("bootmenu: all checks passed\n");
    return 0;
}
