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

static MenuAction feed_action(MenuState *st, int up, int down, int left,
                              int right, int cancel, int confirm, int pause)
{
    MenuInput in = none();

    in.up = up;
    in.down = down;
    in.left = left;
    in.right = right;
    in.cancel = cancel;
    in.confirm = confirm;
    in.pause = pause;
    return menu_state_step(st, &in);
}

static void feed(MenuState *st, int up, int down, int left, int right,
                 int cancel, int confirm, int pause)
{
    MenuInput in = none();

    in.up = up;
    in.down = down;
    in.left = left;
    in.right = right;
    in.cancel = cancel;
    in.confirm = confirm;
    in.pause = pause;
    menu_state_step(st, &in);
}

static void test_title_cursor_and_start(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    expect_int("title starts on start game", st.cursor,
               MENU_TITLE_ROW_START);

    in = none();
    in.down = 1;
    expect_int("down moves off start game",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("cursor is now options, load being hidden with no save",
               menu_state_title_row_at(&st, st.cursor),
               MENU_TITLE_ROW_OPTIONS);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    expect_int("and down again wraps, because there are only two",
               st.cursor, MENU_TITLE_ROW_START);

    in = none();
    in.confirm = 1;
    expect_int("confirm on the start row starts the game",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_START_GAME);
}

static void test_the_title_card_loads_when_there_is_a_save(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    menu_state_enter_title(&st);

    expect_int("a save makes the card three rows",
               menu_state_title_rows(&st), MENU_TITLE_ROWS_FULL);
    expect_int("start game first", menu_state_title_row_at(&st, 0),
               MENU_TITLE_ROW_START);
    expect_int("load game second", menu_state_title_row_at(&st, 1),
               MENU_TITLE_ROW_LOAD);
    expect_int("options third", menu_state_title_row_at(&st, 2),
               MENU_TITLE_ROW_OPTIONS);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    expect_int("the row under start game loads",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_LOAD_GAME);

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    expect_int("and with no save the card is two rows again",
               menu_state_title_rows(&st), MENU_TITLE_ROWS_FULL - 1);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    expect_int("whose second row is options",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("so there is nothing to load from the card itself",
               (int)st.screen, (int)MENU_OPTIONS);
}

static void test_the_pause_load_row_asks_first(void)
{
    MenuState st;
    int i;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    st.save.state = SLOT_OK;
    menu_state_enter_pause(&st);

    for (i = 0; i < MENU_PAUSE_ROW_LOAD; i++) {
        feed(&st, 0, 1, 0, 0, 0, 0, 0);
    }
    expect_int("the cursor is on the load row",
               menu_state_pause_row_at(&st, st.cursor), MENU_PAUSE_ROW_LOAD);
    expect_int("confirming it raises the prompt rather than loading",
               (int)feed_action(&st, 0, 0, 0, 0, 0, 1, 0), (int)MENU_ACT_NONE);
    expect_int("on the confirm screen", (int)st.screen, (int)MENU_CONFIRM);
    expect_int("with yes under the cursor", st.confirmYes, 1);
    expect_int("and the next confirm loads",
               (int)feed_action(&st, 0, 0, 0, 0, 0, 1, 0),
               (int)MENU_ACT_LOAD_GAME);
    expect_int("landing back on the pause menu, which draws a status line",
               (int)st.screen, (int)MENU_PAUSE);

    menu_state_enter_pause(&st);
    for (i = 0; i < MENU_PAUSE_ROW_SAVE; i++) {
        feed(&st, 0, 1, 0, 0, 0, 0, 0);
    }
    feed(&st, 0, 0, 0, 0, 0, 1, 0);
    expect_int("an overwrite opens on yes", st.confirmYes, 1);
}

static void test_the_load_row_appears_only_with_a_save(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    menu_state_enter_options(&st, MENU_TITLE);
    expect_int("the options screen is three rows whatever backup RAM holds",
               menu_state_options_rows(&st), MENU_OPTIONS_ROWS_FULL);
    expect_int("and the first of them is the level select",
               menu_state_options_row_at(&st, 0), MENU_OPTIONS_ROW_LEVELS);

    st.hasSave = 1;
    expect_int("a save does not add a row to it",
               menu_state_options_rows(&st), MENU_OPTIONS_ROWS_FULL);
    expect_int("the level select is still first",
               menu_state_options_row_at(&st, 0), MENU_OPTIONS_ROW_LEVELS);
    expect_int("and no position on it reaches a save",
               (int)feed_action(&st, 0, 0, 0, 0, 0, 1, 0),
               (int)MENU_ACT_NONE);

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);
    expect_int("the pause menu drops its load row the same way",
               menu_state_pause_rows(&st), MENU_PAUSE_ROWS_FULL - 1);
    expect_int("row 2 is controls with load gone",
               menu_state_pause_row_at(&st, 2), MENU_PAUSE_ROW_CONTROLS);

    memset(&st, 0, sizeof(st));
    menu_state_enter_death(&st);
    expect_int("and so does the death screen",
               menu_state_death_rows(&st), MENU_DEATH_ROWS_FULL - 1);
    expect_int("row 2 is save and quit with load gone",
               menu_state_death_row_at(&st, 2), MENU_DEATH_ROW_SAVE_QUIT);
    expect_int("and row 3 is return to title",
               menu_state_death_row_at(&st, 3), MENU_DEATH_ROW_TITLE);
}

static void test_saving_into_an_empty_device_does_not_ask(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);
    st.cursor = 1;

    expect_int("row 1 is save with no save to load",
               menu_state_pause_row_at(&st, 1), MENU_PAUSE_ROW_SAVE);
    expect_int("and there being nothing to overwrite, it just saves",
               (int)feed_action(&st, 0, 0, 0, 0, 0, 1, 0),
               (int)MENU_ACT_SAVE_GAME);
}

