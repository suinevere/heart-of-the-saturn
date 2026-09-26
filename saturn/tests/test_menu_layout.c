#include <stdio.h>
#include <string.h>
#include "menu_layout.h"
#include "savegame.h"
#include "checkpoints.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void expect_str(const char *what, const char *got, const char *want)
{
    if (got == 0 || strcmp(got, want) != 0) {
        g_fail++;
        printf("FAIL %s\n  actual   = %s\n  expected = %s\n",
               what, got ? got : "(null)", want);
    }
}

static int count_kind(const MenuItem *items, int n, unsigned char kind)
{
    int i, c = 0;
    for (i = 0; i < n; i++) {
        if (items[i].kind == kind) {
            c++;
        }
    }
    return c;
}

static int ramp_of(const MenuItem *items, int n, int x, int y)
{
    int i;
    for (i = 0; i < n; i++) {
        if (items[i].kind == MENU_ITEM_GLYPH && items[i].x == x
            && items[i].y == y) {
            return (int)items[i].ramp;
        }
    }
    return -1;
}

static int row_at(const MenuItem *items, int n, int y, int *id)
{
    int i;
    for (i = 0; i < n; i++) {
        if (items[i].kind == MENU_ITEM_TITLE_ROW && items[i].y == y) {
            if (id != 0) {
                *id = (int)items[i].id;
            }
            return (int)items[i].ramp;
        }
    }
    return -1;
}

