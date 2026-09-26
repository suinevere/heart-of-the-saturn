#include <stdio.h>
#include <SDL.h>

#include "input.h"
#include "client.h"
#include "debug.h"
#include "sprites.h"
#include "video.h"

extern void quicksave(void);
extern void quickload(void);
extern void leave_game(void);
extern int speed_throttle;

unsigned int input_raw_buttons(void)
{
	unsigned int raw = 0;

	if (key_up)    raw |= PAD_BIT_UP;
	if (key_down)  raw |= PAD_BIT_DOWN;
	if (key_left)  raw |= PAD_BIT_LEFT;
	if (key_right) raw |= PAD_BIT_RIGHT;
	if (key_a)     raw |= PAD_BIT_A;
	if (key_b)     raw |= PAD_BIT_B;
	if (key_c)     raw |= PAD_BIT_C;

	return raw;
}

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
				break;
			}
			break;

	        	case SDL_KEYDOWN:
			switch(event.key.keysym.sym)
			{
				#ifdef ENABLE_DEBUG
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
				break;
			}
			break;

			case SDL_QUIT:
			leave_game();
			break;
		}
	}
}