static void test_overwrite_asks_and_defaults_to_yes(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    menu_state_enter_pause(&st);
    st.cursor = MENU_PAUSE_ROW_SAVE;

    in = none();
    in.confirm = 1;
    expect_int("an existing save asks first",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("the confirm screen is up", (int)st.screen, (int)MENU_CONFIRM);
    expect_int("it defaults to yes", st.confirmYes, 1);

    in = none();
    in.confirm = 1;
    expect_int("confirming the default saves",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_GAME);
    expect_int("and lands back on the pause menu",
               (int)st.screen, (int)MENU_PAUSE);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    expect_int("right flips to no", st.confirmYes, 0);

    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    expect_int("right again flips back to yes", st.confirmYes, 1);

    in = none();
    in.left = 1;
    menu_state_step(&st, &in);
    expect_int("left flips to no", st.confirmYes, 0);

    in = none();
    in.confirm = 1;
    expect_int("confirming no writes nothing",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("and backs out to the pause menu",
               (int)st.screen, (int)MENU_PAUSE);
}

static void test_a_confirmed_overwrite_lands_back_where_it_was_asked(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    menu_state_enter_pause(&st);
    st.cursor = MENU_PAUSE_ROW_SAVE;

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    expect_int("confirming an overwrite saves",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_GAME);
    expect_int("and lands on the screen that asked, which draws a status line",
               (int)st.screen, (int)MENU_PAUSE);

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    menu_state_enter_pause(&st);
    st.cursor = MENU_PAUSE_ROW_TITLE;
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
    expect_int("and still lands on the title card",
               (int)st.screen, (int)MENU_TITLE);
}

static void test_pause_resume_and_return_to_title(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
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
    st.hasSave = 1;
    menu_state_enter_pause(&st);
    st.cursor = MENU_PAUSE_ROW_TITLE;
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
    st->hasSave = 1;
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
    st.hasSave = 1;
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
    st.hasSave = 1;
    menu_state_enter_pause(&st);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from row 0 wraps to row 4", st.cursor, 4);
}

static void test_title_has_an_options_row(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from the start row wraps to options",
               menu_state_title_row_at(&st, st.cursor),
               MENU_TITLE_ROW_OPTIONS);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("which opens options", (int)st.screen, (int)MENU_OPTIONS);

    in = none();
    in.cancel = 1;
    menu_state_step(&st, &in);
    expect_int("and cancels back to the card", (int)st.screen,
               (int)MENU_TITLE);
}

static void test_the_card_clamps_a_cursor_handed_back_from_a_wider_screen(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    st.cursor = 7;

    in = none();
    expect_int("a step on the card cannot act on a row it does not have",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("and the cursor is back inside it", st.cursor,
               menu_state_title_rows(&st) - 1);
}

static void test_options_rows(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_options(&st, MENU_TITLE);
    expect_int("with no save, options opens on the level select row",
               menu_state_options_row_at(&st, st.cursor),
               MENU_OPTIONS_ROW_LEVELS);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("which opens the level select", (int)st.screen,
               (int)MENU_LEVEL_SELECT);
    expect_int("and cancels back to the options screen",
               (int)st.returnScreen, (int)MENU_OPTIONS);

    memset(&st, 0, sizeof(st));
    menu_state_enter_options(&st, MENU_TITLE);
    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("the row under it still opens controls", (int)st.screen,
               (int)MENU_CONTROLS);

    memset(&st, 0, sizeof(st));
    menu_state_enter_options(&st, MENU_TITLE);
    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from the first row wraps to back",
               menu_state_options_row_at(&st, st.cursor),
               MENU_OPTIONS_ROW_BACK);
    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("which leaves", (int)st.screen, (int)MENU_TITLE);
}

static void test_back_still_closes_options_after_visiting_a_sub_screen(void)
{
    MenuState st;
    MenuInput in;
    int i;

    memset(&st, 0, sizeof(st));
    menu_state_enter_options(&st, MENU_TITLE);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("controls opened", (int)st.screen, (int)MENU_CONTROLS);
    in = none();
    in.cancel = 1;
    menu_state_step(&st, &in);
    expect_int("and left again", (int)st.screen, (int)MENU_OPTIONS);

    for (i = 0; i < MENU_OPTIONS_ROWS_FULL; i++) {
        if (menu_state_options_row_at(&st, st.cursor)
            == MENU_OPTIONS_ROW_BACK) {
            break;
        }
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("back closes the screen rather than reopening it",
               (int)st.screen, (int)MENU_TITLE);

    memset(&st, 0, sizeof(st));
    menu_state_enter_options(&st, MENU_TITLE);
    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("the level select opened", (int)st.screen,
               (int)MENU_LEVEL_SELECT);
    in = none();
    in.cancel = 1;
    menu_state_step(&st, &in);
    expect_int("and left again", (int)st.screen, (int)MENU_OPTIONS);

    in = none();
    in.cancel = 1;
    menu_state_step(&st, &in);
    expect_int("cancel closes the screen too", (int)st.screen,
               (int)MENU_TITLE);
}

static void test_the_level_select_always_offers_a_new_game(void)
{
    MenuState st;
    unsigned char list[CHECKPOINT_COUNT];

    memset(&st, 0, sizeof(st));
    menu_state_enter_levels(&st, MENU_OPTIONS);
    expect_int("a fresh machine sees one word",
               menu_state_levels(&st, list, CHECKPOINT_COUNT), 1);
    expect_int("and it is the first checkpoint", (int)list[0], 0);
}

static void test_the_level_select_shows_what_the_mask_holds(void)
{
    MenuState st;
    unsigned char list[CHECKPOINT_COUNT];

    memset(&st, 0, sizeof(st));
    st.reached = (1UL << 3) | (1UL << 7);
    menu_state_enter_levels(&st, MENU_OPTIONS);
    expect_int("two marks and the new game make three",
               menu_state_levels(&st, list, CHECKPOINT_COUNT), 3);
    expect_int("in table order, first", (int)list[0], 0);
    expect_int("second", (int)list[1], 3);
    expect_int("third", (int)list[2], 7);
}

static void test_an_unlocked_session_shows_every_word(void)
{
    MenuState st;
    unsigned char list[CHECKPOINT_COUNT];

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    menu_state_enter_levels(&st, MENU_OPTIONS);
    expect_int("the mask stops mattering",
               menu_state_levels(&st, list, CHECKPOINT_COUNT),
               CHECKPOINT_COUNT);
}

static void test_level_left_and_right_walk_reading_order(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    menu_state_enter_levels(&st, MENU_OPTIONS);

    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    expect_int("right moves one word", st.levelCursor, 1);

    in = none();
    in.left = 1;
    menu_state_step(&st, &in);
    expect_int("left moves back", st.levelCursor, 0);

    in = none();
    in.left = 1;
    menu_state_step(&st, &in);
    expect_int("and wraps to the last word", st.levelCursor,
               CHECKPOINT_COUNT - 1);
}

static void test_level_up_and_down_move_a_room_and_clamp(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    menu_state_enter_levels(&st, MENU_OPTIONS);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    expect_int("down leaves room 1 for room 2",
               checkpoint_room((int)st.levelCursor), 2);
    expect_int("landing on that room's first word", st.levelCursor, 2);

    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    expect_int("right twice reaches its third word", st.levelCursor, 4);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up clamps to room 1's last word", st.levelCursor, 1);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("and up from the first row wraps to the last", st.levelCursor,
               CHECKPOINT_COUNT - 1);
}

static void test_level_confirm_resolves_a_checkpoint(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    st.reached = 1UL << 9;
    menu_state_enter_levels(&st, MENU_OPTIONS);

    in = none();
    in.right = 1;
    menu_state_step(&st, &in);

    in = none();
    in.confirm = 1;
    expect_int("confirm asks for the jump",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_START_CHECKPOINT);
    expect_int("naming the table index, not the row", st.checkpoint, 9);
}

static void test_level_cancel_returns_where_it_came_from(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_levels(&st, MENU_TITLE);
    in = none();
    in.cancel = 1;
    menu_state_step(&st, &in);
    expect_int("cancel leaves for the card", (int)st.screen, (int)MENU_TITLE);
}

static void konami_first_ten(MenuState *st)
{
    feed(st, 1, 0, 0, 0, 0, 0, 0);
    feed(st, 1, 0, 0, 0, 0, 0, 0);
    feed(st, 0, 1, 0, 0, 0, 0, 0);
    feed(st, 0, 1, 0, 0, 0, 0, 0);
    feed(st, 0, 0, 1, 0, 0, 0, 0);
    feed(st, 0, 0, 0, 1, 0, 0, 0);
    feed(st, 0, 0, 1, 0, 0, 0, 0);
    feed(st, 0, 0, 0, 1, 0, 0, 0);
    feed(st, 0, 0, 0, 0, 1, 0, 0);
    feed(st, 0, 0, 0, 0, 0, 1, 0);
}

static void test_konami_opens_the_level_select_unlocked(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    konami_first_ten(&st);
    expect_int("ten presses in and still on the card", (int)st.screen,
               (int)MENU_TITLE);

    feed(&st, 0, 0, 0, 0, 0, 0, 1);
    expect_int("start opens the level select", (int)st.screen,
               (int)MENU_LEVEL_SELECT);
    expect_int("with every word showing", st.unlocked, 1);
}

static void test_konami_swallows_the_confirm_but_not_the_next_one(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    konami_first_ten(&st);
    expect_int("the code's own confirm did not start a game", (int)st.screen,
               (int)MENU_TITLE);
    expect_int("and left the cursor where it was", st.cursor, 0);

    expect_int("a second confirm does start one",
               (int)feed_action(&st, 0, 0, 0, 0, 0, 1, 0),
               (int)MENU_ACT_START_GAME);
}

static void test_konami_survives_a_false_start(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);

    feed(&st, 1, 0, 0, 0, 0, 0, 0);
    konami_first_ten(&st);
    feed(&st, 0, 0, 0, 0, 0, 0, 1);
    expect_int("the extra press aged out of the window rather than costing "
               "the attempt", (int)st.screen, (int)MENU_LEVEL_SELECT);

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    feed(&st, 1, 0, 0, 0, 0, 0, 0);
    feed(&st, 1, 0, 0, 0, 0, 0, 0);
    feed(&st, 0, 0, 0, 1, 0, 0, 0);
    feed(&st, 0, 1, 0, 0, 0, 0, 0);
    feed(&st, 0, 1, 0, 0, 0, 0, 0);
    feed(&st, 0, 0, 1, 0, 0, 0, 0);
    feed(&st, 0, 0, 0, 1, 0, 0, 0);
    feed(&st, 0, 0, 1, 0, 0, 0, 0);
    feed(&st, 0, 0, 0, 1, 0, 0, 0);
    feed(&st, 0, 0, 0, 0, 1, 0, 0);
    feed(&st, 0, 0, 0, 0, 0, 1, 0);
    feed(&st, 0, 0, 0, 0, 0, 0, 1);
    expect_int("and a wrong token inside the window does not open it",
               (int)st.screen, (int)MENU_TITLE);

    konami_first_ten(&st);
    feed(&st, 0, 0, 0, 0, 0, 0, 1);
    expect_int("but the code still works straight afterwards",
               (int)st.screen, (int)MENU_LEVEL_SELECT);
}

