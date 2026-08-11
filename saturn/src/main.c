/*
 * Heart of The Alien: Game loop and main
 * Copyright (c) 2004-2005 Gil Megidish
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <assert.h>
#include <dirent.h>

#include "main.h"
#include "client.h"
#include "vm.h"
#include "disc_manifest.h"
#include "rooms.h"
#include "debug.h"
#include "sound.h"
#include "common.h"
#include "disc.h"
#include "decode.h"
#include "video.h"
#include "fadecalc.h"
#include "input.h"
#include "platform.h"
#include "screen.h"
#include "sprites.h"
#include "game2bin.h"
#include "animation.h"
#include "getopt.h"

static char *VERSION = "1.2.4";

static char *QUICKSAVE_FILENAME = "quicksave";
static char *RECORDED_KEYS_FILENAME = "recorded-keys";

typedef struct anm_file_s
{
	int track;
	const char *filename;
	int offset;
} anm_file_t;

static anm_file_t anm_files[] =
{
	{31, "INTRO1.BIN", 0},
	{32, "INTRO2.BIN", 0},
	{33, "INTRO3.BIN", 0},
	{34, "INTRO4.BIN", 0},
	{35, "MAKE2MB.BIN", 0x109a},
	{36, "MID2.BIN", 0},
	{37, "END1.BIN", 0},
	{38, "END2.BIN", 0},
	{39, "END3.BIN", 0},
	{40, "END4.BIN", 0}
};

///////
extern int script_ptr;

int next_script;
int current_backdrop;
int current_room;

int speed_throttle = 0;

int debug_flag = 0;
int test_flag = 0;
int record_flag = 0;
int replay_flag = 0;
int fullscreen_flag = 0;
int fastest_flag = 0;

short task_pc[64];
short new_task_pc[64];
short enabled_tasks[64];
short new_enabled_tasks[64];

int key_up, key_down, key_left, key_right, key_a, key_b, key_c, key_select;
int key_reset_record;

static unsigned int last_tick = 0;
static unsigned int last_tick_fp = 0;

#define RECORDED_KEYS_CACHE 4096
static int cached_keys_offset = 0;
static unsigned char cached_recorded_keys[RECORDED_KEYS_CACHE];

/** file descriptor where keys are written to, or read from */
FILE *record_fp = 0;

/** scratchpad used for unpacking code */
static unsigned char scratchpad[29184];


static int load_room(int index)
{
	char filename[16];
	unsigned char *ptr;

	strcpy(filename, "ROOMS0.BIN");
	filename[5] = (index + '0');

	LOG(("loading %s\n", filename));
	ptr = get_memory_ptr(ROOMS_LOAD_BASE);
	if (disc_read_file(filename, ptr, get_memory_size() - ROOMS_LOAD_BASE) < 0)
	{
		panic("load_room failed");
	}

	script_ptr = get_long(0xf900);
	LOG(("script ptr %x\n", script_ptr));

	sound_flush_cache();

	return 0;
}

/** atexit() callback
*/
static void atexit_callback(void)
{
	/* Stop before close, not the reverse: harmless on the host, since
	   disc_close only touches disc_data_fp/disc_root_dir and disc_stop_track
	   only touches disc_music_fp -- but disc.h's contract is written for a
	   Saturn backend too, where both halves share one CD drive and one
	   session. There, closing the disc first and then issuing a stop-track
	   command would be commanding CDDA playback after the drive session
	   that command depends on was already released -- a use-after-close
	   that the host cannot demonstrate but the seam must still get right. */
	disc_stop_track();
	disc_close();
	platform_quit();
}

/*----------------------
 | initialize
 | Description: Engine bring-up after the disc is open: the renderer, the two
 |   bulk buffers, GAME2.BIN and the first screen. platform_init() and the
 |   atexit registration used to head this function and now run in main()
 |   ahead of disc_open(), for the reason main()'s banner gives.
 | Author: suinevere
 ----------------------*/
static int initialize()
{
	if (video_init() < 0)
	{
		panic("failed to initialize renderer module");
	}

	/* Both buffers must exist before game2bin_init(), which is the first
	   thing in the engine that reads from the disc and therefore the first
	   thing that writes into either of them. On Saturn these are LWRAM
	   allocations that can genuinely fail; on the host they cannot. */
	if (!vm_alloc_memory())
	{
		panic("out of memory allocating the emulated 68000 map");
	}

	if (!game2bin_alloc())
	{
		panic("out of memory allocating the GAME2.BIN buffer");
	}

	if (game2bin_init() < 0)
	{
		panic("can't read GAME2.BIN file");
	}

	screen_init();

	vm_reset();
	set_variable(227, 1);

	if (video_create_surface() < 0)
	{
		panic("failed to create video surface");
	}

	return 0;
}

