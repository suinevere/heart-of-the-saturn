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
 | Dependencies: none
 ----------------------*/
#ifndef INPUT_H
#define INPUT_H

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
 | input_menu_start
 | Description: Whether Start is held. The menu needs it and no key_* global
 |   carries it: input_srl.cxx deliberately never writes key_select, which is
 |   host input-recording state that update_keys reads for record and replay.
 |   A ninth key global would put a Saturn-only signal into a seam both
 |   backends share, so the question is asked directly instead.
 |
 |   Level, not edge -- menu.c owns edge detection for every button it reads,
 |   and mixing the two is what let the debug chord this replaces fire a save
 |   when the player went from Start plus B to Start plus A plus B without
 |   releasing.
 |
 |   Saturn only. The host has no caller: menu_pause_poll is behind
 |   HOTA_SATURN, which is why input_sdl.c never defined the chord either.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: 1 while Start is held, 0 otherwise
 ----------------------*/
int input_menu_start(void);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */
