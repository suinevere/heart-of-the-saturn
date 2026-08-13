/*----------------------
 | test_bootmenu.c
 | Description: Host unit tests for bootmenu.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically. discfmt.c is linked
 |   in only so the music index can be checked against the cue track it
 |   resolves to.
 | Author: suinevere
 | Dependencies: bootmenu.h, discfmt.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "bootmenu.h"
#include "discfmt.h"

static int g_fail = 0;

static const char *screen_name(boot_screen s)
{
    switch (s) {
    case BOOT_SCREEN_LEGAL:     return "LEGAL";
    case BOOT_SCREEN_VIRGIN:    return "VIRGIN";
    case BOOT_SCREEN_INTERPLAY: return "INTERPLAY";
    case BOOT_SCREEN_TITLE:     return "TITLE";
    case BOOT_SCREEN_MENU:      return "MENU";
    default:                    return "?";
    }
}

static void expect_screen(const char *what, boot_screen got, boot_screen want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %s\n  expected = %s\n",
               what, screen_name(got), screen_name(want));
    }
}

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void test_music_index_resolves_to_track_three(void)
{
    expect_int("BOOT_MUSIC_INDEX maps to cue track 3 (track03.wav, 2:46)",
               discfmt_cue_track_for_music(BOOT_MUSIC_INDEX), 3);
}

static void test_screen_boundaries(void)
{
    bootmenu_state st;
    boot_frame f;
    bootmenu_init(&st, 1000u);

    bootmenu_step(&st, 1000u, 0u, &f);
    expect_screen("t=0 is LEGAL", f.screen, BOOT_SCREEN_LEGAL);
    expect_int("the first step starts the music", f.music_restart, 1);

    bootmenu_step(&st, 1000u + 5099u, 0u, &f);
    expect_screen("t=5099 is still LEGAL", f.screen, BOOT_SCREEN_LEGAL);
    bootmenu_step(&st, 1000u + 5100u, 0u, &f);
    expect_screen("t=5100 is VIRGIN", f.screen, BOOT_SCREEN_VIRGIN);
    bootmenu_step(&st, 1000u + 10200u, 0u, &f);
    expect_screen("t=10200 is INTERPLAY", f.screen, BOOT_SCREEN_INTERPLAY);
    bootmenu_step(&st, 1000u + 15300u, 0u, &f);
    expect_screen("t=15300 is TITLE", f.screen, BOOT_SCREEN_TITLE);
    bootmenu_step(&st, 1000u + 20399u, 0u, &f);
    expect_screen("t=20399 is still TITLE", f.screen, BOOT_SCREEN_TITLE);
    bootmenu_step(&st, 1000u + 20400u, 0u, &f);
    expect_screen("t=20400 is MENU", f.screen, BOOT_SCREEN_MENU);

    expect_int("music is not restarted by reaching the menu", f.music_restart, 0);
    expect_int("volume is full through the opening", (int)f.music_volume,
               (int)BOOT_VOLUME_MAX);
}

static void test_skip_from_each_still(void)
{
    static const uint32_t AT[] = { 0u, 5200u, 10300u, 15400u };
    unsigned i;

    for (i = 0; i < sizeof(AT) / sizeof(AT[0]); i++) {
        bootmenu_state st;
        boot_frame f;
        bootmenu_init(&st, 0u);
        bootmenu_step(&st, 0u, 0u, &f);
        bootmenu_step(&st, AT[i], BOOT_KEY_A, &f);
        expect_screen("a button skips to the menu", f.screen, BOOT_SCREEN_MENU);
        expect_int("skipping does not restart the music", f.music_restart, 0);
        expect_int("skipping does not start the game", f.start_game, 0);
    }
}

/* Mirrors the edge computation boot_sequence() performs in main.c. bootmenu_step
   is handed edges rather than levels, so a test that passes 0 for a held button
   is asserting the caller's arithmetic by hand and proving nothing; this drives
   the same formula the caller uses so that "held" really means held. */
static uint32_t held_edges(uint32_t current, uint32_t *previous)
{
    uint32_t pressed = current & ~*previous;
    *previous = current;
    return pressed;
}

static void test_held_button_skips_once(void)
{
    bootmenu_state st;
    boot_frame f;
    uint32_t previous = 0u;
    int frame;

    bootmenu_init(&st, 0u);
    bootmenu_step(&st, 0u, held_edges(0u, &previous), &f);

    bootmenu_step(&st, 100u, held_edges(BOOT_KEY_C, &previous), &f);
    expect_screen("the press edge skips", f.screen, BOOT_SCREEN_MENU);

    for (frame = 0; frame < 5; frame++)
    {
        bootmenu_step(&st, (uint32_t)(200 + frame * 100),
                      held_edges(BOOT_KEY_C, &previous), &f);
        expect_screen("holding it yields no further edge", f.screen, BOOT_SCREEN_MENU);
        expect_int("and never starts the game", f.start_game, 0);
    }
}