/** Loads a screen from room file
    @param room    suffix for room%d.bin file
    @param index   screen number
*/
void load_room_screen(int room, int index)
{
	int i;
	unsigned char *pixels;

	LOG(("loading room screen %d from room file %d\n", index - 1, room));

	unpack_room(scratchpad, index - 1);

	/* convert 4bpp -> 8bpp */
	pixels = (unsigned char *) get_screen_ptr(0);
	for (i=0; i<304*192/2; i++)
	{
		pixels[i*2+0] = scratchpad[i] >> 4;
		pixels[i*2+1] = scratchpad[i] & 0xf;
	}

	current_backdrop = index;
}

/** Rewinds (clears) the recorded-keys cache
*/
void rewind_recorded_keys()
{
	cached_keys_offset = 0;
}

/** Writes down all the recored keys, and rewinds
*/
void flush_recorded_keys()
{
	fwrite(cached_recorded_keys, 1, cached_keys_offset, record_fp);
	cached_keys_offset = 0;
}

/** Reads keystate from record file

    Will read information sufficient for one rendered frame; if the
    record file ended already, all keys will be considered 'released'
*/
void read_keys_from_record()
{
	int c = fgetc(record_fp);

	if (c == EOF)
	{
		c = 0;
		LOG(("ERROR: record file ended!\n"));
	}

	key_up = (c >> 7) & 1;
	key_down = (c >> 6) & 1;
	key_left = (c >> 5) & 1;
	key_right = (c >> 4) & 1;
	key_a = (c >> 3) & 1;
	key_b = (c >> 2) & 1;
	key_c = (c >> 1) & 1;
	key_select = (c >> 0) & 1;
}

/** Adds a single key to the record file

    Adds a key to the cache array of recorded keys; if the array
    is full, it will force flushing
*/
void add_keys_to_record()
{
	int c;

	c = (key_up << 7) | (key_down << 6);
	c = c | (key_left << 5) | (key_right << 4);
	c = c | (key_a << 3) | (key_b << 2);
	c = c | (key_c << 1) | key_select;

 	cached_recorded_keys[cached_keys_offset++] = c;
 	if (cached_keys_offset == sizeof(cached_recorded_keys))
	{
 		/* if the player never quicksaves or quickloads */
		flush_recorded_keys();
	}
}

/** Translates gamepad presses and updates variables

    Entry at b6c4
*/
void update_keys()
{
	short flags;

	toggle_aux(0);           /* me and my paranoia */
	set_variable(253, 0);
	set_variable(252, 0);
	set_variable(229, 0);
	set_variable(251, 0);

	flags = 0;

	if (key_right)
	{
		set_variable(252, 1);
		flags |= 1;
	}
	else if (key_left)
	{
		set_variable(252, -1);
		flags |= 2;
	}
	else if (key_down)
	{
		set_variable(251, 1);
		set_variable(229, 1);
		flags |= 4;
	}

	if (key_up)
	{
		set_variable(229, -1);
		flags |= 8;
	}

	if (key_c)
	{
		set_variable(251, -1);
		flags |= 8;
		/* some if  here! */
	}

	set_variable(253, flags);

	/* b748 */
	set_variable(250, 0);
	set_variable(254, get_variable(253));

	if (key_b)
	{
		set_variable(254, (unsigned short)(get_variable(254) | 0x40));
	}
	else if (key_a)
	{
		set_variable(250, 1);
		set_variable(254, (unsigned short)(get_variable(254) | 0x80));
	}
}

/** Loads a quicksave file
*/
void quickload()
{
	int i, j;
	int palette_used;
	FILE *fp;

	fp = fopen(QUICKSAVE_FILENAME, "rb");
	if (fp == NULL)
	{
		perror("failed to load 'quicksave' file\n");
		return;
	}

	current_room = fgetc(fp);
	current_backdrop = fgetc(fp);
	palette_used = fgetc(fp);

	load_room(current_room);
	load_room_screen(0, current_backdrop);
	video_set_palette(palette_used);

	/* must be ran out of thread loop, so no active thread */
	toggle_aux(0);
	for (i=0; i<256; i++)
	{
		set_variable(i, fgetw(fp));
	}

	toggle_aux(1);
	for (j=0; j<MAX_TASKS; j++)
	{
		set_aux_bank(j);
		for (i=0; i<32; i++)
		{
			set_variable(i, fgetw(fp));
		}
	}

	/* when frame ends, aux is always zero anyway */
	toggle_aux(0);

	for (i=0; i<MAX_TASKS; i++)
	{
		task_pc[i] = fgetw(fp);
		new_task_pc[i] = fgetw(fp);
		enabled_tasks[i] = fgetw(fp);
		new_enabled_tasks[i] = fgetw(fp);
	}

	quickload_sprites(fp);

	fclose(fp);
}

/** Creates a quicksave file

    Quicksave file includes all that is required so later on a user can
    use the quickload and be provided with the exact same game state.
*/
void quicksave()
{
	int i, j;
	FILE *fp;

	fp = fopen(QUICKSAVE_FILENAME, "wb");
	if (fp == NULL)
	{
		perror("failed to create 'quicksave' file\n");
		return;
	}

	fputc(current_room, fp);
	fputc(current_backdrop, fp);
	fputc(video_get_current_palette(), fp);

	/* must be ran out of thread loop, so no active thread */
	toggle_aux(0);
	for (i=0; i<256; i++)
	{
		fputw(get_variable(i), fp);
	}

	toggle_aux(1);
	for (j=0; j<MAX_TASKS; j++)
	{
		set_aux_bank(j);
		for (i=0; i<32; i++)
		{
			fputw(get_variable(i), fp);
		}
	}

	toggle_aux(0);

	for (i=0; i<MAX_TASKS; i++)
	{
		fputw(task_pc[i], fp);
		fputw(new_task_pc[i], fp);
		fputw(enabled_tasks[i], fp);
		fputw(new_enabled_tasks[i], fp);
	}

	quicksave_sprites(fp);
	fclose(fp);
}

void leave_game()
{
	flush_recorded_keys();
	exit(0);
}


void rest(int fps)
{
	if (fastest_flag == 0)
	{
		if (fps == 0)
		{
			last_tick = platform_ticks();
			last_tick_fp = 0;
			return;
		}

		if (speed_throttle == 1)
		{
			/* 10 times faster */
			fps = fps*10;
		}

		unsigned int diff = ((1000 << 16) / fps) + last_tick_fp;
		last_tick_fp = diff & 0xffff;
		diff = diff >> 16;
		unsigned int current_tick = platform_ticks();
		while (current_tick - last_tick < diff)
		{
			platform_delay(1);
			current_tick = platform_ticks();
		}
		last_tick += diff;
	}
}

/*----------------------
 | FADE_HOLD_MS
 | Description: How long one step of a fade stays on screen. Eight steps at
 |   60 ms is a fade just under half a second, and the hold is the part worth
 |   keeping: the fade shows eight distinct pictures rather than a smooth
 |   per-frame ramp, which is a different effect belonging to a different
 |   console.
 |
 |   250 ms was tried first, on the reasoning that the original's fade spans
 |   about two seconds. On this port it read as far too slow, and the reason
 |   is that the fade is not carrying what the original's was: there, the
 |   fade is sized to the load happening behind it. Here the load is a
 |   separate black screen that follows, so a fade stretched to cover it just
 |   delays the black. This is the transition alone; the waiting is the
 |   read's and disc_wait_for_music's, and neither needs the palette to move
 |   while it happens.
 |
 |   The one number to change if the fade still feels wrong. Eight steps at
 |   this hold is 480 ms; the step count lives in fadecalc.h.
 | Author: suinevere
 ----------------------*/
#define FADE_HOLD_MS 60

