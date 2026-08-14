/*----------------------
 | menu_layout.h
 | Description: Turns a MenuState into an ordered list of draw items in frame
 |   pixels, plus the slot-row and error strings. Stays pure -- no VDP1, no
 |   engine headers -- so host gcc can test what renders where and in which
 |   ramp before the sprite layer ever exists.
 | Author: suinevere
 | Dependencies: menu_state.h
 ----------------------*/
#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

#include "menu_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MENU_ITEM_PANEL / MENU_ITEM_GLYPH
 | Description: What a MenuItem draws: a backdrop panel or one text glyph.
 | Author: suinevere
 ----------------------*/
#define MENU_ITEM_PANEL 0
#define MENU_ITEM_GLYPH 1

/*----------------------
 | MENU_PANEL_PAUSE / MENU_PANEL_SLOTS / MENU_PANEL_CONFIRM
 | Description: Which panel art a MENU_ITEM_PANEL's id selects.
 | Author: suinevere
 ----------------------*/
#define MENU_PANEL_PAUSE   0
#define MENU_PANEL_SLOTS   1
#define MENU_PANEL_CONFIRM 2

/*----------------------
 | MENU_RAMP_DIM .. MENU_RAMP_TITLE_SEL
 | Description: Which colour ramp a glyph or panel draws with -- dim for
 |   unselected, bright for whatever the cursor is on.
 |
 |   Two pairs, because the sub-title card is the only screen whose text sits on
 |   the artwork rather than on a panel of its own. It draws in the game-select
 |   menu's white-outlined red; every screen with a panel keeps the blue ramp the
 |   menus shipped with, and hides the outline by drawing it in the panel's own
 |   fill colour. The distinction is per item rather than per screen because
 |   saturn_menuart.cxx cannot see which screen it is drawing -- the load screen
 |   and the sub-title menu are both exclusive-mode.
 |
 |   The values are CRAM bank indices in saturn_menuart.cxx and are contiguous
 |   from zero on purpose; that file's MENU_ART_BANK_COUNT must match how many
 |   there are here.
 | Author: suinevere
 ----------------------*/
#define MENU_RAMP_DIM       0
#define MENU_RAMP_SEL       1
#define MENU_RAMP_TITLE_DIM 2
#define MENU_RAMP_TITLE_SEL 3

/*----------------------
 | MENU_LAYOUT_MAX_ITEMS
 | Description: 200 sits under SGL_MAX_POLYGONS (256) with room for the
 |   backdrop and the panel, so a full slot list cannot overrun the VDP1
 |   command list. The widest screen measures ~119 items.
 | Author: suinevere
 ----------------------*/
#define MENU_LAYOUT_MAX_ITEMS 200

/*----------------------
 | MENU_ROW_CHARS
 | Description: The slot-row buffer: big enough for the longest row the
 |   formatters can build plus its terminator, with slack. It is not a pixel
 |   bound -- 32 cells from MENU_SLOTS_ROW_X (64) would end at x=320, past the
 |   slot panel's right edge at 295 and past the screen.
 |
 |   The panel fit comes from the longest *constructible* row instead:
 |   "SLOT 3  ROOM 999  12/31 23:59" is 29 characters, because append_room
 |   clamps the room id to 999 and append_pad2 clamps every date field to two
 |   digits. Its 29th cell starts at 64 + 28*8 = 288, and a glyph is 5 px of
 |   ink in the top 5 bits of an 8 px cell, so the ink ends at x=292 -- inside
 |   the panel's interior, which runs 26..293 after the art tool's 2 px border
 |   on a panel spanning 24..295.
 |
 |   That last cell's three blank columns do overlap the border, and it is
 |   load-bearing that this is harmless: those columns are palette index 0,
 |   which VDP1 treats as transparent in a Paletted16 sprite, so nothing is
 |   painted over the frame.
 | Author: suinevere
 ----------------------*/
#define MENU_ROW_CHARS 32

/*----------------------
 | MenuItem
 | Description: One thing to draw. x and y are the top-left corner in pixels
 |   in the 320x224 frame, not cells and not the 304x192 the engine renders
 |   into -- the menu draws as VDP1 sprites over the whole display, which is
 |   wider and taller than the game's own bitmap. For a glyph, id is the
 |   ASCII code, not an index: the art layer subtracts 0x20, and keeping
 |   ASCII here means the tests read as text.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char kind;
    unsigned char id;
    short         x;
    short         y;
    unsigned char ramp;
} MenuItem;

/*----------------------
 | menu_layout_build
 | Description: Builds the draw list for st->screen.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to lay out; status -- status line to show on the slot
 |         screen, or NULL for none; out -- destination array; cap -- its
 |         capacity
 | Returns: the number of items written, never more than cap
 ----------------------*/
int menu_layout_build(const MenuState *st, const char *status,
                      MenuItem *out, int cap);

/*----------------------
 | menu_layout_slot_row
 | Description: Formats one slot's row text.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- destination, at least cap bytes; cap -- its capacity;
 |         slot -- 0-based slot index; info -- what savedata_probe found
 | Returns: N/A
 ----------------------*/
void menu_layout_slot_row(char *out, int cap, int slot, const SlotInfo *info);

/*----------------------
 | menu_layout_status_text
 | Description: Words a backup RAM error for the status line.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: err -- a SAT_BUP_* or SAVE_ERR_* code; device -- SAT_BUP_INTERNAL
 |         or SAT_BUP_CART, to word an unformatted device correctly
 | Returns: a static string, or NULL if err is SAT_BUP_OK
 ----------------------*/
const char *menu_layout_status_text(int err, unsigned long device);

#ifdef __cplusplus
}
#endif

#endif /* MENU_LAYOUT_H */
