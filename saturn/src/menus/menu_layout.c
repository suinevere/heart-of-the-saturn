/*----------------------
 | menu_layout.c
 | Description: Builds the per-screen draw list, the slot-row text and the
 |   status wording. No snprintf anywhere in this file: sprintf would pull
 |   stdio into a translation unit that has no other need of it, which is the
 |   same reason Another-Saturn hand-rolls its appenders.
 | Author: suinevere
 | Dependencies: menu_layout.h, savegame.h
 ----------------------*/
#include "menu_layout.h"
#include "savegame.h"

/*----------------------
 | MENU_GLYPH_W
 | Description: Pixel advance per character, fixed-width for every glyph.
 | Author: suinevere
 ----------------------*/
#define MENU_GLYPH_W 8

/*----------------------
 | MENU_SCREEN_W
 | Description: The display this file lays out against, which is wider than the
 |   304 the engine rasterizes into -- see menu_layout.h. Named because the
 |   sub-title screen centres against it rather than placing text by hand.
 | Author: suinevere
 ----------------------*/
#define MENU_SCREEN_W 320

/*----------------------
 | Layout geometry
 | Description: Another-Saturn's panels re-centred for this port's 320x224
 |   frame rather than its 320x200 page. Do not copy its literals; they land
 |   12 rows high here.
 |
 |   The two sub-title rows sit below the title card's OUT OF THIS WORLD PART II
 |   subtitle, not beside it. They are the one pair here placed against the
 |   artwork rather than against a panel of their own, so the clearance is the
 |   backdrop's to give: at 162 the first row crowds that subtitle, and the eight
 |   pixels to 170 are what separate the port's text from the original's.
 | Author: suinevere
 ----------------------*/
#define MENU_TITLE_START_Y  161
#define MENU_TITLE_LOAD_Y   177
#define MENU_TITLE_CURSOR_DX 16

#define MENU_PAUSE_PANEL_X 76
#define MENU_PAUSE_PANEL_Y 64
#define MENU_PAUSE_TEXT_X  100
#define MENU_PAUSE_ROW0_Y  76
#define MENU_PAUSE_ROW_DY  16
#define MENU_PAUSE_CURSOR_X 84

/*----------------------
 | Slot screen geometry
 | Description: The slot list's left-aligned column, four pixels left of where
 |   it started.
 |
 |   The widest row is 29 characters and its last cell used to end exactly on
 |   the panel's two-pixel right border, which only worked because a glyph was
 |   five pixels of ink in an eight-pixel cell and the last three columns
 |   painted nothing. Outlining the font spends two of those three, so the row
 |   crossed the border by a pixel. Everything on this screen moves together
 |   rather than the rows alone, because the cursor, the status line and the
 |   footer are all flush with each other and a row that moved on its own would
 |   break that to fix a pixel.
 | Author: suinevere
 ----------------------*/
#define MENU_SLOTS_PANEL_X  24
#define MENU_SLOTS_PANEL_Y  28
#define MENU_SLOTS_HEAD_X   128
#define MENU_SLOTS_HEAD_Y   40
#define MENU_SLOTS_DEVICE_X 52
#define MENU_SLOTS_DEVICE_Y 60
#define MENU_SLOTS_ROW_X    60
#define MENU_SLOTS_ROW0_Y   84
#define MENU_SLOTS_ROW_DY   16
#define MENU_SLOTS_CURSOR_X 44
#define MENU_SLOTS_STATUS_X 44
#define MENU_SLOTS_STATUS_Y 148
#define MENU_SLOTS_FOOT_X   44
#define MENU_SLOTS_FOOT_Y   172

#define MENU_CONFIRM_PANEL_X 40
#define MENU_CONFIRM_PANEL_Y 76
#define MENU_CONFIRM_TEXT_X  64
#define MENU_CONFIRM_LINE0_Y 90
#define MENU_CONFIRM_LINE1_Y 106
#define MENU_CONFIRM_YES_X   136
#define MENU_CONFIRM_NO_X    184
#define MENU_CONFIRM_ANSWER_Y 128
#define MENU_CONFIRM_YES_CURSOR_X 120
#define MENU_CONFIRM_NO_CURSOR_X  168