/*----------------------
 | fade_walk
 | Description: Walks the fade ladder from one step to another inclusive,
 |   holding each for FADE_HOLD_MS and presenting frames the whole way, then
 |   resets rest()'s baseline.
 |
 |   That reset is not housekeeping. rest() paces the game by comparing
 |   platform_ticks() against last_tick, and a fade returns two seconds after
 |   last_tick was current, so without rest(0) the loop reads itself as two
 |   seconds behind and sprints to catch up -- the same speed wobble that
 |   kept cdda_wait_for_sound out of disc_play_track. rest(0) sets last_tick
 |   to now and clears the fractional carry, which is exactly the baseline a
 |   long block needs on the way out.
 |
 |   The inner wait calls platform_frame() rather than only platform_delay(),
 |   so the VDP2 layer is presented and peripherals refreshed while the fade
 |   runs. A fade that froze the display would be a black screen either way
 |   on the last step, and a locked-up one on the first.
 | Author: suinevere
 | Dependencies: video.h, fadecalc.h, platform.h
 | Globals: N/A
 | Params: from -- first step to show; to -- last step to show; both 0
 |   (normal) to FADECALC_SEGA_CD_STEPS (black)
 | Returns: N/A
 ----------------------*/
/*----------------------
 | g_fadeStart / g_fadeActive / g_fadeStep / g_fadeHold
 | Description: State for a fade that is running underneath disc work rather
 |   than in front of it. g_fadeStart is when it began, g_fadeStep the last
 |   level actually written -- kept so fade_pump can skip the CRAM write when
 |   the schedule has not moved on, which matters because it is called from
 |   inside a read loop.
 |
 |   g_fadeHold is this fade's per-step hold, set at every fade_out_begin and
 |   left alone by everything else, so a fade asked to last longer stretches
 |   rather than gaining steps: the same eight pictures, held longer, which is
 |   the effect FADE_HOLD_MS's banner is about.
 | Author: suinevere
 ----------------------*/
static unsigned int g_fadeStart = 0;
static int g_fadeActive = 0;
static int g_fadeStep = 0;
static unsigned int g_fadeHold = FADE_HOLD_MS;

/*----------------------
 | fade_pump
 | Description: Advances the fade to whatever step elapsed time says it
 |   should be on, and writes it. Scheduled on the clock rather than counted
 |   per call, because its callers cannot promise how often they will call
 |   it: a read hands control back twice for a whole animation, while the
 |   music wait polls every frame. Counting steps would make the fade's speed
 |   depend on which one happened to be running.
 |
 |   Writes CRAM and nothing else -- no frame is presented and none is
 |   needed, since VDP2 reads the palette continuously, which is what lets
 |   this be called from inside a disc read where the game loop is not
 |   running.
 |
 |   Installed as the disc layer's tick, so it is invoked from a context that
 |   must not touch the disc. It does not.
 | Author: suinevere
 | Globals: g_fadeStart, g_fadeActive, g_fadeStep
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void fade_pump(void)
{
	unsigned int elapsed;
	int step;

	if (!g_fadeActive)
	{
		return;
	}

	elapsed = platform_ticks() - g_fadeStart;
	step = (int)(elapsed / g_fadeHold) + 1;

	if (step > FADECALC_SEGA_CD_STEPS)
	{
		step = FADECALC_SEGA_CD_STEPS;
	}

	if (step != g_fadeStep)
	{
		g_fadeStep = step;
		video_set_fade(fadecalc_step_level(step, FADECALC_SEGA_CD_STEPS));
	}
}

void fade_out_begin(void)
{
	fade_out_begin_hold(FADE_HOLD_MS);
}

void fade_out_begin_hold(unsigned int hold_ms)
{
	if (hold_ms < 1)
	{
		hold_ms = 1;
	}

	g_fadeHold = hold_ms;

	/* Starts at step 1, not 0: step 0 is the undimmed picture already on
	   screen, and re-writing it would spend a step of the fade showing the
	   viewer what they are already looking at. */
	g_fadeStart = platform_ticks();
	g_fadeActive = 1;
	g_fadeStep = 0;

	disc_set_tick(fade_pump);
	fade_pump();
}

void fade_out_finish(void)
{
	/* Whatever time the disc work took, the fade ends black. If that work
	   outlasted the fade this has already been reached and the remaining
	   wait happened behind black, which is the intended shape; if the work
	   was shorter, this spends what little is left rather than cutting a
	   half-faded picture to black. */
	while (g_fadeStep < FADECALC_SEGA_CD_STEPS)
	{
		fade_pump();
		platform_frame();
		platform_delay(1);
	}

	disc_set_tick(NULL);
	g_fadeActive = 0;
	rest(0);
}

/** Initializes all tasks to stopped state
*/
void init_tasks()
{
	int i;

	for (i=0; i<MAX_TASKS; i++)
	{
		task_pc[i] = -1;
		new_task_pc[i] = -1;

		enabled_tasks[i] = 0;
		new_enabled_tasks[i] = 0;
	}

	toggle_aux(0);
	task_pc[0] = 0;
}

