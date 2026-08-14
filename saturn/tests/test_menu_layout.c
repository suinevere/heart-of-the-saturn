/*----------------------
 | test_menu_layout.c
 | Description: Host unit tests for menu_layout.c. Asserts what renders, where,
 |   and in which ramp -- the pixel packing itself lives in VDP1 and is not
 |   reachable from here, so the layout is what gets pinned.
 | Author: suinevere
 | Dependencies: menu_layout.h, stdio.h, string.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "menu_layout.h"
#include "savegame.h"

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
    expect_int("the sub-title menu has no panel",
               count_kind(items, n, MENU_ITEM_PANEL), 0);
    expect_int("START GAME and LOAD GAME are 18 non-space glyphs",
               count_kind(items, n, MENU_ITEM_GLYPH), 18);
}

static void test_title_selected_row_uses_the_bright_ramp(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    st.cursor = 0;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("START GAME is lit when the cursor is on it",
               ramp_of(items, n, 120, 161), MENU_RAMP_TITLE_SEL);
    expect_int("LOAD GAME is dim",
               ramp_of(items, n, 124, 177), MENU_RAMP_TITLE_DIM);

    st.cursor = 1;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("START GAME is dim when the cursor moves off it",
               ramp_of(items, n, 120, 161), MENU_RAMP_TITLE_DIM);
    expect_int("LOAD GAME is lit",
               ramp_of(items, n, 124, 177), MENU_RAMP_TITLE_SEL);
}

/*----------------------
 | test_only_the_title_card_uses_the_title_ramp
 | Description: The sub-title card is the one screen with no panel behind its
 |   text, and the only one that may draw in the red ramp. Every other screen
 |   keeps the blue one, so a screen added later cannot quietly inherit
 |   white-outlined red over a panel.
 ----------------------*/
static void test_only_the_title_card_uses_the_title_ramp(void)
{
    static const char *names[3] = { "the pause menu", "the slot list",
                                    "the confirm box" };
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n, i, j;

    for (j = 0; j < 3; j++) {
        memset(&st, 0, sizeof(st));
        if (j == 0) {
            menu_state_enter_pause(&st);
        } else {
            menu_state_enter_slots(&st, 0, MENU_TITLE);
            for (i = 0; i < SAVE_NUM_SLOTS; i++) {
                st.slots[i].state = SLOT_OK;
                st.slots[i].roomId = 65535;
            }
            if (j == 2) {
                st.screen = MENU_CONFIRM;
            }
        }

        n = menu_layout_build(&st, "SAVE FAILED", items, MENU_LAYOUT_MAX_ITEMS);
        for (i = 0; i < n; i++) {
            if (items[i].ramp != MENU_RAMP_DIM
                && items[i].ramp != MENU_RAMP_SEL) {
                g_fail++;
                printf("FAIL %s drew item %d in ramp %d; only the sub-title "
                       "card may leave the blue pair\n",
                       names[j], i, (int)items[i].ramp);
                break;
            }
        }
    }
}

/*----------------------
 | test_title_rows_are_centred
 | Description: Pins both rows' left edges to the centre of the 320-wide
 |   display and the cursor to a fixed gap left of the row it marks.
 ----------------------*/
static void test_title_rows_are_centred(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    st.cursor = 0;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("START GAME's ten cells start at (320 - 80) / 2",
               ramp_of(items, n, 120, 161) >= 0, 1);
    expect_int("LOAD GAME's nine cells start at (320 - 72) / 2",
               ramp_of(items, n, 124, 177) >= 0, 1);
    expect_int("the cursor sits two cells left of START GAME",
               ramp_of(items, n, 104, 161), MENU_RAMP_TITLE_SEL);

    st.cursor = 1;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("the cursor follows LOAD GAME's own left edge",
               ramp_of(items, n, 108, 177), MENU_RAMP_TITLE_SEL);
}

static void test_slot_rows(void)
{
    char row[MENU_ROW_CHARS];
    SlotInfo info;

    memset(&info, 0, sizeof(info));
    info.state = SLOT_EMPTY;
    menu_layout_slot_row(row, (int)sizeof(row), 0, &info);
    expect_str("an empty slot", row, "SLOT 1  - EMPTY -");

    info.state = SLOT_DAMAGED;
    menu_layout_slot_row(row, (int)sizeof(row), 1, &info);
    expect_str("a damaged slot", row, "SLOT 2  - DAMAGED -");

    info.state = SLOT_OLD_VERSION;
    menu_layout_slot_row(row, (int)sizeof(row), 2, &info);
    expect_str("an old save", row, "SLOT 3  - OLD SAVE -");

    info.state = SLOT_OK;
    info.roomId = 12;
    info.date = 0u;
    menu_layout_slot_row(row, (int)sizeof(row), 0, &info);
    expect_str("a good slot at the BUP epoch", row,
               "SLOT 1  ROOM 12  01/01 00:00");
}

