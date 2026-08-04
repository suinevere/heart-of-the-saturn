/*----------------------
 | platform.h
 | Description: Startup, shutdown, the millisecond clock and the frame pump --
 |   everything main.c used to take from SDL that is not video, input or disc.
 |
 |   platform_frame is the one function with no SDL ancestor. On the host the
 |   game loop is free-running and this is a no-op; on Saturn it is
 |   SRL::Core::Synchronize(), and the loop must call it every iteration or the
 |   VDP2 layer is never presented and the screen stays black. It is declared
 |   here rather than hidden inside video_render because it paces the whole
 |   frame, not just the blit.
 |
 |   Audio device setup is deliberately absent. main.c's Mix_OpenAudioDevice
 |   block exists solely to guarantee the host disc backend's CD-DA
 |   precondition, as disc.h's banner sets out; SRL::Sound::Cdda has no
 |   negotiated spec and no resampler question, so there is nothing to
 |   abstract. It moves into the host backend instead.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef PLATFORM_H
#define PLATFORM_H

/*----------------------
 | platform_init
 | Description: Module initializer, called before any other engine startup.
 |   On the host this is SDL_Init plus the audio-device negotiation the
 |   CD-DA path depends on (see disc.h's banner); a Saturn backend has no
 |   equivalent negotiation to perform. Returns 1 on success, 0 on failure.
 | Author: suinevere
 ----------------------*/
int          platform_init(void);

/*----------------------
 | platform_quit
 | Description: Releases whatever platform_init acquired. Registered in
 |   atexit_callback alongside the disc and audio teardown it used to sit
 |   next to inline in main.c.
 | Author: suinevere
 ----------------------*/
void         platform_quit(void);

/*----------------------
 | platform_ticks
 | Description: Milliseconds elapsed since platform_init, on whatever clock
 |   the backend has. The engine's frame throttle in rest() busy-waits on
 |   this value at main.c's `while (current_tick - last_tick < diff)` loop --
 |   a backend that returns a coarser unit or a non-monotonic clock does not
 |   fail that loop, it just changes the game's speed.
 | Author: suinevere
 ----------------------*/
unsigned int platform_ticks(void);

/*----------------------
 | platform_delay
 | Description: Blocks the calling thread for approximately ms milliseconds.
 |   Used only by rest()'s busy-wait to avoid spinning at full CPU between
 |   ticks.
 | Author: suinevere
 ----------------------*/
void         platform_delay(unsigned int ms);

/*----------------------
 | platform_frame
 | Description: Called once per game-loop iteration, after rendering. On the
 |   host it is empty -- the loop is free-running. On Saturn it will be
 |   SRL::Core::Synchronize(), without which the VDP2 layer is never
 |   presented and the screen stays black.
 | Author: suinevere
 ----------------------*/
void         platform_frame(void);

#endif /* PLATFORM_H */