/** Plays several animations at once
    @param anm        pointer to an array of anm_file_t entries
    @param n          number of elements in array
    @param skippable  skip to next animation if key pressed (otherwise breaks)
    @returns zero on completion of all videos, 1 if skipped, negative on error

    Note that cls.quit might be true; in that case, return value of one will
    be returned
*/
int play_anm(anm_file_t *anm, int n, int skippable)
{
	int seq;
	int ret;

	ret = 0;
	for (seq = 0; seq < n; seq++)
	{
		if (cls.quit == 0)
		{
			int ok;

			ok = play_animation(anm[seq].filename, anm[seq].offset, anm[seq].track);
			if (ok < 0)
			{
				ret = ok;
				break;
			}

			if (ok == 1 && skippable == 0)
			{
				ret = 1;
				break;
			}
		}
	}

	disc_stop_track();
	return ret;
}

/** Plays the introduction to the game

    Introduction is split into 4 files, since sega-cd was limited with
    512 KB of ram, and video is loaded into memory before it can be
    played. also, each such sequence comes with it's own audio track
*/
static void play_intro()
{
	play_anm(anm_files, 4, 0);
}

/** Main game loop

    This is where all the magic happens!
*/
static void run()
{
	cls.quit = 0;
	init_tasks();

	if (next_script == 0)
	{
		/* if no room specified, then follow the original flow:
		 * first play intro, then jump to code entry script.
		 */
		play_intro();
		next_script = 7;
	}

	rest(0);

	while (cls.quit == 0)
	{
		int i;

		if (next_script != 0)
		{
			current_room = next_script;
			reset_sprite_list();
			init_tasks();
			LOG(("loading room %d\n", current_room));
			load_room(current_room);
			next_script = 0;
		}

		check_events();

		if (replay_flag)
		{
			read_keys_from_record();
		}

		update_keys();

		if (record_flag)
		{
			add_keys_to_record();
		}

		LOG(("*new frame*\n"));

		for (i=0; i<MAX_TASKS; i++)
		{
			int d0;

			/* 70d0 */
			enabled_tasks[i] = new_enabled_tasks[i];

			d0 = new_task_pc[i];
			if (d0 == -1)
			{
				continue;
			}

			if (d0 == -2)
			{
				d0 = -1;
			}

			task_pc[i] = d0;
			new_task_pc[i] = -1;
		}

		for (i=0; i<MAX_TASKS; i++)
		{
			int pc = task_pc[i];
			if (pc != INVALID_PC && enabled_tasks[i] == 0)
			{
				toggle_aux(0);
				set_aux_bank(i);
				LOG(("task %d starts at 0x%x\n", i, pc));
				task_pc[i] = decode(i, pc);
				LOG(("task %d ended at 0x%x\n", i, pc));
			}

			if (next_script != 0)
			{
				/* script has been changed */
				break;
			}
		}

		rest(12);
		platform_frame();
	}
}

/** Runs the animation-test, enabled with --animation-test
*/
static void animation_test()
{
	int files = sizeof(anm_files) / sizeof(anm_files[0]);
	play_anm(anm_files, files, 1);
}

/** Runs the sprite-test

    Interactively show the sprites in this 'room' file. Use the --room
    parameter to view sprites in other rooms
*/
void sprite_test()
{
	int redraw;

	load_room(next_script);

	sprites[0].index = 0;
	sprites[0].frame = 0;
	sprites[0].x = 10;
	sprites[0].y = 10;

	cls.quit = 0;

	redraw = 1;
	video_set_palette(0x11);
	rest(0);
	while (cls.quit == 0)
	{
		int a4;

		a4 = get_long(0xf904) + (sprites[0].index << 2);
		a4 = get_long(a4);

		if (redraw)
		{
			int selected_screen;
			void *background;

			selected_screen = get_selected_screen();
			background = get_screen_ptr(selected_screen);
			memset(background, 0xff, 304*192);

			render_sprite(0);
			video_render(background);
			redraw = 0;
			print_sprite(0);
		}

		check_events();
		rest(12);

		update_keys();

		if (get_variable(252) == 1)
		{
			int i = (sprites[0].frame & 0x7f) + 1;
			sprites[0].frame = (sprites[0].frame & 0x80) | i;
			if (sprites[0].frame > get_byte(a4))
			{
				sprites[0].frame &= 0x80;
			}

			redraw = 1;
		}

		if (get_variable(252) == -1)
		{
			if (sprites[0].frame > 0)
			{
				int i = (sprites[0].frame & 0x7f) - 1;
				redraw = 1;
				sprites[0].frame = (sprites[0].frame & 0x80) | i;
			}
		}

		if (get_variable(229) == 1)
		{
			redraw = 1;
			sprites[0].index++;
			sprites[0].frame &= 0x80;
		}

		if (get_variable(229) == -1)
		{
			if (sprites[0].index > 0)
			{
				redraw = 1;
				sprites[0].index--;
				sprites[0].frame &= 0x80;
			}
		}

		if (get_variable(250))
		{
			/* flip */
			sprites[0].frame ^= 0x80;
		}

		set_variable(229, 0);
		set_variable(252, 0);
	}
}

