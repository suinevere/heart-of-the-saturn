/*----------------------
 | menu_state.h
 | Description: The sub-title, pause, slot-list, confirm and controls screens
 |   as a pure state machine: no drawing, no backup RAM calls, no engine
 |   references. That is what makes it host-testable with gcc, the same
 |   reason bootmenu.h and discfmt.h are kept clean -- this must not include
 |   srl.hpp, sega_bup.h or any engine header, only savedata.h and keymap.h.
 | Author: suinevere
 | Dependencies: savedata.h, keymap.h
 ----------------------*/
#ifndef MENU_STATE_H
#define MENU_STATE_H

#include "savedata.h"
#include "keymap.h"

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
    MENU_CONFIRM,
    MENU_CONTROLS,
    MENU_DEATH
} MenuScreen;

/*----------------------
 | MenuAction
 | Description: What the caller must do in response to a step. Most actions
 |   are self-explanatory except MENU_ACT_RESCAN_SLOTS: it means the caller
 |   must re-probe st->device and refill st->slots before the next call, since
 |   this module never touches backup RAM itself. MENU_ACT_SAVE_KEYMAP is its
 |   counterpart for the controls screen: it means the caller must install
 |   st->map as the active mapping with keymap_set_active and persist it to
 |   backup RAM, since this module never touches keymap's live state or the
 |   backup library either.
 |
 |   MENU_ACT_RESUME means different things to the two callers that can see it,
 |   and both are correct: from the pause menu it means close the menu and let
 |   the frame continue, and from the death screen it means reload the room the
 |   player died in without resetting the VM. Only menu_gate ever sees the
 |   second, because MENU_DEATH is only ever opened there.
 |
 |   MENU_ACT_SAVE_AND_RESUME is the death screen's first row: save to the
 |   first slot and then resume as above. The save cannot happen where the
 |   action is raised -- the state at that moment is the one the player just
 |   died in -- so the caller defers it until after the room reload.
 | Author: suinevere
 ----------------------*/
typedef enum {
    MENU_ACT_NONE,
    MENU_ACT_START_GAME,
    MENU_ACT_RESUME,
    MENU_ACT_SAVE_SLOT,
    MENU_ACT_LOAD_SLOT,
    MENU_ACT_RETURN_TO_TITLE,
    MENU_ACT_RESCAN_SLOTS,
    MENU_ACT_SAVE_KEYMAP,
    MENU_ACT_SAVE_AND_RESUME
} MenuAction;

/*----------------------
 | MenuInput
 | Description: One frame of button state. Every field is true only on the
 |   frame the button became pressed -- the caller owns edge detection, so
 |   this module never has to guess whether a held button should repeat.
 |   captured is the odd one out: every other field names a role (confirm,
 |   cancel, pause) that the caller maps from whichever physical button plays
 |   that part, but a capture on the controls screen has to know which
 |   physical button was actually pressed, so captured carries a PadButton
 |   identity instead of a role -- PAD_NONE when nothing new became held this
 |   frame.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int up, down, left, right, confirm, cancel, pause;
    PadButton captured;
} MenuInput;

/*----------------------
 | MenuState
 | Description: Everything the machine remembers between frames. returnScreen
 |   is private to menu_state.c -- it remembers which screen opened the slot
 |   list or the controls screen so a cancel goes back to the right place.
 |   Callers must not read or write it, except through menu_state_enter_slots
 |   or menu_state_enter_controls.
 |
 |   map, capturing and mapDirty belong to the controls screen only, and are
 |   meaningless outside it. map is the screen's own working copy of the
 |   mapping, never the live one keymap_active returns -- nothing the player
 |   does to it reaches the pad until they leave and the caller installs it.
 |   capturing is the KeymapRow currently waiting for a button, or -1 when no
 |   row is capturing. mapDirty is set the moment map first diverges from what
 |   was loaded, and gates whether leaving the screen asks the caller to save.
 |
 |   There is no rejection state, because with every row holding a real button
 |   keymap_assign can no longer refuse one -- the displaced row always has a
 |   binding to receive.
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
    KeyMap     map;
    int        capturing;
    int        mapDirty;
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
 | MENU_CONTROLS_ROWS / MENU_CONTROLS_ROW_RESET / MENU_CONTROLS_ROW_BACK
 | Description: Five rows: the three bindings in KeymapRow order, then reset,
 |   then back. The bindings share KeymapRow's numbering on purpose, so a
 |   cursor below KEYMAP_ROW_COUNT is a row index and needs no table.
 | Author: suinevere
 ----------------------*/
#define MENU_CONTROLS_ROWS       5
#define MENU_CONTROLS_ROW_RESET  3
#define MENU_CONTROLS_ROW_BACK   4

/*----------------------
 | menu_state_enter_controls
 | Description: Opens the controls screen on the first binding row, taking a
 |   working copy of the live mapping. The copy is what makes cancelling safe:
 |   nothing the player does here reaches the pad until they leave and the
 |   caller installs it.
 | Author: suinevere
 | Dependencies: keymap.h
 | Globals: N/A
 | Params: st -- state; back -- the screen a cancel should return to
 | Returns: N/A
 ----------------------*/
void menu_state_enter_controls(MenuState *st, MenuScreen back);

/*----------------------
 | MENU_DEATH_ROWS / MENU_DEATH_ROW_RESUME / MENU_DEATH_ROW_SAVE /
 | MENU_DEATH_ROW_SLOT0 / MENU_DEATH_ROW_TITLE
 | Description: The screen a death opens. Resume, save and resume, one row per
 |   save slot, then return to title.
 |
 |   The slot rows sit inline rather than behind a further screen because a
 |   death is the one arrival where the player has not chosen to be in a menu
 |   at all: every route out of it is one press from the row they land on.
 |   Subtracting MENU_DEATH_ROW_SLOT0 from the cursor gives the slot index.
 | Author: suinevere
 ----------------------*/
#define MENU_DEATH_ROW_RESUME 0
#define MENU_DEATH_ROW_SAVE   1
#define MENU_DEATH_ROW_SLOT0  2
#define MENU_DEATH_ROW_TITLE  (MENU_DEATH_ROW_SLOT0 + SAVE_NUM_SLOTS)
#define MENU_DEATH_ROWS       (MENU_DEATH_ROW_TITLE + 1)

/*----------------------
 | menu_state_enter_death
 | Description: Opens the death screen on the resume row, so the press a player
 |   is most likely already making does the least destructive thing.
 |
 |   Cancel resumes rather than backing out to anything, because there is
 |   nothing behind a death to back out to -- the screen is the first thing the
 |   player sees after dying, not something they opened.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to initialise
 | Returns: N/A
 ----------------------*/
void menu_state_enter_death(MenuState *st);

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
