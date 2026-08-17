/*----------------------
 | input.h
 | Description: The single platform boundary between the engine and the
 |   controls. check_events() drains whatever the platform calls an event
 |   queue and leaves the result in the key state below; the game loop reads
 |   that state and never asks how it got there.
 |
 |   The key variables are defined in main.c and declared here rather than
 |   living with the backend, because the game loop in main.c reads them every
 |   frame while the backend only writes them. They were file-static in main.c
 |   until the backend moved out; that is the only reason they are visible.
 |
 |   saturn/host/input_sdl.c is the SDL keyboard implementation.
 |   src/system/input_srl.cxx is the Saturn implementation, over port 0's
 |   digital pad.
 | Author: suinevere
 | Dependencies: keymap.h
 ----------------------*/
#ifndef INPUT_H
#define INPUT_H

#include "keymap.h"

/* The Saturn backend is C++; without this its definitions would get C++
   linkage and fail to satisfy the C callers. */
#ifdef __cplusplus
extern "C" {
#endif

extern int key_up, key_down, key_left, key_right;
extern int key_a, key_b, key_c, key_select;
extern int key_reset_record;

/*----------------------
 | check_events
 | Description: Updates the key state from whatever the platform offers.
 |   Called once per frame from the game loop. Draining an event queue and
 |   setting cls.quit are the SDL backend's shape of that contract, not the
 |   seam's requirement -- the Saturn backend samples pad state instead and
 |   never touches cls.quit. A backend with no input source implements this as
 |   an empty function rather than refusing to link -- that is the supported
 |   way to defer input, and it leaves the key state at its initial zero so
 |   the engine simply sees nothing pressed.
 | Author: suinevere
 ----------------------*/
void check_events(void);

/*----------------------
 | input_raw_buttons
 | Description: Port 0's physical button state this frame, before any
 |   mapping, as a PAD_BIT_* mask.
 |
 |   Exists because the key globals stopped being raw. keymap_apply writes
 |   key_a, key_b and key_c from whatever the player bound, and menu_key_mask
 |   used to read key_a as confirm and key_b as cancel -- so without this,
 |   binding Run to X would move the pause menu's confirm button to X too, and
 |   with it the screen that would let the player undo the change. The rule
 |   this seam exists to enforce is that the mapping writes the key globals
 |   and every menu reads raw.
 |
 |   Level, not edge, for the reason input_menu_start gave before it was
 |   folded in here: menu.c owns edge detection for every button it reads, and
 |   mixing the two is what let an earlier debug chord fire on the wrong
 |   frame. Start is PAD_BIT_START in the mask.
 |
 |   Saturn only. menu_pause_poll is behind HOTA_SATURN, which is why
 |   input_sdl.c never defined the chord this replaces either.
 | Author: suinevere
 | Dependencies: keymap.h
 | Globals: N/A
 | Params: N/A
 | Returns: the PAD_BIT_* mask of everything held, or 0 if no pad is connected
 ----------------------*/
unsigned int input_raw_buttons(void);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */
