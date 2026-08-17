/*----------------------
 | test_menu_state.c
 | Description: Host unit tests for menu_state.c. Built and run by
 |   run_tests.sh with the host gcc, never by the Saturn makefile -- that
 |   globs src/ under saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: menu_state.h, stdio.h, string.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "menu_state.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static MenuInput none(void)
{
    MenuInput in;
    memset(&in, 0, sizeof(in));
    return in;
}

static void test_title_cursor_and_start(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    expect_int("title starts on start game", st.cursor, 0);

    in = none();
    in.down = 1;
    expect_int("down moves off start game",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("cursor is now load game", st.cursor, 1);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up moves back", st.cursor, 0);

    in = none();
    in.confirm = 1;
    expect_int("confirm on row 0 starts the game",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_START_GAME);
}

static void test_title_load_opens_slots(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    in = none();
    in.down = 1;
    menu_state_step(&st, &in);

    in = none();
    in.confirm = 1;
    expect_int("confirm on row 1 asks for a rescan",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RESCAN_SLOTS);
    expect_int("screen is the slot list", (int)st.screen, (int)MENU_SLOTS);
    expect_int("opened in load mode", st.saving, 0);
    expect_int("slot cursor reset", st.slotCursor, 0);
}

static void test_slots_cancel_returns_to_title(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    expect_int("entered the slot list", (int)st.screen, (int)MENU_SLOTS);

    in = none();
    in.cancel = 1;
    expect_int("cancel yields no action",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("cancel out of a gate-opened slot list lands on the sub-title menu",
               (int)st.screen, (int)MENU_TITLE);
}

static void test_slots_cursor_wraps(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from row 0 wraps to the last slot",
               st.slotCursor, SAVE_NUM_SLOTS - 1);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    expect_int("down from the last slot wraps to 0", st.slotCursor, 0);
}

static void test_device_toggle_needs_a_cart(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    st.device = SAT_BUP_INTERNAL;
    st.cartPresent = 0;

    in = none();
    in.right = 1;
    expect_int("no cart means no toggle",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("device unchanged", (int)st.device, SAT_BUP_INTERNAL);

    st.cartPresent = 1;
    in = none();
    in.right = 1;
    expect_int("a cart means a toggle and a rescan",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RESCAN_SLOTS);
    expect_int("device is the cart", (int)st.device, SAT_BUP_CART);
}

static void test_load_only_from_a_good_slot(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    st.slots[0].state = SLOT_DAMAGED;

    in = none();
    in.confirm = 1;
    expect_int("a damaged slot cannot be loaded",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);

    st.slots[0].state = SLOT_OK;
    in = none();
    in.confirm = 1;
    expect_int("a good slot loads",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_LOAD_SLOT);
}

static void test_save_over_an_empty_slot_does_not_ask(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 1, MENU_PAUSE);
    st.slots[0].state = SLOT_EMPTY;

    in = none();
    in.confirm = 1;
    expect_int("an empty slot saves straight away",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_SLOT);
}

static void test_overwrite_asks_and_defaults_to_no(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 1, MENU_PAUSE);
    st.slots[0].state = SLOT_OK;

    in = none();
    in.confirm = 1;
    expect_int("an occupied slot asks first",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("the confirm screen is up", (int)st.screen, (int)MENU_CONFIRM);
    expect_int("it defaults to no", st.confirmYes, 0);

    in = none();
    in.confirm = 1;
    expect_int("confirming the default does nothing",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("and backs out to the slot list",
               (int)st.screen, (int)MENU_SLOTS);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    expect_int("right flips to yes", st.confirmYes, 1);

    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    expect_int("right again flips back to no", st.confirmYes, 0);

    in = none();
    in.left = 1;
    menu_state_step(&st, &in);
    expect_int("left flips to yes", st.confirmYes, 1);

    in = none();
    in.confirm = 1;
    expect_int("confirming yes saves",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_SLOT);
}

static void test_a_confirmed_overwrite_lands_on_the_slot_list(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 1, MENU_PAUSE);
    st.slots[1].state = SLOT_OK;
    st.slotCursor = 1;

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    expect_int("confirming an overwrite saves",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_SLOT);
    expect_int("and stays on the slot list so the status line can be seen",
               (int)st.screen, (int)MENU_SLOTS);

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);
    st.cursor = 4;
    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    expect_int("a confirmed return to title still returns to title",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RETURN_TO_TITLE);
    expect_int("and still lands on the sub-title menu",
               (int)st.screen, (int)MENU_TITLE);
}

static void test_pause_resume_and_return_to_title(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);
    expect_int("pause starts on resume", st.cursor, 0);

    in = none();
    in.pause = 1;
    expect_int("the pause button resumes from any row",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RESUME);

    menu_state_enter_pause(&st);
    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from resume wraps to return to title", st.cursor, 4);

    in = none();
    in.confirm = 1;
    expect_int("return to title asks first",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("the confirm screen is up", (int)st.screen, (int)MENU_CONFIRM);

    in = none();
    in.left = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    expect_int("confirming yes returns to title",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RETURN_TO_TITLE);
    expect_int("and lands on the sub-title menu",
               (int)st.screen, (int)MENU_TITLE);
}

static void test_confirm_cancel_goes_back_where_it_came_from(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);
    st.cursor = 4;
    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);

    in = none();
    in.cancel = 1;
    menu_state_step(&st, &in);
    expect_int("cancelling a return-to-title prompt goes back to the pause menu",
               (int)st.screen, (int)MENU_PAUSE);
}

static void enter_controls_from_pause(MenuState *st)
{
    MenuInput in;
    int i;

    memset(st, 0, sizeof(*st));
    menu_state_enter_pause(st);

    for (i = 0; i < 3; i++) {
        in = none();
        in.down = 1;
        menu_state_step(st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(st, &in);
}

static void test_pause_has_a_controls_row(void)
{
    MenuState st;

    enter_controls_from_pause(&st);
    expect_int("pause row 3 opens controls", (int)st.screen, (int)MENU_CONTROLS);
    expect_int("controls opens on the first binding row", st.cursor, 0);
    expect_int("controls opens not capturing", st.capturing, -1);
    expect_int("controls remembers where it came from",
               (int)st.returnScreen, (int)MENU_PAUSE);
}

static void test_pause_return_to_title_moved_to_row_four(void)
{
    MenuState st;
    MenuInput in;
    int i;

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);

    for (i = 0; i < 4; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("pause row 4 is still return to title",
               (int)st.screen, (int)MENU_CONFIRM);
}

static void test_pause_cursor_wraps_over_five_rows(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from row 0 wraps to row 4", st.cursor, 4);
}

static void test_title_has_a_controls_row(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from row 0 wraps to row 2", st.cursor, 2);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("title row 2 opens controls", (int)st.screen, (int)MENU_CONTROLS);
    expect_int("and cancels back to the title",
               (int)st.returnScreen, (int)MENU_TITLE);
}

static void test_capture_binds_a_button(void)
{
    MenuState st;
    MenuInput in;

    enter_controls_from_pause(&st);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("confirm on a binding row starts a capture", st.capturing, 0);

    in = none();
    in.captured = PAD_X;
    menu_state_step(&st, &in);
    expect_int("the capture ends", st.capturing, -1);
    expect_int("run is now X", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_X);
    expect_int("the map is dirty", st.mapDirty, 1);
}

static void test_start_aborts_a_capture(void)
{
    MenuState st;
    MenuInput in;

    enter_controls_from_pause(&st);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);

    in = none();
    in.pause = 1;
    menu_state_step(&st, &in);
    expect_int("start ends the capture", st.capturing, -1);
    expect_int("run is untouched", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_A);
    expect_int("the map is clean", st.mapDirty, 0);
}

static void test_a_refused_capture_reports_in_use(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_controls_from_pause(&st);

    for (i = 0; i < KEYMAP_ROW_FORWARD; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);

    in = none();
    in.captured = PAD_A;
    menu_state_step(&st, &in);
    expect_int("the capture ends", st.capturing, -1);
    expect_int("the shortcut is still clear",
               (int)st.map.row[KEYMAP_ROW_FORWARD], (int)PAD_NONE);
    expect_int("run kept A", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_A);
    expect_int("the screen is told to say IN USE", st.mapRejected, 1);
    expect_int("the map is clean", st.mapDirty, 0);
}

static void test_left_clears_the_shortcut(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_controls_from_pause(&st);

    for (i = 0; i < KEYMAP_ROW_FORWARD; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.captured = PAD_Z;
    menu_state_step(&st, &in);
    expect_int("the shortcut is Z", (int)st.map.row[KEYMAP_ROW_FORWARD], (int)PAD_Z);

    in = none();
    in.left = 1;
    menu_state_step(&st, &in);
    expect_int("left clears the shortcut",
               (int)st.map.row[KEYMAP_ROW_FORWARD], (int)PAD_NONE);
}

static void test_reset_row_restores_defaults(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_controls_from_pause(&st);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.captured = PAD_X;
    menu_state_step(&st, &in);

    for (i = 0; i < MENU_CONTROLS_ROW_RESET; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("reset restores run to A", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_A);
    expect_int("reset stays on the screen", (int)st.screen, (int)MENU_CONTROLS);
}

static void test_back_saves_only_when_the_map_changed(void)
{
    MenuState st;
    MenuInput in;

    enter_controls_from_pause(&st);

    in = none();
    in.cancel = 1;
    expect_int("an unchanged map does not ask to be saved",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("cancel returns to pause", (int)st.screen, (int)MENU_PAUSE);

    enter_controls_from_pause(&st);
    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.captured = PAD_X;
    menu_state_step(&st, &in);

    in = none();
    in.cancel = 1;
    expect_int("a changed map asks to be saved",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_KEYMAP);
    expect_int("and returns to pause", (int)st.screen, (int)MENU_PAUSE);
}

int main(void)
{
    test_title_cursor_and_start();
    test_title_load_opens_slots();
    test_slots_cancel_returns_to_title();
    test_slots_cursor_wraps();
    test_device_toggle_needs_a_cart();
    test_load_only_from_a_good_slot();
    test_save_over_an_empty_slot_does_not_ask();
    test_overwrite_asks_and_defaults_to_no();
    test_a_confirmed_overwrite_lands_on_the_slot_list();
    test_pause_resume_and_return_to_title();
    test_confirm_cancel_goes_back_where_it_came_from();
    test_pause_has_a_controls_row();
    test_pause_return_to_title_moved_to_row_four();
    test_pause_cursor_wraps_over_five_rows();
    test_title_has_a_controls_row();
    test_capture_binds_a_button();
    test_start_aborts_a_capture();
    test_a_refused_capture_reports_in_use();
    test_left_clears_the_shortcut();
    test_reset_row_restores_defaults();
    test_back_saves_only_when_the_map_changed();

    if (g_fail != 0) {
        printf("menu_state: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("menu_state: all tests passed\n");
    return 0;
}