static void test_konami_refuses_two_buttons_at_once(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    feed(&st, 1, 1, 0, 0, 0, 0, 0);
    konami_first_ten(&st);
    feed(&st, 0, 0, 0, 0, 0, 0, 1);
    expect_int("up and down together is not a press the code accepts, and "
               "ages out like any other wrong one",
               (int)st.screen, (int)MENU_LEVEL_SELECT);

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    feed(&st, 1, 0, 0, 0, 0, 0, 0);
    feed(&st, 1, 1, 0, 0, 0, 0, 0);
    feed(&st, 0, 1, 0, 0, 0, 0, 0);
    feed(&st, 0, 1, 0, 0, 0, 0, 0);
    feed(&st, 0, 0, 1, 0, 0, 0, 0);
    feed(&st, 0, 0, 0, 1, 0, 0, 0);
    feed(&st, 0, 0, 1, 0, 0, 0, 0);
    feed(&st, 0, 0, 0, 1, 0, 0, 0);
    feed(&st, 0, 0, 0, 0, 1, 0, 0);
    feed(&st, 0, 0, 0, 0, 0, 1, 0);
    feed(&st, 0, 0, 0, 0, 0, 0, 1);
    expect_int("and inside the window it is a wrong press, not a guess at "
               "which button was meant", (int)st.screen, (int)MENU_TITLE);
}