/*----------------------
 | ROW_X / GLYPH_CELL_W / GLYPH_INK_W / PANEL_X / PANEL_W / PANEL_BORDER
 | Description: The geometry test_a_full_row_fits_the_panel measures against,
 |   copied from the two files that own it: ROW_X, PANEL_X and the 8 px
 |   advance are menu_layout.c's private MENU_SLOTS_* defines, while PANEL_W
 |   and PANEL_BORDER are MENUPANS.ART's width and PANEL_BORDER_PX in
 |   tools/mkmenuart.py. GLYPH_INK_W is how far into its cell a glyph paints:
 |   font8x8.py's widest entry is 5 px, mkmenuart.py offsets it one to the right
 |   to make room for the outline, and the outline adds one more on each side,
 |   so columns 0 through 6 can carry ink.
 | Author: suinevere
 ----------------------*/
#define ROW_X        60
#define GLYPH_CELL_W 8
#define GLYPH_INK_W  7
#define PANEL_X      24
#define PANEL_W      272
#define PANEL_BORDER 2

static void test_a_full_row_fits_the_panel(void)
{
    char row[MENU_ROW_CHARS];
    SlotInfo info;
    int len;
    int ink_end;
    int interior_end;

    memset(&info, 0, sizeof(info));
    info.state = SLOT_OK;
    info.roomId = 65535;
    info.date = 0xFFFFFFFFu;
    menu_layout_slot_row(row, (int)sizeof(row), 2, &info);
    len = (int)strlen(row);

    if (len + 2 > MENU_ROW_CHARS) {
        g_fail++;
        printf("FAIL the widest slot row filled the %d-byte buffer at %d "
               "chars, so it may have been truncated\n", MENU_ROW_CHARS, len);
        return;
    }

    ink_end = ROW_X + GLYPH_CELL_W * (len - 1) + GLYPH_INK_W;
    interior_end = PANEL_X + PANEL_W - PANEL_BORDER;

    if (ink_end > interior_end) {
        g_fail++;
        printf("FAIL the widest slot row is %d chars, whose ink ends at x=%d, "
               "past the slot panel's interior at x=%d\n",
               len, ink_end, interior_end);
    }
}

static void test_build_never_exceeds_cap(void)
{
    MenuState st;
    MenuItem items[8];
    int n, i;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    for (i = 0; i < SAVE_NUM_SLOTS; i++) {
        st.slots[i].state = SLOT_OK;
        st.slots[i].roomId = 99;
    }
    n = menu_layout_build(&st, "SAVE DATA DAMAGED", items, 8);
    if (n > 8) {
        g_fail++;
        printf("FAIL build returned %d items for a cap of 8\n", n);
    }
}

static void test_slot_list_fits_the_command_list(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n, i;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    st.cartPresent = 1;
    st.device = SAT_BUP_CART;
    for (i = 0; i < SAVE_NUM_SLOTS; i++) {
        st.slots[i].state = SLOT_OK;
        st.slots[i].roomId = 255;
        st.slots[i].date = 0xFFFFFFFFu;
    }
    n = menu_layout_build(&st, "CARTRIDGE WRITE PROTECTED", items,
                          MENU_LAYOUT_MAX_ITEMS);
    if (n >= MENU_LAYOUT_MAX_ITEMS) {
        g_fail++;
        printf("FAIL the widest slot list produced %d items, hitting the cap\n",
               n);
    }
    expect_int("the slot list draws one panel",
               count_kind(items, n, MENU_ITEM_PANEL), 1);
    expect_int("that panel is the slot panel", (int)items[0].id,
               MENU_PANEL_SLOTS);
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

int main(void)
{
    test_no_spaces_are_emitted();
    test_title_selected_row_uses_the_bright_ramp();
    test_title_rows_are_centred();
    test_only_the_title_card_uses_the_title_ramp();
    test_slot_rows();
    test_a_full_row_fits_the_panel();
    test_build_never_exceeds_cap();
    test_slot_list_fits_the_command_list();
    test_status_text();

    if (g_fail != 0) {
        printf("menu_layout: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("menu_layout: all tests passed\n");
    return 0;
}