/*----------------------
 | put_panel
 | Description: Appends a panel item, silently dropping it past cap.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- destination array; cap -- its capacity; n -- write cursor,
 |         updated; id -- MENU_PANEL_*; x, y -- top-left corner in pixels
 | Returns: N/A
 ----------------------*/
static void put_panel(MenuItem *out, int cap, int *n, int id, int x, int y)
{
    if (*n >= cap) {
        return;
    }
    out[*n].kind = MENU_ITEM_PANEL;
    out[*n].id = (unsigned char)id;
    out[*n].x = (short)x;
    out[*n].y = (short)y;
    out[*n].ramp = MENU_RAMP_DIM;
    (*n)++;
}

/*----------------------
 | put_text
 | Description: Appends one glyph item per non-space character. A space
 |   advances the cursor without emitting an item; that is not an
 |   optimisation for its own sake -- it is what keeps the widest screen
 |   under MENU_LAYOUT_MAX_ITEMS and therefore under VDP1's command list.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- destination array; cap -- its capacity; n -- write cursor,
 |         updated; x, y -- top-left corner of the first glyph; s -- text;
 |         ramp -- MENU_RAMP_*
 | Returns: N/A
 ----------------------*/
static void put_text(MenuItem *out, int cap, int *n, int x, int y,
                     const char *s, int ramp)
{
    while (*s != 0) {
        if (*n >= cap) {
            return;
        }
        if (*s != ' ' && (unsigned char)*s >= 0x20u
            && (unsigned char)*s <= 0x5Fu) {
            out[*n].kind = MENU_ITEM_GLYPH;
            out[*n].id = (unsigned char)*s;
            out[*n].x = (short)x;
            out[*n].y = (short)y;
            out[*n].ramp = (unsigned char)ramp;
            (*n)++;
        }
        x += MENU_GLYPH_W;
        s++;
    }
}

/*----------------------
 | append_char
 | Description: Appends one byte to a NUL-terminated buffer, dropping it
 |   silently once the buffer is full.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dst -- buffer; cap -- its capacity; pos -- write cursor, updated;
 |         c -- byte to append
 | Returns: N/A
 ----------------------*/
static void append_char(char *dst, int cap, int *pos, char c)
{
    if (*pos + 1 >= cap) {
        return;
    }
    dst[*pos] = c;
    (*pos)++;
    dst[*pos] = 0;
}

/*----------------------
 | append_str
 | Description: Appends a NUL-terminated string one byte at a time.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dst -- buffer; cap -- its capacity; pos -- write cursor, updated;
 |         s -- string to append
 | Returns: N/A
 ----------------------*/
static void append_str(char *dst, int cap, int *pos, const char *s)
{
    while (*s != 0) {
        append_char(dst, cap, pos, *s);
        s++;
    }
}

/*----------------------
 | append_pad2
 | Description: Appends a value as two zero-padded digits, clamped to
 |   [0, 99] so a garbage field cannot walk past two digits.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dst -- buffer; cap -- its capacity; pos -- write cursor, updated;
 |         v -- value to format
 | Returns: N/A
 ----------------------*/
static void append_pad2(char *dst, int cap, int *pos, int v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > 99) {
        v = 99;
    }
    append_char(dst, cap, pos, (char)('0' + (v / 10)));
    append_char(dst, cap, pos, (char)('0' + (v % 10)));
}

/*----------------------
 | append_room
 | Description: Appends a room id without leading zeros, because "ROOM 07"
 |   reads as a chapter number the game does not have.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: dst -- buffer; cap -- its capacity; pos -- write cursor, updated;
 |         room -- room id, clamped to [0, 999]
 | Returns: N/A
 ----------------------*/
