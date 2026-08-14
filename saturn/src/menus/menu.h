/*----------------------
 | menu.h
 | Description: The two points where the menu layer meets the running game: a
 |   gate on the room the engine reaches between playthroughs, and a poll that
 |   runs at the one moment in the frame where a save is legal.
 |
 |   Everything below this header is pure or platform-facing -- menu_state.h is
 |   a state machine, menu_layout.h is arithmetic, menu_clock.h is timers, and
 |   saturn_menuart.h is the sprite seam. menu.c is the only file in the layer
 |   that knows the engine exists, which is what keeps the other four
 |   host-testable.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef MENU_H
#define MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MENU_PASSWORD_ROOM
 | Description: Room 7 is the Sega CD's password entry screen. run() reaches it
 |   after the intro (main.c), and decode.c's ending branch sends the credits
 |   back to it. Intercepting the room rather than the reason covers every route
 |   into it -- except a death, which turned out to reach the password screen
 |   without changing room at all. decode.c's death branch now asks for this
 |   room explicitly, on death_played, which is what puts a death back inside
 |   the one interception point.
 | Author: suinevere
 ----------------------*/
#define MENU_PASSWORD_ROOM 7

/*----------------------
 | MENU_START_ROOM
 | Description: Where a new game begins, now that the password screen no longer
 |   picks it. One constant, because this is the one value in the feature that
 |   could not be verified by reading and has to survive an emulator run.
 | Author: suinevere
 ----------------------*/
#define MENU_START_ROOM    1

/*----------------------
 | menu_gate
 | Description: Runs the sub-title menu, or the load screen, in place of the
 |   password room, and reports the room run() should load next.
 |
 |   Returns 0 when a slot was loaded, because quickload() calls load_room and
 |   restores the tasks and the sprite list itself -- running run()'s own load
 |   block over that would reset the sprite list and the tasks it just
 |   restored, and load the room a second time.
 |
 |   Every path out of here that hands control to the game arms screen.c's
 |   deferred fade in, including that one: the gate closes over a black screen
 |   -- its own fade out leaves one -- and only the frame the game finally draws
 |   is safe to start bringing the light back on.
 |
 |   A death opens this in overlay mode so the load card composites over the
 |   frame the player died on; everything else opens on the title card. Closing
 |   the card promotes to the title card either way, which is the one route from
 |   a death to the sub-title menu.
 | Author: suinevere
 | Dependencies: menu_state.h, menu_layout.h, menu_clock.h, saturn_menuart.h,
 |   saturn_saveslot.h, savedata.h, disc.h, input.h, main.h, screen.h, video.h,
 |   fadecalc.h
 | Globals: next_script, ending_played, death_played
 | Params: N/A
 | Returns: the room to load, or 0 if a load already restored the game state
 ----------------------*/
int  menu_gate(void);

/*----------------------
 | menu_pause_poll
 | Description: Opens the pause menu when Start is newly pressed, and returns
 |   immediately otherwise.
 |
 |   Must be called between check_events() and the task loop's first
 |   toggle_aux(0), and nowhere else. That is the only point in the program
 |   where quicksave's and quickload's "no active thread" precondition holds,
 |   and it is the save point already verified on real hardware.
 | Author: suinevere
 | Dependencies: menu_state.h, saturn_menuart.h, saturn_saveslot.h, input.h
 | Globals: next_script
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_pause_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_H */