static int has_text(const MenuItem *items, int n, int x, int y, const char *s)
{
    int i;
    int k;

    for (k = 0; s[k] != '\0'; k++) {
        int found = 0;

        if (s[k] == ' ') {
            continue;
        }
        for (i = 0; i < n; i++) {
            if (items[i].kind == MENU_ITEM_GLYPH
                && items[i].x == (short)(x + k * 8)
                && items[i].y == (short)y
                && items[i].id == (unsigned char)s[k]) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

#define GLYPH_CELL_W 8
#define GLYPH_CELL_H 10
#define GLYPH_INK_W  7
#define SCREEN_W     320
#define SCREEN_H     224
#define BOX_PAD_X    24
#define BOX_PAD_Y    12
#define ROW_DY       16
#define STATUS_DY    20

static int box_w(int cells) { return cells * GLYPH_CELL_W + 2 * BOX_PAD_X; }
static int box_h(int contentH) { return contentH + 2 * BOX_PAD_Y; }
static int content_left(int cells)
{
    return (SCREEN_W - box_w(cells)) / 2 + BOX_PAD_X;
}
static int content_top(int contentH)
{
    return (SCREEN_H - box_h(contentH)) / 2 + BOX_PAD_Y;
}
static int rows_h(int rows, int status)
{
    return (rows - 1) * ROW_DY + GLYPH_CELL_H + (status ? STATUS_DY : 0);
}
static int row_y(int rows, int status, int i)
{
    return content_top(rows_h(rows, status)) + i * ROW_DY;
}
static int centred_x(int cells, const char *s)
{
    return content_left(cells) + (cells * GLYPH_CELL_W
                                  - (int)strlen(s) * GLYPH_CELL_W) / 2;
}

#define LEVEL_NAME_CELLS 6
#define LEVEL_GAP        3
#define LEVEL_WORD_CELLS 4
#define LEVEL_WORD_GAP   2
#define LEVEL_HEADING    "LEVEL SELECT"

static int level_cells(int widestRow)
{
    int cells = LEVEL_NAME_CELLS + LEVEL_GAP + widestRow * LEVEL_WORD_CELLS
              + (widestRow - 1) * LEVEL_WORD_GAP;
    int heading = (int)strlen(LEVEL_HEADING);

    return cells > heading ? cells : heading;
}

static int level_word_x(int cells, int column)
{
    return content_left(cells) + (LEVEL_NAME_CELLS + LEVEL_GAP
        + column * (LEVEL_WORD_CELLS + LEVEL_WORD_GAP)) * GLYPH_CELL_W;
}

static int level_row_y(int rooms, int row)
{
    return content_top(rows_h(rooms + 1, 0)) + (row + 1) * ROW_DY;
}

#define CTRL_LABEL_CELLS 16
#define CTRL_GAP          2
#define CTRL_VALUE_CELLS  6

static int ctrl_cells(const char *foot)
{
    int cells = CTRL_LABEL_CELLS + CTRL_GAP + CTRL_VALUE_CELLS;
    int footCells = foot != 0 ? (int)strlen(foot) : 0;

    return footCells > cells ? footCells : cells;
}

static int ctrl_value_x(const char *foot, const char *value)
{
    int cells = ctrl_cells(foot);

    return content_left(cells) + (cells - (int)strlen(value)) * GLYPH_CELL_W;
}

static int ctrl_row_y(const char *foot, int row)
{
    return content_top(rows_h(MENU_CONTROLS_ROWS, foot != 0)) + row * ROW_DY;
}

static int ctrl_foot_y(const char *foot)
{
    return ctrl_row_y(foot, MENU_CONTROLS_ROWS - 1) + STATUS_DY;
}

static void test_no_spaces_are_emitted(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n, i;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    for (i = 0; i < n; i++) {
        if (items[i].kind == MENU_ITEM_GLYPH && items[i].id == ' ') {
            g_fail++;
            printf("FAIL a space was emitted as a glyph at index %d\n", i);
            return;
        }
    }
    expect_int("the title card has no box",
               count_kind(items, n, MENU_ITEM_RECT), 0);
    expect_int("the rows are whole-label textures, not glyphs",
               count_kind(items, n, MENU_ITEM_GLYPH), 0);
    expect_int("START GAME and OPTIONS are one row each",
               count_kind(items, n, MENU_ITEM_TITLE_ROW), 2);
}

static void test_title_selected_row_uses_the_bright_ramp(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    st.cursor = MENU_TITLE_ROW_START;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("START GAME is lit when the cursor is on it",
               row_at(items, n, 154, 0), MENU_RAMP_TITLE_SEL);
    expect_int("OPTIONS is dim",
               row_at(items, n, 176, 0), MENU_RAMP_TITLE_DIM);

    st.cursor = 1;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("START GAME is dim when the cursor moves off it",
               row_at(items, n, 154, 0), MENU_RAMP_TITLE_DIM);
    expect_int("OPTIONS is lit",
               row_at(items, n, 176, 0), MENU_RAMP_TITLE_SEL);
}

static void test_a_save_grows_the_card_around_its_centre(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    menu_state_enter_title(&st);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    {
        int id = -1;

        expect_int("START GAME rises half a row, to 143",
                   row_at(items, n, 143, &id), MENU_RAMP_TITLE_SEL);
        expect_int("and is label 0", id, MENU_TITLE_ROW_START);
        expect_int("LOAD GAME takes the midpoint the two rows straddle",
                   row_at(items, n, 165, &id), MENU_RAMP_TITLE_DIM);
        expect_int("and is label 1", id, MENU_TITLE_ROW_LOAD);
        expect_int("OPTIONS drops half a row, to 187",
                   row_at(items, n, 187, &id), MENU_RAMP_TITLE_DIM);
        expect_int("and is label 2", id, MENU_TITLE_ROW_OPTIONS);
        expect_int("and nothing is left on the two-row card's first line",
                   row_at(items, n, 154, 0), -1);
    }
}

static void test_only_the_title_card_uses_the_title_ramp(void)
{
    static const char *names[4] = { "the pause menu", "the options menu",
                                    "the confirm box", "the death screen" };
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n, i, j;

    for (j = 0; j < 4; j++) {
        memset(&st, 0, sizeof(st));
        st.hasSave = 1;
        st.save.state = SLOT_OK;
        st.save.roomId = 65535;

        if (j == 0) {
            menu_state_enter_pause(&st);
        } else if (j == 1) {
            menu_state_enter_options(&st, MENU_TITLE);
        } else if (j == 2) {
            menu_state_enter_pause(&st);
            st.screen = MENU_CONFIRM;
        } else {
            menu_state_enter_death(&st);
        }

        n = menu_layout_build(&st, "SAVE FAILED", items, MENU_LAYOUT_MAX_ITEMS);
        for (i = 0; i < n; i++) {
            if (items[i].ramp != MENU_RAMP_DIM
                && items[i].ramp != MENU_RAMP_SEL) {
                g_fail++;
                printf("FAIL %s drew item %d in ramp %d; only the title "
                       "card may leave the blue pair\n",
                       names[j], i, (int)items[i].ramp);
                break;
            }
        }
    }
}

static void test_a_box_is_two_rects_sized_to_its_rows(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;
    int five;
    int four;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    menu_state_enter_pause(&st);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    expect_int("a box is two rectangles, a border and the fill inside it",
               count_kind(items, n, MENU_ITEM_RECT), 2);
    expect_int("the border comes first", (int)items[0].id, MENU_RECT_BORDER);
    expect_int("the fill second", (int)items[1].id, MENU_RECT_FILL);
    expect_int("and the fill is inset by the border's thickness",
               (int)items[1].x - (int)items[0].x, MENU_BOX_BORDER);
    expect_int("on every side",
               (int)items[0].w - (int)items[1].w, 2 * MENU_BOX_BORDER);

    five = items[0].h;
    expect_int("five rows and the padding either side",
               five, box_h(rows_h(5, 0)));
    expect_int("the box is centred on the display",
               (int)items[0].y, (SCREEN_H - five) / 2);
    expect_int("and as wide as RETURN TO TITLE plus its padding",
               (int)items[0].w, box_w(15));

    st.unlocked = 1;
    st.hasSave = 0;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    four = items[0].h;
    expect_int("four rows are a four-row box",
               four, box_h(rows_h(4, 0)));
    expect_int("which is one row shorter, not a five-row box with a row of "
               "nothing in it", five - four, ROW_DY);
    expect_int("and it is still centred", (int)items[0].y,
               (SCREEN_H - four) / 2);
}

static void test_title_rows_are_centred(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    st.cursor = MENU_TITLE_ROW_START;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    {
        int i;

        for (i = 0; i < n; i++) {
            expect_int("a row starts at (320 - its width) / 2", (int)items[i].x,
                       (320 - MENU_TITLE_ROW_W) / 2);
            expect_int("and is as wide as its texture", (int)items[i].w,
                       MENU_TITLE_ROW_W);
            expect_int("and as tall", (int)items[i].h, MENU_TITLE_ROW_H);
        }
        expect_int("and there is nothing else on the card", n, 2);
    }
}

static void test_save_rows(void)
{
    char row[MENU_ROW_CHARS];
    SlotInfo info;

    memset(&info, 0, sizeof(info));
    info.state = SLOT_EMPTY;
    menu_layout_save_row(row, (int)sizeof(row), &info);
    expect_str("an empty device", row, "- EMPTY -");

    info.state = SLOT_DAMAGED;
    menu_layout_save_row(row, (int)sizeof(row), &info);
    expect_str("a damaged save", row, "- DAMAGED -");

    info.state = SLOT_OLD_VERSION;
    menu_layout_save_row(row, (int)sizeof(row), &info);
    expect_str("an old save", row, "- OLD SAVE -");

    info.state = SLOT_OK;
    info.roomId = 12;
    info.date = 0u;
    menu_layout_save_row(row, (int)sizeof(row), &info);
    expect_str("a good save at the BUP epoch", row, "ROOM 12  01/01 00:00");
}

static void test_a_full_row_fits_the_box(void)
{
    char row[MENU_ROW_CHARS];
    SlotInfo info;
    int len;
    int cells;
    int ink_end;
    int interior_end;

    memset(&info, 0, sizeof(info));
    info.state = SLOT_OK;
    info.roomId = 65535;
    info.date = 0xFFFFFFFFu;
    menu_layout_save_row(row, (int)sizeof(row), &info);
    len = (int)strlen(row);

    if (len + 2 > MENU_ROW_CHARS) {
        g_fail++;
        printf("FAIL the widest save row filled the %d-byte buffer at %d "
               "chars, so it may have been truncated\n", MENU_ROW_CHARS, len);
        return;
    }

    cells = len > 21 ? len : 21;
    ink_end = content_left(cells) + GLYPH_CELL_W * (len - 1) + GLYPH_INK_W;
    interior_end = SCREEN_W;

    if (box_w(cells) > SCREEN_W || ink_end > interior_end) {
        g_fail++;
        printf("FAIL the widest save row is %d chars, whose box is %d wide "
               "and whose ink ends at x=%d, off a %d-wide display\n",
               len, box_w(cells), ink_end, SCREEN_W);
    }
}

static void test_build_never_exceeds_cap(void)
{
    MenuState st;
    MenuItem items[8];
    int n;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    st.save.state = SLOT_OK;
    st.save.roomId = 99;
    menu_state_enter_death(&st);
    n = menu_layout_build(&st, "SAVE DATA DAMAGED", items, 8);
    if (n > 8) {
        g_fail++;
        printf("FAIL build returned %d items for a cap of 8\n", n);
    }
}

static void test_the_death_screen_fits_the_command_list(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    st.cartPresent = 1;
    st.device = SAT_BUP_CART;
    st.hasSave = 1;
    st.save.state = SLOT_OK;
    st.save.roomId = 255;
    st.save.date = 0xFFFFFFFFu;
    menu_state_enter_death(&st);

    n = menu_layout_build(&st, "CARTRIDGE WRITE PROTECTED", items,
                          MENU_LAYOUT_MAX_ITEMS);
    if (n >= MENU_LAYOUT_MAX_ITEMS) {
        g_fail++;
        printf("FAIL the widest death screen produced %d items, hitting the "
               "cap\n", n);
    }
    expect_int("the death screen draws one box",
               count_kind(items, n, MENU_ITEM_RECT), 2);
    expect_int("whose width comes from the status line, which is wider than "
               "any row on it", (int)items[0].w, box_w(25));
}

static void test_status_text(void)
{
    expect_int("success has nothing to report",
               menu_layout_status_text(SAT_BUP_OK, SAT_BUP_INTERNAL) == 0, 1);
    expect_str("an unformatted cart is worded for the cart",
               menu_layout_status_text(SAT_BUP_ERR_UNFORMAT, SAT_BUP_CART),
               "CARTRIDGE UNFORMATTED");
    expect_str("an unformatted internal is worded for internal",
               menu_layout_status_text(SAT_BUP_ERR_UNFORMAT, SAT_BUP_INTERNAL),
               "BACKUP RAM UNFORMATTED");
    expect_str("an oversized state is not a space problem",
               menu_layout_status_text(SAVE_ERR_TOO_LARGE, SAT_BUP_INTERNAL),
               "SAVE STATE TOO LARGE");
    expect_str("anything unrecognised still says something",
               menu_layout_status_text(9999, SAT_BUP_INTERNAL), "SAVE FAILED");
}

static void test_controls_rows_and_values(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_controls(&st, MENU_PAUSE);

    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("the screen fits the command list", n <= MENU_LAYOUT_MAX_ITEMS, 1);
    expect_int("five rows and no foot line, so five rows of box",
               (int)items[0].h, box_h(rows_h(MENU_CONTROLS_ROWS, 0)));
    expect_int("the run row is labelled at the content's left edge",
               has_text(items, n, content_left(ctrl_cells(0)),
                        ctrl_row_y(0, 0), "RUN/SHOOT/SHIELD"), 1);
    expect_int("the run row shows B",
               has_text(items, n, ctrl_value_x(0, "B"), ctrl_row_y(0, 0),
                        "B"), 1);
    expect_int("the whip row shows A",
               has_text(items, n, ctrl_value_x(0, "A"), ctrl_row_y(0, 1),
                        "A"), 1);
    expect_int("the jump row shows C",
               has_text(items, n, ctrl_value_x(0, "C"), ctrl_row_y(0, 2),
                        "C"), 1);
    expect_int("the reset row is there",
               has_text(items, n, content_left(ctrl_cells(0)),
                        ctrl_row_y(0, 3), "RESET DEFAULTS"), 1);
    expect_int("the back row is there",
               has_text(items, n, content_left(ctrl_cells(0)),
                        ctrl_row_y(0, 4), "BACK"), 1);
    expect_int("and there is no fourth binding row",
               has_text(items, n, ctrl_value_x(0, "NONE"), ctrl_row_y(0, 3),
                        "NONE"), 0);
}

static void test_controls_capture_prompt(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    const char *hint = "START CANCELS";
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_controls(&st, MENU_PAUSE);
    st.capturing = KEYMAP_ROW_JUMP;

    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("the captured row prompts",
               has_text(items, n, ctrl_value_x(hint, "PRESS?"),
                        ctrl_row_y(hint, 2), "PRESS?"), 1);
    expect_int("and the hint says how to escape",
               has_text(items, n, centred_x(ctrl_cells(hint), hint),
                        ctrl_foot_y(hint), hint), 1);
    expect_int("the hint costs the box a line of its own",
               (int)items[0].h, box_h(rows_h(MENU_CONTROLS_ROWS, 1)));
}

static void test_controls_status_replaces_hint(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    const char *status = "NO BACKUP DEVICE";
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_controls(&st, MENU_PAUSE);
    st.capturing = KEYMAP_ROW_JUMP;

    n = menu_layout_build(&st, status, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("a failed save's status renders on the foot line",
               has_text(items, n, centred_x(ctrl_cells(status), status),
                        ctrl_foot_y(status), status), 1);
    expect_int("the status replaces the capture hint rather than joining it",
               has_text(items, n, centred_x(ctrl_cells(status),
                                            "START CANCELS"),
                        ctrl_foot_y(status), "START CANCELS"), 0);
}

static void test_controls_every_button_name_renders(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    static const char *NAMES[] = { "A", "B", "C", "X", "Y", "Z", "L", "R" };
    int b;
    int n;

    for (b = PAD_A; b <= PAD_R; b++) {
        memset(&st, 0, sizeof(st));
        menu_state_enter_controls(&st, MENU_PAUSE);
        st.map.row[KEYMAP_ROW_JUMP] = (PadButton)b;

        n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
        expect_int("the jump row renders its button name",
                   has_text(items, n, ctrl_value_x(0, NAMES[b - PAD_A]),
                            ctrl_row_y(0, 2), NAMES[b - PAD_A]), 1);
    }
}

static void test_death_screen_rows(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;
    int cells = 13;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    st.save.state = SLOT_OK;
    st.save.roomId = 12;
    menu_state_enter_death(&st);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    expect_int("five rows, and no heading or device line above them",
               (int)items[0].h, box_h(rows_h(5, 0)));
    expect_int("nothing announces the death",
               has_text(items, n, centred_x(cells, "YOU DIED"),
                        row_y(5, 0, 0), "YOU DIED"), 0);
    expect_int("load game is the first row, named plainly rather than by what "
               "it holds",
               has_text(items, n, centred_x(cells, "LOAD GAME"),
                        row_y(5, 0, 0), "LOAD GAME"), 1);
    expect_int("resume the second, directly under it",
               has_text(items, n, centred_x(cells, "RESUME"),
                        row_y(5, 0, 1), "RESUME"), 1);
    expect_int("save and resume the third, ampersand as Another-Saturn spells "
               "it",
               has_text(items, n, centred_x(cells, "SAVE & RESUME"),
                        row_y(5, 0, 2), "SAVE & RESUME"), 1);
    expect_int("save and quit above quit",
               has_text(items, n, centred_x(cells, "SAVE & QUIT"),
                        row_y(5, 0, 3), "SAVE & QUIT"), 1);
    expect_int("and quit last, the word the other game's death screen uses "
               "for the same row",
               has_text(items, n, centred_x(cells, "QUIT"),
                        row_y(5, 0, 4), "QUIT"), 1);
}

static void test_the_load_prompt_is_three_lines_over_the_answers(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    char save[MENU_ROW_CHARS];
    int n;
    int cells = 21;

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    st.save.state = SLOT_OK;
    st.save.roomId = 12;
    menu_state_enter_death(&st);
    st.screen = MENU_CONFIRM;
    st.pending = MENU_ACT_LOAD_GAME;
    menu_layout_save_row(save, MENU_ROW_CHARS, &st.save);

    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    expect_int("four lines of box: the question, the save, the cost, the "
               "answers", (int)items[0].h, box_h(rows_h(4, 0)));
    expect_int("the question first",
               has_text(items, n, centred_x(cells, "LOAD GAME ?"),
                        row_y(4, 0, 0), "LOAD GAME ?"), 1);
    expect_int("the save it would jump to second",
               has_text(items, n, centred_x(cells, save), row_y(4, 0, 1),
                        save), 1);
    expect_int("what it costs third",
               has_text(items, n, centred_x(cells, "PROGRESS WILL BE LOST"),
                        row_y(4, 0, 2), "PROGRESS WILL BE LOST"), 1);

    st.pending = MENU_ACT_RETURN_TO_TITLE;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("a return to title is still three lines in total",
               (int)items[0].h, box_h(rows_h(3, 0)));

    st.pending = MENU_ACT_SAVE_GAME;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("and so is an overwrite",
               (int)items[0].h, box_h(rows_h(3, 0)));
}

static void test_death_screen_status(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;
    const char *worst = "CARTRIDGE WRITE PROTECTED";

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    menu_state_enter_death(&st);

    n = menu_layout_build(&st, worst, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("a failed save reports under the rows",
               has_text(items, n, centred_x(25, worst),
                        row_y(5, 1, 4) + STATUS_DY, worst), 1);
    expect_int("and it still fits the command list",
               n <= MENU_LAYOUT_MAX_ITEMS, 1);
}

#define LEVEL_NAME_X  44
#define LEVEL_WORD_X  116
#define LEVEL_WORD_DX 48
#define LEVEL_ROW0_Y  64
#define LEVEL_ROW_DY  16

static void test_options_rows_render(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;
    int cells = 12;

    memset(&st, 0, sizeof(st));
    menu_state_enter_options(&st, MENU_TITLE);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    expect_int("three rows and nothing else in the box",
               (int)items[0].h, box_h(rows_h(3, 0)));
    expect_int("level select first",
               has_text(items, n, centred_x(cells, "LEVEL SELECT"),
                        row_y(3, 0, 0), "LEVEL SELECT"), 1);
    expect_int("controls under it",
               has_text(items, n, centred_x(cells, "CONTROLS"),
                        row_y(3, 0, 1), "CONTROLS"), 1);
    expect_int("back under that",
               has_text(items, n, centred_x(cells, "BACK"),
                        row_y(3, 0, 2), "BACK"), 1);
    expect_int("the selected row is lit",
               ramp_of(items, n, centred_x(cells, "LEVEL SELECT"),
                       row_y(3, 0, 0)), MENU_RAMP_SEL);
    expect_int("the rest are dim",
               ramp_of(items, n, centred_x(cells, "BACK"), row_y(3, 0, 2)),
               MENU_RAMP_DIM);

    st.hasSave = 1;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("a save does not grow the screen -- the load row it used to add "
               "is on the title card now",
               (int)items[0].h, box_h(rows_h(3, 0)));
    expect_int("level select is still first",
               has_text(items, n, centred_x(cells, "LEVEL SELECT"),
                        row_y(3, 0, 0), "LEVEL SELECT"), 1);
    expect_int("and load game is nowhere on it",
               has_text(items, n, centred_x(cells, "LOAD GAME"),
                        row_y(3, 0, 0), "LOAD GAME"), 0);
}

static void test_a_status_line_widens_the_box_rather_than_lengthening_it(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;
    const char *worst = "CARTRIDGE WRITE PROTECTED";

    memset(&st, 0, sizeof(st));
    st.hasSave = 1;
    menu_state_enter_pause(&st);
    n = menu_layout_build(&st, worst, items, MENU_LAYOUT_MAX_ITEMS);

    expect_int("the longest message the wording can produce sets the width, "
               "because it is wider than any row",
               (int)items[0].w, box_w(25));
    expect_int("and the box is still on the display",
               (int)items[0].x >= 0 && (int)items[0].x + (int)items[0].w
                   <= SCREEN_W, 1);
    expect_int("it sits under the last row, a little further than a row step",
               has_text(items, n, centred_x(25, worst),
                        row_y(5, 1, 4) + STATUS_DY, worst), 1);
    expect_int("and the five rows are still five rows",
               has_text(items, n, centred_x(25, "RESUME"),
                        row_y(5, 1, 0), "RESUME"), 1);
}

static void test_a_fresh_level_select_shows_one_word(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;
    int cells = level_cells(1);

    memset(&st, 0, sizeof(st));
    menu_state_enter_levels(&st, MENU_OPTIONS);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    expect_int("one reached checkpoint is a one-row box, not a seven-row one",
               (int)items[0].h, box_h(rows_h(2, 0)));
    expect_int("headed LEVEL SELECT",
               has_text(items, n, centred_x(cells, LEVEL_HEADING),
                        content_top(rows_h(2, 0)), LEVEL_HEADING), 1);
    expect_int("with the room under it",
               has_text(items, n, content_left(cells), level_row_y(1, 0),
                        checkpoint_room_name(checkpoint_room(0))), 1);
    expect_int("and the new game word beside that",
               has_text(items, n, level_word_x(cells, 0), level_row_y(1, 0),
                        checkpoint_word(0)), 1);
    expect_int("selected",
               ramp_of(items, n, level_word_x(cells, 0), level_row_y(1, 0)),
               MENU_RAMP_SEL);
    expect_int("and no second word",
               has_text(items, n, level_word_x(cells, 1), level_row_y(1, 0),
                        checkpoint_word(1)), 0);
}

static void test_a_room_with_nothing_reached_is_not_drawn(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;
    int cells;
    int last = CHECKPOINT_COUNT - 1;

    memset(&st, 0, sizeof(st));
    st.reached = 1UL << last;
    menu_state_enter_levels(&st, MENU_OPTIONS);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    cells = level_cells(1);

    expect_int("two rooms showing is a two-row box",
               (int)items[0].h, box_h(rows_h(3, 0)));
    expect_int("the first is where a new game begins",
               has_text(items, n, content_left(cells), level_row_y(2, 0),
                        checkpoint_room_name(checkpoint_room(0))), 1);
    expect_int("the second is the one checkpoint the mask holds",
               has_text(items, n, content_left(cells), level_row_y(2, 1),
                        checkpoint_room_name(checkpoint_room(last))), 1);
    expect_int("and nothing sits on a third row",
               has_text(items, n, content_left(cells), level_row_y(2, 2),
                        checkpoint_room_name(checkpoint_room(0))), 0);
}

static void test_every_heading_has_a_word_under_it(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    unsigned char list[CHECKPOINT_COUNT];
    int n;
    int count;
    int rooms = 0;
    int widest = 0;
    int column = 0;
    int previous = -1;
    int cells;
    int i;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    menu_state_enter_levels(&st, MENU_OPTIONS);
    count = menu_state_levels(&st, list, CHECKPOINT_COUNT);

    for (i = 0; i < count; i++) {
        int room = checkpoint_room((int)list[i]);

        if (room != previous) {
            previous = room;
            rooms++;
            column = 0;
        }
        column++;
        if (column > widest) {
            widest = column;
        }
    }

    cells = level_cells(widest);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("every room showing is a box of that many rows plus a heading",
               (int)items[0].h, box_h(rows_h(rooms + 1, 0)));

    previous = -1;
    column = 0;

    for (i = 0; i < count; i++) {
        int room = checkpoint_room((int)list[i]);

        if (room != previous) {
            previous = room;
            expect_int("the heading is where the row is",
                       has_text(items, n, content_left(cells),
                                level_row_y(rooms, column),
                                checkpoint_room_name(room)), 1);
            column++;
        }
    }
    expect_int("seven rooms in all", column, 7);
    expect_int("and the box is on the display",
               box_h(rows_h(rooms + 1, 0)) <= SCREEN_H, 1);
}

static void test_the_widest_level_row_fits_the_box(void)
{
    int cells = level_cells(4);
    int ink_end = level_word_x(cells, 3)
                + (CHECKPOINT_WORD_LEN - 1) * GLYPH_CELL_W + GLYPH_INK_W;

    expect_int("a heading and four words fit inside their own box",
               ink_end <= content_left(cells) + cells * GLYPH_CELL_W, 1);
    expect_int("and that box fits the display", box_w(cells) <= SCREEN_W, 1);
}

static void test_the_level_select_fits_the_command_list(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    menu_state_enter_levels(&st, MENU_OPTIONS);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("the widest level select is under the cap",
               n < MENU_LAYOUT_MAX_ITEMS, 1);
}

static void test_a_checkpoint_save_row_shows_its_word(void)
{
    char row[MENU_ROW_CHARS];
    SlotInfo info;

    memset(&info, 0, sizeof(info));
    info.state = SLOT_OK;
    info.flags = SAVE_FLAG_CHECKPOINT;
    info.roomId = (unsigned short)checkpoint_room(5);
    info.entry = (unsigned char)checkpoint_entry(5);
    info.date = 0;
    menu_layout_save_row(row, (int)sizeof(row), &info);
    expect_int("the word is in the row",
               strstr(row, checkpoint_word(5)) != 0, 1);
    expect_int("and the room number is not", strstr(row, "ROOM") == 0, 1);
}

static void test_an_unknown_checkpoint_save_falls_back_to_the_room(void)
{
    char row[MENU_ROW_CHARS];
    SlotInfo info;

    memset(&info, 0, sizeof(info));
    info.state = SLOT_OK;
    info.flags = SAVE_FLAG_CHECKPOINT;
    info.roomId = 3;
    info.entry = 9;
    menu_layout_save_row(row, (int)sizeof(row), &info);
    expect_int("a pair no word reaches reads as a room",
               strstr(row, "ROOM 3") != 0, 1);
}

static void test_an_unlocked_pause_menu_closes_the_gap(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;
    int cells = 15;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    st.hasSave = 1;
    menu_state_enter_pause(&st);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    expect_int("save game is gone",
               has_text(items, n, centred_x(cells, "SAVE GAME"),
                        row_y(4, 0, 1), "SAVE GAME"), 0);
    expect_int("four rows, and the box lost a row with them",
               (int)items[0].h, box_h(rows_h(4, 0)));
    expect_int("resume first",
               has_text(items, n, centred_x(cells, "RESUME"),
                        row_y(4, 0, 0), "RESUME"), 1);
    expect_int("the level select takes the row under it, the cheat having "
               "swapped it for the load",
               has_text(items, n, centred_x(cells, "LEVEL SELECT"),
                        row_y(4, 0, 1), "LEVEL SELECT"), 1);
    expect_int("and load game is nowhere on it",
               has_text(items, n, centred_x(cells, "LOAD GAME"),
                        row_y(4, 0, 1), "LOAD GAME"), 0);
    expect_int("and return to title is last",
               has_text(items, n, centred_x(cells, "RETURN TO TITLE"),
                        row_y(4, 0, 3), "RETURN TO TITLE"), 1);
}

static void test_an_unlocked_death_screen_closes_the_gap(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;
    int cells = 12;

    memset(&st, 0, sizeof(st));
    st.unlocked = 1;
    st.hasSave = 1;
    st.save.state = SLOT_OK;
    st.save.roomId = 4;
    menu_state_enter_death(&st);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    expect_int("both saving rows are gone, so three rows are left",
               (int)items[0].h, box_h(rows_h(3, 0)));
    expect_int("resume first, the cheat having put it there",
               has_text(items, n, centred_x(cells, "RESUME"),
                        row_y(3, 0, 0), "RESUME"), 1);
    expect_int("the level select second, the cheat having swapped it for the "
               "load",
               has_text(items, n, centred_x(cells, "LEVEL SELECT"),
                        row_y(3, 0, 1), "LEVEL SELECT"), 1);
    expect_int("quit last",
               has_text(items, n, centred_x(cells, "QUIT"),
                        row_y(3, 0, 2), "QUIT"), 1);
    expect_int("and save and resume is nowhere on it",
               has_text(items, n, centred_x(cells, "SAVE & RESUME"),
                        row_y(3, 0, 1), "SAVE & RESUME"), 0);
}

static void test_a_message_is_one_centred_line_in_its_own_box(void)
{
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n = menu_layout_message("GAME SAVED", items, MENU_LAYOUT_MAX_ITEMS);

    expect_int("a border and a fill and nothing else",
               count_kind(items, n, MENU_ITEM_RECT), 2);
    expect_int("GAME SAVED is nine glyphs, the space not being one",
               count_kind(items, n, MENU_ITEM_GLYPH), 9);
    expect_int("the box is centred across the display",
               items[0].x * 2 + items[0].w, 320);
    expect_int("and down it",
               items[0].y * 2 + items[0].h, 224);
}

static void test_a_message_honours_its_cap(void)
{
    MenuItem items[4];
    int n = menu_layout_message("GAME SAVED", items, 4);

    expect_int("a cap that cannot hold the line stops at it", n, 4);
}

int main(void)
{
    test_no_spaces_are_emitted();
    test_a_message_is_one_centred_line_in_its_own_box();
    test_a_message_honours_its_cap();
    test_title_selected_row_uses_the_bright_ramp();
    test_a_save_grows_the_card_around_its_centre();
    test_title_rows_are_centred();
    test_only_the_title_card_uses_the_title_ramp();
    test_a_box_is_two_rects_sized_to_its_rows();
    test_save_rows();
    test_a_full_row_fits_the_box();
    test_build_never_exceeds_cap();
    test_the_death_screen_fits_the_command_list();
    test_status_text();
    test_controls_rows_and_values();
    test_controls_capture_prompt();
    test_controls_status_replaces_hint();
    test_controls_every_button_name_renders();
    test_death_screen_rows();
    test_the_load_prompt_is_three_lines_over_the_answers();
    test_death_screen_status();
    test_options_rows_render();
    test_a_status_line_widens_the_box_rather_than_lengthening_it();
    test_a_fresh_level_select_shows_one_word();
    test_a_room_with_nothing_reached_is_not_drawn();
    test_every_heading_has_a_word_under_it();
    test_the_widest_level_row_fits_the_box();
    test_the_level_select_fits_the_command_list();
    test_a_checkpoint_save_row_shows_its_word();
    test_an_unknown_checkpoint_save_falls_back_to_the_room();
    test_an_unlocked_pause_menu_closes_the_gap();
    test_an_unlocked_death_screen_closes_the_gap();

    if (g_fail != 0) {
        printf("menu_layout: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("menu_layout: all tests passed\n");
    return 0;
}