static void test_move_and_confirm_on_one_frame(void)
{
    bootmenu_state st;
    boot_frame f;

    bootmenu_init(&st, 0u);
    bootmenu_step(&st, 0u, 0u, &f);
    bootmenu_step(&st, BOOT_OPENING_MS, 0u, &f);

    bootmenu_step(&st, BOOT_OPENING_MS + 100u, BOOT_KEY_DOWN | BOOT_KEY_A, &f);
    expect_int("a same-frame move and confirm still moves the cursor",
               (int)f.highlight, (int)BOOT_ENTRY_HEART_OF_THE_ALIEN);
    expect_int("but confirms the entry that was lit before the move",
               f.start_game, 0);
}

static bootmenu_state g_menu_st;

static void at_menu(void)
{
    boot_frame f;
    bootmenu_init(&g_menu_st, 0u);
    bootmenu_step(&g_menu_st, 0u, 0u, &f);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS, 0u, &f);
}

static void test_cursor_starts_on_part_one(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS, 0u, &f);
    expect_int("cursor starts on OUT OF THIS WORLD",
               (int)f.highlight, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
}

static void test_cursor_toggles(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_DOWN, &f);
    expect_int("Down moves to HEART OF THE ALIEN",
               (int)f.highlight, (int)BOOT_ENTRY_HEART_OF_THE_ALIEN);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 200u, BOOT_KEY_UP, &f);
    expect_int("Up moves back to OUT OF THIS WORLD",
               (int)f.highlight, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
}

static void test_part_one_cannot_be_confirmed(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_A, &f);
    expect_int("confirming OUT OF THIS WORLD does not start the game",
               f.start_game, 0);
    expect_int("and leaves the cursor where it was",
               (int)f.highlight, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
    expect_screen("and stays on the menu", f.screen, BOOT_SCREEN_MENU);
}

static void test_part_two_starts_the_game(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_DOWN, &f);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 200u, BOOT_KEY_B, &f);
    expect_int("confirming HEART OF THE ALIEN starts the game", f.start_game, 1);
}

static void test_idle_timer_resets_on_ignored_input(void)
{
    boot_frame f;
    at_menu();
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18000u, BOOT_KEY_A, &f);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18000u + 18999u, 0u, &f);
    expect_screen("an ignored confirm still resets the idle timer",
                  f.screen, BOOT_SCREEN_MENU);
    expect_int("and no replay has happened", f.music_restart, 0);
}

static void test_fade_and_attract(void)
{
    boot_frame f;
    at_menu();

    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18000u, 0u, &f);
    expect_int("volume is full on the last frame before the fade window opens",
               (int)f.music_volume, (int)BOOT_VOLUME_MAX);

    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18500u, 0u, &f);
    if (f.music_volume == 0u || f.music_volume >= BOOT_VOLUME_MAX) {
        g_fail++;
        printf("FAIL mid-fade volume is between 0 and 7\n  actual = %d\n",
               (int)f.music_volume);
    }

    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 18999u, 0u, &f);
    expect_int("volume is silent as the replay arrives", (int)f.music_volume, 0);

    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 19000u, 0u, &f);
    expect_screen("the attract loop returns to LEGAL", f.screen, BOOT_SCREEN_LEGAL);
    expect_int("and restarts the music", f.music_restart, 1);
    expect_int("at full volume", (int)f.music_volume, (int)BOOT_VOLUME_MAX);
}

static void test_music_cap(void)
{
    boot_frame f;

    at_menu();

    bootmenu_step(&g_menu_st, BOOT_MUSIC_CAP_MS, BOOT_KEY_SELECT, &f);
    expect_int("the 40 s cap silences the track even though the idle timer just reset",
               (int)f.music_volume, 0);
    expect_screen("without ending the menu", f.screen, BOOT_SCREEN_MENU);
    expect_int("or starting the game", f.start_game, 0);
}

int main(void)
{
    test_music_index_resolves_to_track_three();
    test_screen_boundaries();
    test_skip_from_each_still();
    test_held_button_skips_once();
    test_move_and_confirm_on_one_frame();
    test_cursor_starts_on_part_one();
    test_cursor_toggles();
    test_part_one_cannot_be_confirmed();
    test_part_two_starts_the_game();
    test_idle_timer_resets_on_ignored_input();
    test_fade_and_attract();
    test_music_cap();

    if (g_fail != 0) {
        printf("%d bootmenu check(s) failed\n", g_fail);
        return 1;
    }

    printf("bootmenu: all checks passed\n");
    return 0;
}