static void test_konami_finishes_on_c_as_readily_as_on_a(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    konami_first_ten(&st);
    feed(&st, 0, 0, 0, 0, 0, 0, 1);
    expect_int("because the caller folds A and C into one confirm bit",
               (int)st.screen, (int)MENU_LEVEL_SELECT);
}

static void test_the_unlock_outlasts_the_screen(void)
{
    MenuState st;
    unsigned char list[CHECKPOINT_COUNT];

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    konami_first_ten(&st);
    feed(&st, 0, 0, 0, 0, 0, 0, 1);

    feed(&st, 0, 0, 0, 0, 1, 0, 0);
    expect_int("cancel goes back to the card", (int)st.screen,
               (int)MENU_TITLE);
    expect_int("and the session is still unlocked", st.unlocked, 1);

    menu_state_enter_options(&st, MENU_TITLE);
    feed(&st, 0, 0, 0, 0, 0, 1, 0);
    expect_int("reopening from the options row lands on the level select",
               (int)st.screen, (int)MENU_LEVEL_SELECT);
    expect_int("still showing every word",
               menu_state_levels(&st, list, CHECKPOINT_COUNT),
               CHECKPOINT_COUNT);
}

static void test_an_unlocked_session_hides_the_pause_save_row(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    st.hasSave = 1;
    menu_state_enter_pause(&st);

    expect_int("four rows, not five", menu_state_pause_rows(&st),
               MENU_PAUSE_ROWS_FULL - 1);
    expect_int("row 0 is still resume", menu_state_pause_row_at(&st, 0),
               MENU_PAUSE_ROW_RESUME);
    expect_int("row 1 is now load", menu_state_pause_row_at(&st, 1),
               MENU_PAUSE_ROW_LOAD);
    expect_int("row 3 is still return to title",
               menu_state_pause_row_at(&st, 3), MENU_PAUSE_ROW_TITLE);

    expect_int("and confirming row 0 still resumes",
               (int)feed_action(&st, 0, 0, 0, 0, 0, 1, 0),
               (int)MENU_ACT_RESUME);
}

