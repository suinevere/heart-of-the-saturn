/*----------------------
 | input_sdl.c
 | Description: SDL host implementation of input.h. check_events() below is
 |   moved verbatim from src/main.c, where it lived as file-static SDL
 |   keyboard handling before the input seam existed -- the keysym switch is
 |   host behaviour and the move does not touch it.
 | Author: suinevere
 | Dependencies: SDL.h, input.h, client.h, debug.h, sprites.h, video.h,
 |   stdio.h (sprites.h's quickload_sprites/quicksave_sprites prototypes need
 |   FILE and never include it themselves)
 ----------------------*/
#include <stdio.h>
#include <SDL.h>

#include "input.h"
#include "client.h"
#include "debug.h"
#include "sprites.h"
#include "video.h"

/*----------------------
 | quicksave / quickload / leave_game / speed_throttle
 | Description: check_events calls the first three and reads/writes the
 |   fourth, but all four are engine globals defined in main.c with no
 |   header of their own -- every caller used to be main.c itself, so no
 |   declaration was ever needed until check_events moved out. Declared here
 |   rather than in main.h so main.c's own edits for this task stay limited
 |   to what the brief asked for.
 | Author: suinevere
 ----------------------*/
extern void quicksave(void);
extern void quickload(void);
extern void leave_game(void);
extern int speed_throttle;

/** Processes SDL events

    This processes windows messages, keyboard pressed, joystick moves,
    and general SDL events
*/
void check_events(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
	        switch (event.type)
		{
			case SDL_KEYUP:
			switch(event.key.keysym.sym)
			{
				case SDLK_RIGHT:
				key_right = 0;
				break;

				case SDLK_LEFT:
				key_left = 0;
				break;

				case SDLK_UP:
				key_up = 0;
				break;

				case SDLK_DOWN:
				key_down = 0;
				break;

				case SDLK_z:
				case SDLK_a:
				#ifdef PYRA
				case SDLK_PAGEDOWN:
				#endif
				key_a = 0;
				break;

				case SDLK_x:
				case SDLK_s:
				#ifdef PYRA
				case SDLK_END:
				#endif
				key_b = 0;
				break;

				case SDLK_c:
				case SDLK_d:
				#ifdef PYRA
				case SDLK_HOME:
				#endif
				key_c = 0;
				break;

				case SDLK_q:
				key_a = 0;
				key_reset_record = 0;
				break;

				case SDLK_SPACE:
				speed_throttle = 0;
				break;

				default:
				/* keep -Wall happy */
				break;
			}
			break;

	        	case SDL_KEYDOWN:
			switch(event.key.keysym.sym)
			{
				#ifdef ENABLE_DEBUG
				/* enable/disable sprites */
				case SDLK_1:
				case SDLK_2:
				case SDLK_3:
				case SDLK_4:
				case SDLK_5:
				case SDLK_6:
				case SDLK_7:
				case SDLK_8:
				case SDLK_9:
				{
					int tmp = event.key.keysym.sym - SDLK_1 + 1;

					if (event.key.keysym.mod & KMOD_SHIFT)
					{
						tmp = tmp + 10;
					}

					sprites[tmp].u1 ^= 0x80;
				}
				break;
				#endif

				case SDLK_ESCAPE:
				cls.quit = 1;
				break;

				case SDLK_RIGHT:
				key_right = 1;
				break;

				case SDLK_LEFT:
				key_left = 1;
				break;

				case SDLK_UP:
				key_up = 1;
				break;

				case SDLK_DOWN:
				key_down = 1;
				break;

				case SDLK_z:
				case SDLK_a:
				#ifdef PYRA
				case SDLK_PAGEDOWN:
				#endif
				key_a = 1;
				break;

				case SDLK_x:
				case SDLK_s:
				#ifdef PYRA
				case SDLK_END:
				#endif
				key_b = 1;
				break;

				case SDLK_c:
				case SDLK_d:
				#ifdef PYRA
				case SDLK_HOME:
				#endif
				key_c = 1;
				break;

				#ifdef ENABLE_DEBUG
				case SDLK_g:
				debug_flag ^= 1;
				break;
				#endif

				case SDLK_F5:
				quicksave();
				break;

				case SDLK_F7:
				quickload();
				break;

				#ifndef PYRA
				case SDLK_RETURN:
				if (event.key.keysym.mod & KMOD_ALT)
				{
					video_toggle_fullscreen();
				}
				break;
				#endif

				case SDLK_q:
				key_a = 1;
				key_reset_record = 1;
				break;

				case SDLK_SPACE:
				speed_throttle = 1;
				break;

				default:
				/* keep -Wall happy */
				break;
			}
			break;

			case SDL_QUIT:
			leave_game();
			break;
		}
	}
}