static void append_room(char *dst, int cap, int *pos, unsigned short room)
{
    if (room > 999u) {
        room = 999u;
    }
    if (room >= 100u) {
        append_char(dst, cap, pos, (char)('0' + (room / 100u)));
    }
    if (room >= 10u) {
        append_char(dst, cap, pos, (char)('0' + ((room / 10u) % 10u)));
    }
    append_char(dst, cap, pos, (char)('0' + (room % 10u)));
}

/*----------------------
 | menu_layout_slot_row
 | Description: See menu_layout.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- destination; cap -- its capacity; slot -- 0-based index;
 |         info -- what savedata_probe found
 | Returns: N/A
 ----------------------*/
void menu_layout_slot_row(char *out, int cap, int slot, const SlotInfo *info)
{
    int pos = 0;
    int month = 0, day = 0, hour = 0, minute = 0;

    out[0] = 0;
    append_str(out, cap, &pos, "SLOT ");
    append_char(out, cap, &pos, (char)('1' + slot));
    append_str(out, cap, &pos, "  ");

    if (info->state == SLOT_EMPTY) {
        append_str(out, cap, &pos, "- EMPTY -");
        return;
    }
    if (info->state == SLOT_DAMAGED) {
        append_str(out, cap, &pos, "- DAMAGED -");
        return;
    }
    if (info->state == SLOT_OLD_VERSION) {
        append_str(out, cap, &pos, "- OLD SAVE -");
        return;
    }

    append_str(out, cap, &pos, "ROOM ");
    append_room(out, cap, &pos, info->roomId);
    append_str(out, cap, &pos, "  ");

    savedata_date_split(info->date, &month, &day, &hour, &minute);
    append_pad2(out, cap, &pos, month);
    append_char(out, cap, &pos, '/');
    append_pad2(out, cap, &pos, day);
    append_char(out, cap, &pos, ' ');
    append_pad2(out, cap, &pos, hour);
    append_char(out, cap, &pos, ':');
    append_pad2(out, cap, &pos, minute);
}

/*----------------------
 | menu_layout_status_text
 | Description: See menu_layout.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: err -- a SAT_BUP_* or SAVE_ERR_* code; device -- SAT_BUP_INTERNAL
 |         or SAT_BUP_CART
 | Returns: a static string, or NULL if err is SAT_BUP_OK
 ----------------------*/
const char *menu_layout_status_text(int err, unsigned long device)
{
    switch (err) {
    case SAT_BUP_OK:            return 0;
    case SAVE_ERR_TOO_LARGE:    return "SAVE STATE TOO LARGE";
    case SAT_BUP_ERR_NONE:      return "NO BACKUP DEVICE";
    case SAT_BUP_ERR_UNFORMAT:  return (device == SAT_BUP_CART)
                                       ? "CARTRIDGE UNFORMATTED"
                                       : "BACKUP RAM UNFORMATTED";
    case SAT_BUP_ERR_PROTECTED: return "CARTRIDGE WRITE PROTECTED";
    case SAT_BUP_ERR_NO_SPACE:  return "NOT ENOUGH SPACE";
    case SAT_BUP_ERR_NOT_FOUND: return "SAVE NOT FOUND";
    case SAT_BUP_ERR_EXISTS:    return "SLOT ALREADY IN USE";
    case SAT_BUP_ERR_BROKEN:    return "SAVE DATA DAMAGED";
    default:                    return "SAVE FAILED";
    }
}

/*----------------------
 | centre_x
 | Description: The left edge that puts a string across the middle of the
 |   display.
 |
 |   Measures the whole string including its spaces, because put_text advances
 |   the cursor for a space even though it emits no glyph -- a width counted
 |   from emitted glyphs alone would pull every string with a space in it to
 |   the right of centre by half a cell per space.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- the string that will be drawn
 | Returns: the x put_text should be given
 ----------------------*/
static int centre_x(const char *s)
{
    int cells = 0;

    while (s[cells] != 0) {
        cells++;
    }

    return (MENU_SCREEN_W - cells * MENU_GLYPH_W) / 2;
}