#ifndef HOTA_SATURN
static void help()
{
	printf("Heart of The Alien Redux %s\n", VERSION);
	puts("USAGE: alien [OPTIONS] [.cue-file]");
	puts("");
	puts("OPTIONS:");
	#ifdef ENABLE_DEBUG
	puts("\t--debug        turn on debugging");
	#endif
	puts("\t--double       double size window (608 x 384)");
	puts("\t--triple       triple size window (912 x 576)");
	puts("\t--scale=[2|3]  rescale using scale2x or scale3x filters");
	puts("\t--fullscreen   start in fullscreen");
	puts("\t--room n       start from a different room");
	puts("\t--sprite-test  run sprite test (use with room)");
	puts("\t--intro-test   play all animations");
	puts("\t--fastest      speed throttle");
	puts("\t--record       record keys");
	puts("\t--replay       replay keys");
	puts("\t--help         this help");
	puts("");
	puts("ARGUMENTS:");
	puts("\t.cue-file      disc cue sheet (default: alphabetically-first .cue in");
	puts("\t               ./cd/, resolved against the current working directory)");
}

/*----------------------
 | find_cue_path
 | Description: When no cue path is given on the command line, falls back to
 |   the *.cue found directly under ./cd/ (resolved against the process's
 |   current working directory, not this executable's location) -- the
 |   layout the disc rip is normally dropped into. If more than one .cue is
 |   present, picks the alphabetically-first name rather than whatever
 |   readdir happens to return first: readdir order is unspecified, and a
 |   fallback that silently picked a different disc on a different run (or a
 |   different OS/filesystem) would be a much worse surprise than picking a
 |   fixed, predictable one. Returns a pointer into a static buffer (valid
 |   until the next call), or NULL if cd/ doesn't exist or holds no cue.
 | Author: suinevere
 ----------------------*/
static const char *find_cue_path(void)
{
	static char found[512];
	static char best_name[508]; /* leaves room for the "cd/" prefix + NUL in found[] --
	                                sized so gcc's -Wformat-truncation can prove the
	                                snprintf below always fits, not just usually does */
	DIR *dir;
	struct dirent *entry;
	int have_best = 0;

	dir = opendir("cd");
	if (dir == NULL)
	{
		return NULL;
	}

	while ((entry = readdir(dir)) != NULL)
	{
		size_t name_len = strlen(entry->d_name);

		if (name_len > 4 && strcmp(entry->d_name + name_len - 4, ".cue") == 0)
		{
			if (!have_best || strcmp(entry->d_name, best_name) < 0)
			{
				snprintf(best_name, sizeof(best_name), "%s", entry->d_name);
				have_best = 1;
			}
		}
	}

	closedir(dir);

	if (!have_best)
	{
		return NULL;
	}

	snprintf(found, sizeof(found), "cd/%s", best_name);
	printf("no disc cue file given, using '%s'\n", found);
	fflush(stdout);
	return found;
}

static struct option options[] =
{
	{"debug", no_argument, 0, 'd'},
	{"room", required_argument, 0, 'r'},
	{"sprite-test", no_argument, &test_flag, 1},
	{"intro-test", no_argument, &test_flag, 2},
	{"help", no_argument, 0, 'h'},
	{"no-sound", no_argument, 0, 'n'},
	{"fullscreen", no_argument, &fullscreen_flag, 1},
	{"record", no_argument, &record_flag, 1},
	{"replay", no_argument, &replay_flag, 1},
	{"double", no_argument, 0, '2'},
	{"triple", no_argument, 0, '3'},
	{"scale", required_argument, 0, 's'},
	{"fastest", no_argument, &fastest_flag, 1},
	{0, no_argument, 0, 0}
};
#endif /* !HOTA_SATURN */