static void test_the_cheat_swaps_the_pause_load_row_for_the_level_select(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    menu_state_enter_pause(&st);

    expect_int("the row is drawn with no save to load, because what it now "
               "offers has nothing to do with one",
               menu_state_pause_row_at(&st, 1), MENU_PAUSE_ROW_LOAD);

    feed_action(&st, 0, 1, 0, 0, 0, 0, 0);
    expect_int("confirming it opens a screen rather than a prompt",
               (int)feed_action(&st, 0, 0, 0, 0, 0, 1, 0),
               (int)MENU_ACT_NONE);
    expect_int("the level select", (int)st.screen, (int)MENU_LEVEL_SELECT);
}

static void test_both_rows_can_go_at_once(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    menu_state_enter_pause(&st);

    expect_int("an unlocked session with no save is four pause rows -- save "
               "goes, and the load row stays because the cheat has made it the "
               "level select, which has nothing to do with whether a save "
               "exists", menu_state_pause_rows(&st), MENU_PAUSE_ROWS_FULL - 1);
    expect_int("resume", menu_state_pause_row_at(&st, 0),
               MENU_PAUSE_ROW_RESUME);
    expect_int("the level select", menu_state_pause_row_at(&st, 1),
               MENU_PAUSE_ROW_LOAD);
    expect_int("controls", menu_state_pause_row_at(&st, 2),
               MENU_PAUSE_ROW_CONTROLS);
    expect_int("return to title", menu_state_pause_row_at(&st, 3),
               MENU_PAUSE_ROW_TITLE);

    menu_state_enter_death(&st);
    expect_int("and three death rows -- an unlocked session loses both of its "
               "saving rows, and keeps the top one because the cheat has made "
               "it the level select, which does not need a save",
               menu_state_death_rows(&st), MENU_DEATH_ROWS_FULL - 2);
    expect_int("resume first, the cheat having put it there",
               menu_state_death_row_at(&st, 0), MENU_DEATH_ROW_RESUME);
    expect_int("then the level select", menu_state_death_row_at(&st, 1),
               MENU_DEATH_ROW_LOAD);
    expect_int("return to title", menu_state_death_row_at(&st, 2),
               MENU_DEATH_ROW_TITLE);
}