/*----------------------
 | build_title
 | Description: Lays out the sub-title screen: START GAME, LOAD GAME and a
 |   cursor glyph next to whichever row is selected.
 |
 |   Both rows are centred on their own width rather than sharing one left
 |   edge, so neither is hostage to the other's length; the cursor follows the
 |   row it marks rather than sitting at a fixed x, which is what keeps its gap
 |   to the text the same on both. That costs the cursor a four pixel step
 |   between the rows, which is the two strings' half-cell difference in length.
 |
 |   The only screen that draws in MENU_RAMP_TITLE_*, and the reason that pair
 |   exists: its text is the one text in the menus with no panel behind it, so
 |   it takes the game-select menu's white-outlined red rather than the blue
 |   ramp every panelled screen reads better in.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; out -- destination array; cap -- its capacity; n --
 |         write cursor, updated
 | Returns: N/A
 ----------------------*/
static void build_title(const MenuState *st, MenuItem *out, int cap, int *n)
{
    int startX = centre_x("START GAME");
    int loadX = centre_x("LOAD GAME");

    put_text(out, cap, n, startX, MENU_TITLE_START_Y, "START GAME",
             st->cursor == 0 ? MENU_RAMP_TITLE_SEL : MENU_RAMP_TITLE_DIM);
    put_text(out, cap, n, loadX, MENU_TITLE_LOAD_Y, "LOAD GAME",
             st->cursor == 1 ? MENU_RAMP_TITLE_SEL : MENU_RAMP_TITLE_DIM);
    put_text(out, cap, n,
             (st->cursor == 0 ? startX : loadX) - MENU_TITLE_CURSOR_DX,
             st->cursor == 0 ? MENU_TITLE_START_Y : MENU_TITLE_LOAD_Y, ">",
             MENU_RAMP_TITLE_SEL);
}

/*----------------------
 | build_pause
 | Description: Lays out the pause screen: its panel, the four rows and a
 |   cursor glyph.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; out -- destination array; cap -- its capacity; n --
 |         write cursor, updated
 | Returns: N/A
 ----------------------*/
