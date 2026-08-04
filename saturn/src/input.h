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
 |   src/system/input_srl.cxx is the Saturn implementation and is deliberately
 |   empty for now -- pad mapping is a later sub-project, and the intro this
 |   sub-project boots to plays on a timer with no input at all.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef INPUT_H
#define INPUT_H

extern int key_up, key_down, key_left, key_right;
extern int key_a, key_b, key_c, key_select;
extern int key_reset_record;

/*----------------------
 | check_events
 | Description: Drains the platform event queue and updates the key state and
 |   cls.quit. Called once per frame from the game loop. A backend with no
 |   input source implements this as an empty function rather than refusing to
 |   link -- that is the supported way to defer input, and it leaves the key
 |   state at its initial zero so the engine simply sees nothing pressed.
 | Author: suinevere
 ----------------------*/
void check_events(void);

#endif /* INPUT_H */