static void test_an_unlocked_session_hides_the_death_save_row(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    st.hasSave = 1;
    menu_state_enter_death(&st);

    expect_int("two rows fewer, because save and quit goes with save and "
               "resume", menu_state_death_rows(&st),
               MENU_DEATH_ROWS_FULL - 2);
    expect_int("row 0 is resume, the cheat having put it first",
               menu_state_death_row_at(&st, 0), MENU_DEATH_ROW_RESUME);
    expect_int("row 1 is the level select the cheat made of the load row",
               menu_state_death_row_at(&st, 1), MENU_DEATH_ROW_LOAD);
    expect_int("and row 2 is return to title",
               menu_state_death_row_at(&st, 2), MENU_DEATH_ROW_TITLE);

    expect_int("the top row resumes",
               (int)feed_action(&st, 0, 0, 0, 0, 0, 1, 0),
               (int)MENU_ACT_RESUME);
    feed_action(&st, 0, 1, 0, 0, 0, 0, 0);
    expect_int("and the second opens a screen",
               (int)feed_action(&st, 0, 0, 0, 0, 0, 1, 0),
               (int)MENU_ACT_NONE);
    expect_int("the level select, not a prompt", (int)st.screen,
               (int)MENU_LEVEL_SELECT);
}

static void test_a_locked_session_keeps_every_row(void)
{
    MenuState st;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    menu_state_enter_pause(&st);
    expect_int("five pause rows", menu_state_pause_rows(&st),
               MENU_PAUSE_ROWS_FULL);
    expect_int("row 1 is save", menu_state_pause_row_at(&st, 1),
               MENU_PAUSE_ROW_SAVE);

    menu_state_enter_death(&st);
    expect_int("and every death row", menu_state_death_rows(&st),
               MENU_DEATH_ROWS_FULL);
    expect_int("row 0 is load game", menu_state_death_row_at(&st, 0),
               MENU_DEATH_ROW_LOAD);
    expect_int("row 1 is resume", menu_state_death_row_at(&st, 1),
               MENU_DEATH_ROW_RESUME);
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
    expect_int("run is untouched", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_B);
    expect_int("the map is clean", st.mapDirty, 0);
}

static void test_capturing_another_rows_button_swaps(void)
{
    MenuState st;
    MenuInput in;

    enter_controls_from_pause(&st);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);

    in = none();
    in.captured = PAD_B;
    menu_state_step(&st, &in);
    expect_int("the capture ends", st.capturing, -1);
    expect_int("whip took B", (int)st.map.row[KEYMAP_ROW_WHIP], (int)PAD_B);
    expect_int("run took whip's old A",
               (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_A);
    expect_int("the map is dirty", st.mapDirty, 1);
}

static void test_capturing_the_current_button_is_a_no_op(void)
{
    MenuState st;
    MenuInput in;

    enter_controls_from_pause(&st);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);

    in = none();
    in.captured = PAD_B;
    menu_state_step(&st, &in);
    expect_int("the capture ends", st.capturing, -1);
    expect_int("run is still B", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_B);
    expect_int("capturing the row's own button does not dirty the map",
               st.mapDirty, 0);
}