/*----------------------
 | main
 | Description: Entry point. Parses the command line, opens the disc, brings
 |   the platform up and runs the game or one of the two test modes.
 |
 |   Everything above between #ifndef HOTA_SATURN and its #endif -- help(),
 |   find_cue_path() and the long-option table -- plus the getopt_long loop
 |   below is host-only, and gated out rather than merely unused. SRL enters
 |   at `int main()` with no arguments at all (see SaturnRingLib/Samples), so
 |   argc and argv here name registers the startup never wrote; and opendir()
 |   is a stub that always fails (saturn_filestub.c), so find_cue_path() would
 |   return NULL and panic on "no disc cue file given". A Saturn has one
 |   mounted disc and no cue sheet, which is why disc_open's cue_path
 |   parameter is documented as accepted-and-ignored in disc_srl.cxx -- the
 |   literal passed below is never read.
 |
 |   platform_init() and the atexit registration moved here out of
 |   initialize() so that they precede disc_open(). On Saturn platform_init is
 |   SRL::Core::Initialize(), which must run before the CD, the VDP2 layer or
 |   the first malloc; on the host it is SDL_Init plus the audio-device
 |   negotiation, which disc.h names as the CD-DA path's precondition, so
 |   running it before the disc opens is right on both backends. It stays
 |   after option parsing because it reads cls.nosound.
 | Author: suinevere
 ----------------------*/
int main(int argc, char **argv)
{
#ifndef HOTA_SATURN
	int options_index;
#endif
	const char *cue_path;

	next_script = 0;

	cls.scale = 1;
	cls.filtered = 0;
	cls.fullscreen = 0;
	cls.speed_throttle = 0;
	cls.paused = 0;
	cls.nosound = 0;

#ifndef HOTA_SATURN
	options_index = 0;
	while (1)
	{
		int c = getopt_long(argc, argv, "hdr:23s:", options, &options_index);
		if (c == -1)
		{
			/* no more options */
			break;
		}

		switch(c)
		{
			/* won't do a thing if turned on without ENABLE_DEBUG */
			case 'd':
			debug_flag = 1;
			break;

			case 'r':
			next_script = atoi(optarg);
			break;

			case 'h':
			help();
			return 0;

			case '2':
			cls.scale = 2;
			break;

			case '3':
			cls.scale = 3;
			break;

			case 's':
			cls.scale = atoi(optarg);
			if (cls.scale != 2 && cls.scale != 3)
			{
				panic("invalid scaler (either 2 or 3)");
				return 1;
			}

			cls.filtered = 1;
			break;

			case 'n':
			cls.nosound = 1;
			break;

			case '?':
			/* invalid argument */
			return 1;
		}
	}

	if (replay_flag && record_flag)
	{
		fprintf(stderr, "cant specify both replay and record\n");
		return 1;
	}

	if (replay_flag)
	{
		record_fp = fopen(RECORDED_KEYS_FILENAME, "rb");
	}
	else if (record_flag)
	{
		record_fp = fopen(RECORDED_KEYS_FILENAME, "wb");
	}

	/* Positional argument after option parsing, or fall back to searching
	   the cd directory for a cue sheet -- disc_open must run before
	   initialize()'s game2bin_init() call, the first thing that reads
	   from the disc. */
	cue_path = (optind < argc) ? argv[optind] : find_cue_path();
	if (cue_path == NULL)
	{
		panic("no disc cue file given, and none found under cd/*.cue");
	}
#else
	(void)argc;
	(void)argv;
	cue_path = "cd";
#endif

	if (!platform_init())
	{
		panic("platform_init failed\n");
	}
	atexit(atexit_callback);

	if (!disc_open(cue_path))
	{
		panic("failed to open disc");
	}

	initialize();

	switch(test_flag)
	{
		case 0:
		run();
		break;

		case 1:
		sprite_test();
		break;

		case 2:
		animation_test();
		break;

		default:
		fprintf(stderr, "unknown test_flag %d\n", test_flag);
		return 1;
	}

	if (record_fp != NULL)
	{
		flush_recorded_keys();
		fclose(record_fp);
	}

	return 0;
}