static void build_pause(const MenuState *st, MenuItem *out, int cap, int *n)
{
    static const char *ROWS[4] = {
        "RESUME", "SAVE GAME", "LOAD GAME", "RETURN TO TITLE"
    };
    int i;

    put_panel(out, cap, n, MENU_PANEL_PAUSE, MENU_PAUSE_PANEL_X,
              MENU_PAUSE_PANEL_Y);
    for (i = 0; i < 4; i++) {
        put_text(out, cap, n, MENU_PAUSE_TEXT_X,
                 MENU_PAUSE_ROW0_Y + i * MENU_PAUSE_ROW_DY, ROWS[i],
                 st->cursor == i ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    }
    put_text(out, cap, n, MENU_PAUSE_CURSOR_X,
             MENU_PAUSE_ROW0_Y + st->cursor * MENU_PAUSE_ROW_DY, ">",
             MENU_RAMP_SEL);
}

/*----------------------
 | build_slots
 | Description: Lays out the slot list: its panel, header, device toggle,
 |   every slot row, the cursor glyph, an optional status line and the
 |   footer hint.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; status -- status line or NULL; out -- destination
 |         array; cap -- its capacity; n -- write cursor, updated
 | Returns: N/A
 ----------------------*/
static void build_slots(const MenuState *st, const char *status, MenuItem *out,
                        int cap, int *n)
{
    char row[MENU_ROW_CHARS];
    int i;

    put_panel(out, cap, n, MENU_PANEL_SLOTS, MENU_SLOTS_PANEL_X,
              MENU_SLOTS_PANEL_Y);
    put_text(out, cap, n, MENU_SLOTS_HEAD_X, MENU_SLOTS_HEAD_Y,
             st->saving ? "SAVE GAME" : "LOAD GAME", MENU_RAMP_DIM);
    put_text(out, cap, n, MENU_SLOTS_DEVICE_X, MENU_SLOTS_DEVICE_Y,
             st->device == SAT_BUP_CART ? "< CARTRIDGE >"
                                        : "< INTERNAL MEMORY >",
             st->cartPresent ? MENU_RAMP_SEL : MENU_RAMP_DIM);

    for (i = 0; i < SAVE_NUM_SLOTS; i++) {
        menu_layout_slot_row(row, MENU_ROW_CHARS, i, &st->slots[i]);
        put_text(out, cap, n, MENU_SLOTS_ROW_X,
                 MENU_SLOTS_ROW0_Y + i * MENU_SLOTS_ROW_DY, row,
                 st->slotCursor == i ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    }
    put_text(out, cap, n, MENU_SLOTS_CURSOR_X,
             MENU_SLOTS_ROW0_Y + st->slotCursor * MENU_SLOTS_ROW_DY, ">",
             MENU_RAMP_SEL);

    if (status != 0) {
        put_text(out, cap, n, MENU_SLOTS_STATUS_X, MENU_SLOTS_STATUS_Y, status,
                 MENU_RAMP_DIM);
    }
    put_text(out, cap, n, MENU_SLOTS_FOOT_X, MENU_SLOTS_FOOT_Y,
             "A SELECT   B BACK", MENU_RAMP_DIM);
}

/*----------------------
 | build_confirm
 | Description: Lays out the confirm screen: its panel, one or two lines of
 |   prompt text depending on what is pending, and the YES/NO answer row.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; out -- destination array; cap -- its capacity; n --
 |         write cursor, updated
 | Returns: N/A
 ----------------------*/
static void build_confirm(const MenuState *st, MenuItem *out, int cap, int *n)
{
    char row[MENU_ROW_CHARS];
    int pos = 0;

    put_panel(out, cap, n, MENU_PANEL_CONFIRM, MENU_CONFIRM_PANEL_X,
              MENU_CONFIRM_PANEL_Y);

    if (st->pending == MENU_ACT_RETURN_TO_TITLE) {
        put_text(out, cap, n, MENU_CONFIRM_TEXT_X, MENU_CONFIRM_LINE0_Y,
                 "RETURN TO TITLE ?", MENU_RAMP_DIM);
        put_text(out, cap, n, MENU_CONFIRM_TEXT_X, MENU_CONFIRM_LINE1_Y,
                 "PROGRESS WILL BE LOST", MENU_RAMP_DIM);
    } else {
        row[0] = 0;
        append_str(row, MENU_ROW_CHARS, &pos, "OVERWRITE SLOT ");
        append_char(row, MENU_ROW_CHARS, &pos, (char)('1' + st->slotCursor));
        append_str(row, MENU_ROW_CHARS, &pos, " ?");
        put_text(out, cap, n, MENU_CONFIRM_TEXT_X, MENU_CONFIRM_LINE0_Y, row,
                 MENU_RAMP_DIM);
    }

    put_text(out, cap, n, MENU_CONFIRM_YES_X, MENU_CONFIRM_ANSWER_Y, "YES",
             st->confirmYes ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    put_text(out, cap, n, MENU_CONFIRM_NO_X, MENU_CONFIRM_ANSWER_Y, "NO",
             st->confirmYes ? MENU_RAMP_DIM : MENU_RAMP_SEL);
    put_text(out, cap, n,
             st->confirmYes ? MENU_CONFIRM_YES_CURSOR_X
                            : MENU_CONFIRM_NO_CURSOR_X,
             MENU_CONFIRM_ANSWER_Y, ">", MENU_RAMP_SEL);
}

/*----------------------
 | menu_layout_build
 | Description: See menu_layout.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to lay out; status -- status line or NULL; out --
 |         destination array; cap -- its capacity
 | Returns: the number of items written, never more than cap
 ----------------------*/
int menu_layout_build(const MenuState *st, const char *status, MenuItem *out,
                      int cap)
{
    int n = 0;

    switch (st->screen) {
    case MENU_TITLE:   build_title(st, out, cap, &n); break;
    case MENU_PAUSE:   build_pause(st, out, cap, &n); break;
    case MENU_SLOTS:   build_slots(st, status, out, cap, &n); break;
    case MENU_CONFIRM: build_confirm(st, out, cap, &n); break;
    default: break;
    }
    return n;
}