static void test_controls_has_five_rows(void)
{
    MenuState st;
    MenuInput in;

    enter_controls_from_pause(&st);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from the first row wraps to back",
               st.cursor, MENU_CONTROLS_ROW_BACK);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    expect_int("and down comes back to the first binding", st.cursor, 0);
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
    expect_int("reset restores run to B", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_B);
    expect_int("reset stays on the screen", (int)st.screen, (int)MENU_CONTROLS);
}

static void test_reset_on_an_unmodified_map_does_not_dirty(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_controls_from_pause(&st);

    for (i = 0; i < MENU_CONTROLS_ROW_RESET; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("reset on an unmodified map does not dirty it", st.mapDirty, 0);
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

static void enter_death_with_save(MenuState *st, SlotState state)
{
    memset(st, 0, sizeof(*st));
    menu_state_enter_death(st);
    st->save.state = state;
    st->hasSave = state != SLOT_EMPTY;
}

static void test_death_opens_on_load(void)
{
    MenuState st;

    enter_death_with_save(&st, SLOT_OK);
    expect_int("a death opens the death screen", (int)st.screen, (int)MENU_DEATH);
    expect_int("on the load row", st.cursor, MENU_DEATH_ROW_LOAD);

    enter_death_with_save(&st, SLOT_EMPTY);
    expect_int("and on resume with no save to load",
               menu_state_death_row_at(&st, st.cursor), MENU_DEATH_ROW_RESUME);
}

static void test_death_cancel_and_pause_resume(void)
{
    MenuState st;
    MenuInput in;

    enter_death_with_save(&st, SLOT_OK);
    in = none();
    in.cancel = 1;
    expect_int("cancel resumes rather than returning to the title",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RESUME);

    enter_death_with_save(&st, SLOT_OK);
    in = none();
    in.pause = 1;
    expect_int("start resumes too",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RESUME);

    enter_death_with_save(&st, SLOT_OK);
    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    expect_int("confirming the resume row resumes",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RESUME);
}

static void test_death_save_row(void)
{
    MenuState st;
    MenuInput in;

    enter_death_with_save(&st, SLOT_OK);
    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    menu_state_step(&st, &in);
    expect_int("row 2 is save and resume", st.cursor, MENU_DEATH_ROW_SAVE);

    in = none();
    in.confirm = 1;
    expect_int("confirming it asks the caller to save and resume",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_AND_RESUME);
}

static void test_death_load_row(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_death_with_save(&st, SLOT_OK);

    for (i = 0; i < MENU_DEATH_ROW_LOAD; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    expect_int("the load row asks first",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("on the prompt", (int)st.screen, (int)MENU_CONFIRM);
    expect_int("with yes already under the cursor", st.confirmYes, 1);
    expect_int("so one more confirm loads",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_LOAD_GAME);
    expect_int("and lands back on the death screen, which draws a status line",
               (int)st.screen, (int)MENU_DEATH);

    enter_death_with_save(&st, SLOT_EMPTY);
    expect_int("an empty device has no load row at all",
               menu_state_death_rows(&st), MENU_DEATH_ROWS_FULL - 1);

    enter_death_with_save(&st, SLOT_DAMAGED);
    expect_int("a damaged save keeps its row",
               menu_state_death_rows(&st), MENU_DEATH_ROWS_FULL);

    for (i = 0; i < MENU_DEATH_ROW_LOAD; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("and still asks for the load, so the caller can say why not",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_LOAD_GAME);
}

static void test_the_cheat_swaps_the_death_load_row_for_the_level_select(void)
{
    MenuState st;
    MenuInput in;

    enter_death_with_save(&st, SLOT_OK);
    st.unlocked = 1;
    expect_int("the row sits under resume now",
               menu_state_death_row_at(&st, 1), MENU_DEATH_ROW_LOAD);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    expect_int("and opens a screen rather than a prompt",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("the level select", (int)st.screen, (int)MENU_LEVEL_SELECT);

    enter_death_with_save(&st, SLOT_EMPTY);
    st.unlocked = 1;
    expect_int("drawn with no save to load",
               menu_state_death_row_at(&st, 1), MENU_DEATH_ROW_LOAD);

    enter_death_with_save(&st, SLOT_EMPTY);
    expect_int("and gone again once the cheat is off",
               menu_state_death_rows(&st), MENU_DEATH_ROWS_FULL - 1);
}

static void test_death_save_and_quit_row(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_death_with_save(&st, SLOT_OK);

    for (i = 0; i < MENU_DEATH_ROW_SAVE_QUIT; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }
    expect_int("save and quit sits above return to title",
               menu_state_death_row_at(&st, st.cursor),
               MENU_DEATH_ROW_SAVE_QUIT);

    in = none();
    in.confirm = 1;
    expect_int("and asks the caller to write the save and then leave",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_AND_QUIT);

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    st.hasSave = 1;
    menu_state_enter_death(&st);

    for (i = 0; i < menu_state_death_rows(&st); i++) {
        int row = menu_state_death_row_at(&st, i);

        if (row == MENU_DEATH_ROW_SAVE_QUIT) {
            expect_int("save and quit is not on an unlocked death screen",
                       row, -1);
        }
    }
}

static void test_death_title_row_shows_the_title_card(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_death_with_save(&st, SLOT_OK);

    for (i = 0; i < MENU_DEATH_ROW_TITLE; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }
    expect_int("the last row is return to title",
               st.cursor, MENU_DEATH_ROW_TITLE);

    in = none();
    in.confirm = 1;
    expect_int("confirming it stays inside the menu",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("and lands on the sub-title card",
               (int)st.screen, (int)MENU_TITLE);
}

static void test_death_cursor_wraps(void)
{
    MenuState st;
    MenuInput in;

    enter_death_with_save(&st, SLOT_OK);
    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from load wraps to return to title",
               st.cursor, MENU_DEATH_ROW_TITLE);
}

int main(void)
{
    test_title_cursor_and_start();
    test_the_title_card_loads_when_there_is_a_save();
    test_the_pause_load_row_asks_first();
    test_the_load_row_appears_only_with_a_save();
    test_saving_into_an_empty_device_does_not_ask();
    test_overwrite_asks_and_defaults_to_yes();
    test_a_confirmed_overwrite_lands_back_where_it_was_asked();
    test_pause_resume_and_return_to_title();
    test_confirm_cancel_goes_back_where_it_came_from();
    test_pause_has_a_controls_row();
    test_pause_return_to_title_moved_to_row_four();
    test_pause_cursor_wraps_over_five_rows();
    test_title_has_an_options_row();
    test_the_card_clamps_a_cursor_handed_back_from_a_wider_screen();
    test_options_rows();
    test_back_still_closes_options_after_visiting_a_sub_screen();
    test_the_level_select_always_offers_a_new_game();
    test_the_level_select_shows_what_the_mask_holds();
    test_an_unlocked_session_shows_every_word();
    test_level_left_and_right_walk_reading_order();
    test_level_up_and_down_move_a_room_and_clamp();
    test_level_confirm_resolves_a_checkpoint();
    test_level_cancel_returns_where_it_came_from();
    test_konami_opens_the_level_select_unlocked();
    test_konami_swallows_the_confirm_but_not_the_next_one();
    test_konami_survives_a_false_start();
    test_konami_refuses_two_buttons_at_once();
    test_konami_finishes_on_c_as_readily_as_on_a();
    test_the_unlock_outlasts_the_screen();
    test_an_unlocked_session_hides_the_pause_save_row();
    test_the_cheat_swaps_the_pause_load_row_for_the_level_select();
    test_both_rows_can_go_at_once();
    test_an_unlocked_session_hides_the_death_save_row();
    test_the_cheat_swaps_the_death_load_row_for_the_level_select();
    test_a_locked_session_keeps_every_row();
    test_capture_binds_a_button();
    test_start_aborts_a_capture();
    test_capturing_another_rows_button_swaps();
    test_capturing_the_current_button_is_a_no_op();
    test_controls_has_five_rows();
    test_reset_row_restores_defaults();
    test_reset_on_an_unmodified_map_does_not_dirty();
    test_back_saves_only_when_the_map_changed();
    test_death_opens_on_load();
    test_death_cancel_and_pause_resume();
    test_death_save_row();
    test_death_load_row();
    test_death_save_and_quit_row();
    test_death_title_row_shows_the_title_card();
    test_death_cursor_wraps();

    if (g_fail != 0) {
        printf("menu_state: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("menu_state: all tests passed\n");
    return 0;
}
