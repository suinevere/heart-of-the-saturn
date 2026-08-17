/*----------------------
 | menu_state.c
 | Description: Implements the sub-title, pause, slot-list, confirm and
 |   controls screens as a pure state machine, ported from Another-Saturn's
 |   menu_state.cxx and retargeted at this port's savedata.h. No drawing, no
 |   backup RAM calls, no engine references -- callers act on the returned
 |   MenuAction and feed slot data back in through st->slots.
 | Author: suinevere
 | Dependencies: menu_state.h
 ----------------------*/
#include "menu_state.h"

/*----------------------
 | menu_state_enter_title
 | Description: See menu_state.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to initialise
 | Returns: N/A
 ----------------------*/
void menu_state_enter_title(MenuState *st)
{
    st->screen = MENU_TITLE;
    st->cursor = 0;
}

/*----------------------
 | menu_state_enter_pause
 | Description: See menu_state.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to initialise
 | Returns: N/A
 ----------------------*/
void menu_state_enter_pause(MenuState *st)
{
    st->screen = MENU_PAUSE;
    st->cursor = 0;
}

/*----------------------
 | menu_state_enter_slots
 | Description: Exists so the gate can open the slot list directly after a
 |   death with returnScreen = MENU_TITLE, which is what keeps a player with
 |   no saves off a dead end instead of a cancel with nowhere to go.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; saving -- non-zero to open in save mode; back --
 |         screen a cancel should return to
 | Returns: N/A
 ----------------------*/
void menu_state_enter_slots(MenuState *st, int saving, MenuScreen back)
{
    st->screen = MENU_SLOTS;
    st->saving = saving;
    st->slotCursor = 0;
    st->returnScreen = back;
}

/*----------------------
 | menu_state_enter_controls
 | Description: See menu_state.h.
 | Author: suinevere
 | Dependencies: keymap.h
 | Globals: N/A
 | Params: st -- state; back -- the screen a cancel should return to
 | Returns: N/A
 ----------------------*/
void menu_state_enter_controls(MenuState *st, MenuScreen back)
{
    st->screen       = MENU_CONTROLS;
    st->cursor       = 0;
    st->capturing    = -1;
    st->mapDirty     = 0;
    st->mapRejected  = 0;
    st->map          = *keymap_active();
    st->returnScreen = back;
}

/*----------------------
 | step_title
 | Description: Handles the sub-title screen: start game, load game and
 |   controls, in that cursor order.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; in -- this frame's input
 | Returns: the action for this frame
 ----------------------*/
