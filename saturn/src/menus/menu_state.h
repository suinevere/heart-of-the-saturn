/*----------------------
 | menu_state.h
 | Description: The sub-title, pause, slot-list and confirm screens as a pure
 |   state machine: no drawing, no backup RAM calls, no engine references.
 |   That is what makes it host-testable with gcc, the same reason bootmenu.h
 |   and discfmt.h are kept clean -- this must not include srl.hpp, sega_bup.h
 |   or any engine header, only savedata.h.
 | Author: suinevere
 | Dependencies: savedata.h
 ----------------------*/
#ifndef MENU_STATE_H
#define MENU_STATE_H

#include "savedata.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MenuScreen
 | Description: Which screen is showing.
 | Author: suinevere
 ----------------------*/
typedef enum {
    MENU_NONE,
    MENU_TITLE,
    MENU_PAUSE,
    MENU_SLOTS,
    MENU_CONFIRM
} MenuScreen;

/*----------------------
 | MenuAction
 | Description: What the caller must do in response to a step. Most actions
 |   are self-explanatory except MENU_ACT_RESCAN_SLOTS: it means the caller
 |   must re-probe st->device and refill st->slots before the next call, since
 |   this module never touches backup RAM itself.
 | Author: suinevere
 ----------------------*/
typedef enum {
    MENU_ACT_NONE,
    MENU_ACT_START_GAME,
    MENU_ACT_RESUME,
    MENU_ACT_SAVE_SLOT,
    MENU_ACT_LOAD_SLOT,
    MENU_ACT_RETURN_TO_TITLE,
    MENU_ACT_RESCAN_SLOTS
} MenuAction;

/*----------------------
 | MenuInput
 | Description: One frame of button state. Every field is true only on the
 |   frame the button became pressed -- the caller owns edge detection, so
 |   this module never has to guess whether a held button should repeat.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int up, down, left, right, confirm, cancel, pause;
} MenuInput;

/*----------------------
 | MenuState
 | Description: Everything the machine remembers between frames. returnScreen
 |   is private to menu_state.c -- it remembers which screen opened the slot
 |   list so a cancel goes back to the right place. Callers must not read or
 |   write it, except through menu_state_enter_slots.
 | Author: suinevere
 ----------------------*/
typedef struct {
    MenuScreen screen;
    int        cursor;
    int        slotCursor;
    int        saving;
    unsigned long device;
    int        cartPresent;
    int        confirmYes;
    MenuAction pending;
    SlotInfo   slots[SAVE_NUM_SLOTS];
    MenuScreen returnScreen;
} MenuState;

/*----------------------
 | menu_state_enter_title
 | Description: Opens the sub-title screen with the cursor on start game.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to initialise
 | Returns: N/A
 ----------------------*/
void menu_state_enter_title(MenuState *st);

/*----------------------
 | menu_state_enter_pause
 | Description: Opens the pause screen with the cursor on resume.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to initialise
 | Returns: N/A
 ----------------------*/
void menu_state_enter_pause(MenuState *st);

/*----------------------
 | menu_state_enter_slots
 | Description: Opens the slot list directly, bypassing the screens that
 |   normally lead to it. Exists so the gate can open the slot list right
 |   after a death with back = MENU_TITLE, which is what keeps a player with
 |   no saves off a dead end instead of stranding them on a sub-title menu
 |   with nothing behind cancel.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; saving -- non-zero to open in save mode; back --
 |         screen a cancel should return to
 | Returns: N/A
 ----------------------*/
void menu_state_enter_slots(MenuState *st, int saving, MenuScreen back);

/*----------------------
 | menu_state_step
 | Description: Advances the machine by one frame of input and reports what
 |   the caller must do. MENU_ACT_RESCAN_SLOTS means the caller must re-probe
 |   st->device and refill st->slots before the next call.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; in -- this frame's edge-triggered input
 | Returns: the action the caller must perform
 ----------------------*/
MenuAction menu_state_step(MenuState *st, const MenuInput *in);

#ifdef __cplusplus
}
#endif

#endif /* MENU_STATE_H */