static MenuAction step_title(MenuState *st, const MenuInput *in)
{
    if (in->up) {
        st->cursor = (st->cursor + 2) % 3;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % 3;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        if (st->cursor == 0) {
            return MENU_ACT_START_GAME;
        }
        if (st->cursor == 1) {
            menu_state_enter_slots(st, 0, MENU_TITLE);
            return MENU_ACT_RESCAN_SLOTS;
        }
        menu_state_enter_controls(st, MENU_TITLE);
        return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

/*----------------------
 | step_pause
 | Description: Handles the pause screen. Cancel and the pause button both
 |   resume from any cursor position, because a player who opened the menu
 |   by accident should not have to find the resume row. Row 3 opens the
 |   controls screen; return to title moved to row 4 to make room for it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; in -- this frame's input
 | Returns: the action for this frame
 ----------------------*/
static MenuAction step_pause(MenuState *st, const MenuInput *in)
{
    if (in->cancel || in->pause) {
        return MENU_ACT_RESUME;
    }
    if (in->up) {
        st->cursor = (st->cursor + 4) % 5;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % 5;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        if (st->cursor == 0) {
            return MENU_ACT_RESUME;
        }
        if (st->cursor == 1 || st->cursor == 2) {
            menu_state_enter_slots(st, st->cursor == 1, MENU_PAUSE);
            return MENU_ACT_RESCAN_SLOTS;
        }
        if (st->cursor == 3) {
            menu_state_enter_controls(st, MENU_PAUSE);
            return MENU_ACT_NONE;
        }
        st->screen = MENU_CONFIRM;
        st->pending = MENU_ACT_RETURN_TO_TITLE;
        st->confirmYes = 0;
        return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

/*----------------------
 | step_slots
 | Description: Handles the slot list: cursor movement, the internal/cart
 |   device toggle, and confirming a slot for load or save.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; in -- this frame's input
 | Returns: the action for this frame
 ----------------------*/
static MenuAction step_slots(MenuState *st, const MenuInput *in)
{
    SlotState state;

    if (in->cancel) {
        st->screen = st->returnScreen;
        return MENU_ACT_NONE;
    }
    if (in->up) {
        st->slotCursor = (st->slotCursor + SAVE_NUM_SLOTS - 1) % SAVE_NUM_SLOTS;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->slotCursor = (st->slotCursor + 1) % SAVE_NUM_SLOTS;
        return MENU_ACT_NONE;
    }
    if (in->left || in->right) {
        if (!st->cartPresent) {
            return MENU_ACT_NONE;
        }
        st->device = (st->device == SAT_BUP_INTERNAL) ? SAT_BUP_CART
                                                      : SAT_BUP_INTERNAL;
        return MENU_ACT_RESCAN_SLOTS;
    }
    if (in->confirm) {
        state = st->slots[st->slotCursor].state;
        if (st->saving) {
            if (state == SLOT_EMPTY) {
                return MENU_ACT_SAVE_SLOT;
            }
            st->screen = MENU_CONFIRM;
            st->pending = MENU_ACT_SAVE_SLOT;
            st->confirmYes = 0;
            return MENU_ACT_NONE;
        }
        if (state == SLOT_OK) {
            return MENU_ACT_LOAD_SLOT;
        }
        return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

/*----------------------
 | step_confirm
 | Description: Handles the yes/no confirm screen shared by an overwrite and
 |   a return-to-title. confirmYes starts false every time the screen is
 |   entered, so a stray confirm cannot destroy a save.
 |
 |   A confirmed overwrite lands on MENU_SLOTS, not MENU_PAUSE. This is a
 |   deliberate divergence from Another-Saturn's menu_state.cxx, which sends it
 |   to the pause menu: menu_layout_build renders the status line only inside
 |   build_slots, so on MENU_PAUSE the driver computes CARTRIDGE WRITE
 |   PROTECTED, NOT ENOUGH SPACE, SAVE STATE TOO LARGE or SAVE FAILED and then
 |   throws it away -- while an empty-slot save, which never reaches this
 |   screen, stays on MENU_SLOTS and shows the same error correctly. The spec's
 |   Error handling requirement is that save and load failures show their
 |   message on the slot list's status line, and the spec binds over the
 |   sibling port. Landing here also shows the refreshed row after a save that
 |   succeeded; cancel still returns to st->returnScreen, one press from where
 |   the player was.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; in -- this frame's input
 | Returns: the action for this frame
 ----------------------*/
static MenuAction step_confirm(MenuState *st, const MenuInput *in)
{
    MenuScreen decline =
        (st->pending == MENU_ACT_RETURN_TO_TITLE) ? MENU_PAUSE : MENU_SLOTS;
    MenuAction action;

    if (in->left || in->right) {
        st->confirmYes = !st->confirmYes;
        return MENU_ACT_NONE;
    }
    if (in->cancel) {
        st->screen = decline;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        if (!st->confirmYes) {
            st->screen = decline;
            return MENU_ACT_NONE;
        }
        action = st->pending;
        st->screen =
            (action == MENU_ACT_RETURN_TO_TITLE) ? MENU_TITLE : MENU_SLOTS;
        return action;
    }
    return MENU_ACT_NONE;
}

/*----------------------
 | leave_controls
 | Description: Returns to returnScreen, asking the caller to save the map
 |   exactly when something in it actually changed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state
 | Returns: MENU_ACT_SAVE_KEYMAP if the map is dirty, MENU_ACT_NONE otherwise
 ----------------------*/
static MenuAction leave_controls(MenuState *st)
{
    st->screen = st->returnScreen;
    st->cursor = 0;

    if (st->mapDirty) {
        st->mapDirty = 0;
        return MENU_ACT_SAVE_KEYMAP;
    }
    return MENU_ACT_NONE;
}

/*----------------------
 | step_controls
 | Description: Handles the controls screen: capturing a button for whichever
 |   row is selected, clearing the shortcut row with left, resetting to
 |   defaults, and leaving.
 |
 |   While a row is capturing, Start is the only button that aborts it. Every
 |   one of the eight bindable buttons -- including B, which is Cancel on
 |   every other screen -- has to be capturable here, so B cannot also mean
 |   cancel while a capture is in progress. Start is already spoken for as
 |   the pause button and is never itself bindable, which makes it the one
 |   button both sides can agree means "stop capturing" without taking a
 |   binding away from the player.
 | Author: suinevere
 | Dependencies: keymap.h
 | Globals: N/A
 | Params: st -- state; in -- this frame's input
 | Returns: the action for this frame
 ----------------------*/
static MenuAction step_controls(MenuState *st, const MenuInput *in)
{
    if (st->capturing >= 0) {
        if (in->pause) {
            st->capturing = -1;
            return MENU_ACT_NONE;
        }
        if (in->captured != PAD_NONE) {
            if (keymap_assign(&st->map, (KeymapRow)st->capturing, in->captured)) {
                st->mapDirty = 1;
            } else {
                st->mapRejected = 1;
            }
            st->capturing = -1;
        }
        return MENU_ACT_NONE;
    }

    if (in->up) {
        st->cursor = (st->cursor + MENU_CONTROLS_ROWS - 1) % MENU_CONTROLS_ROWS;
        st->mapRejected = 0;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % MENU_CONTROLS_ROWS;
        st->mapRejected = 0;
        return MENU_ACT_NONE;
    }
    if (in->left && st->cursor == KEYMAP_ROW_FORWARD) {
        if (keymap_assign(&st->map, KEYMAP_ROW_FORWARD, PAD_NONE)) {
            st->mapDirty = 1;
        }
        st->mapRejected = 0;
        return MENU_ACT_NONE;
    }
    if (in->cancel) {
        return leave_controls(st);
    }
    if (in->confirm) {
        st->mapRejected = 0;

        if (st->cursor < KEYMAP_ROW_COUNT) {
            st->capturing = st->cursor;
            return MENU_ACT_NONE;
        }
        if (st->cursor == MENU_CONTROLS_ROW_RESET) {
            keymap_defaults(&st->map);
            st->mapDirty = 1;
            return MENU_ACT_NONE;
        }
        return leave_controls(st);
    }
    return MENU_ACT_NONE;
}

/*----------------------
 | menu_state_step
 | Description: See menu_state.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; in -- this frame's edge-triggered input
 | Returns: the action the caller must perform
 ----------------------*/
MenuAction menu_state_step(MenuState *st, const MenuInput *in)
{
    switch (st->screen) {
    case MENU_TITLE:    return step_title(st, in);
    case MENU_PAUSE:    return step_pause(st, in);
    case MENU_SLOTS:    return step_slots(st, in);
    case MENU_CONFIRM:  return step_confirm(st, in);
    case MENU_CONTROLS: return step_controls(st, in);
    default:            return MENU_ACT_NONE;
    }
}
