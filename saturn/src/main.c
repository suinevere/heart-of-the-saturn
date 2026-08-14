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
#include "bootmenu.h"
#ifdef HOTA_SATURN
#include "system/saturn_bootart.h"
#include "system/saturn_backup.h"
#include "system/saturn_saveslot.h"
#include "menus/menu.h"
#endif

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

/*----------------------
 | ending_played
 | Description: Set by decode.c when the four ending animations have played and
 |   it is about to ask for room 7. It is the one arrival at the password screen
 |   the menu driver cannot infer for itself -- a death and an ending reach that
 |   room by the same route and look identical from there -- and it exists so a
 |   player who has just watched the credits gets the sub-title menu rather than
 |   a list of saves to reload.
 |
 |   Consumed by menu_gate, which clears it as it acts on it, so it means "this
 |   arrival" and not "this playthrough".
 |
 |   An int beside next_script rather than a call into menus/, because that
 |   would put a Saturn header and an #ifdef into decode.c for one bit.
 | Author: suinevere
 ----------------------*/
int ending_played;

/*----------------------
 | death_played
 | Description: Set by animation.c when a terminal death sequence has faded to
 |   black, and consumed by menu_gate, which clears it and opens the load screen
 |   rather than the sub-title menu -- a player who has just died wants their
 |   saves, not a fresh start.
 |
 |   It exists because a death is invisible to every other signal the menu layer
 |   has. An ending changes rooms and can be spotted by the gate's next_script
 |   test; a death changes no room at all. Its script is one or two 0x21 opcodes
 |   and then DESTROY_TASKS, and destroy_tasks kills only a range of tasks, so a
 |   task that survives goes on to draw the original password screen inside the
 |   room the player died in. Nothing in that sequence asks for room 7, so the
 |   gate never sees it and the screen this branch exists to replace comes up
 |   anyway. This flag is what decode.c tests to route the death to the gate
 |   instead: on the opcode after a death it sets next_script to 7 and ends the
 |   script. Ending it is what stops the surviving task ever drawing; next_script
 |   is what breaks run()'s task loop and reaches the top of the frame, the one
 |   place where quickload's "no active thread" precondition holds and so the one
 |   place a load may run.
 |
 |   That test in decode.c is the one thing here inside #ifdef HOTA_SATURN, and
 |   it is guarded where ending_played's store is not because it is not inert
 |   without a menu. ending_played writes an int the host build never reads; this
 |   ends the script and asks for another room, so on a host build -- which has
 |   no menu_gate -- it would load ROOMS7.BIN where the original draws the
 |   password display in place. video.h calls the SDL backend the reference a
 |   wrong Saturn frame is compared against, and a reference whose death path
 |   diverges is worth less. HOTA_SATURN is a bare define from saturn/makefile,
 |   so the guard costs no include and does not reopen the rule it looks like it
 |   breaks.
 |
 |   The screen_arm_fade_restore beside that guard is deliberately outside it.
 |   The two statements answer different questions. Whether a death ends the
 |   script is a menu question and belongs to builds that have a menu; whether
 |   the screen comes back is not optional anywhere, because the fades on this
 |   path run on every build. Guarding the arm with the routing was the mistake
 |   that took the host build's intro, ending and every death to a permanently
 |   black screen -- the fade compiled, the thing that undoes it did not.
 |   Anything that fades and then hands control to the game arms
 |   unconditionally; see screen.h. A terminal death now ends lit rather than
 |   black, so on that path the arm has nothing left to undo, and it stays for
 |   the aborted and chained ones and because the rule has no exceptions.
 |
 |   This definition and animation.c's store are unguarded too: an int nobody
 |   reads costs the host nothing, and keeping the guard to the single place
 |   that changes control flow is what makes it obvious why it is there.
 |
 |   Cleared by animation.c on entry to every death as well as by the gate, so
 |   it always means "the death that just finished" and can never be left set
 |   from a previous one to end a two-segment death between its halves.
 |
 |   An int beside ending_played rather than a call into menus/, for the same
 |   reason: that would put a Saturn header and an #ifdef into decode.c for one
 |   bit.
 | Author: suinevere
 ----------------------*/
int death_played;

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
 | g_fadeSilent
 | Description: Set when the screen was already black as this fade was armed,
 |   which makes every level the ladder would write a level brighter than what
 |   is on screen. A fade out must only ever darken, so a silent fade keeps its
 |   schedule and writes nothing.
 |
 |   Without it a fade begun on black lights the screen back up: fade_pump's
 |   first write is step 1, seven eighths of full brightness, so the outgoing
 |   frame the last fade just hid would flash back into view and then walk down
 |   the ladder again. That is the same stale-frame flash the deferred restores
 |   exist to prevent, arriving from the other direction.
 | Author: suinevere
 ----------------------*/
static int g_fadeSilent = 0;

/*----------------------
 | g_fadeCeiling
 | Description: The level on screen when this fade was armed. fade_pump writes
 |   nothing brighter, so the ladder's opening steps are skipped rather than
 |   played on a screen that is already darker than they are.
 |
 |   g_fadeSilent is the special case of this where the ceiling is 0, and it
 |   stays a flag of its own because it also decides whether the fade may finish
 |   early. The general case arrived with the cutscene fade in: a sequence
 |   skipped inside its first half second is still part-way up the ramp, and a
 |   fade out that started from step 1 would brighten it back toward full before
 |   taking it down again.
 | Author: suinevere
 ----------------------*/
static int g_fadeCeiling = FADECALC_LEVEL_NORMAL;

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
		int level = fadecalc_step_level(step, FADECALC_SEGA_CD_STEPS);

		g_fadeStep = step;

		if (!g_fadeSilent && level < g_fadeCeiling)
		{
			video_set_fade(level);
		}
	}
}

/*----------------------
 | fade_out_arm
 | Description: The body both fade_out_begin forms share: install the tick,
 |   start the clock, and decide what an already-black screen means for this
 |   particular fade.
 |
 |   It always means "write nothing" -- see g_fadeSilent. Whether it also means
 |   "take no time" is the caller's to say, and it is the whole difference
 |   between the two entry points. A plain fade out is a transition, so on a
 |   screen that is already black it has nothing to transition and may finish
 |   at once. An explicit hold is a duration someone measured, and the one
 |   caller that asks for one is the two-segment death, whose 960 ms is holding
 |   the splat in place; short-circuiting that would silently retune the one
 |   number in this port that was settled by eye over six rounds. So the hold
 |   form spends its time whatever is on screen, and merely does it quietly.
 |
 |   The tick is installed on both paths, including the one that has already
 |   finished, so fade_out_finish uninstalls exactly what was installed no
 |   matter which way the fade went.
 |
 |   Any deferred fade in screen.c is holding is cancelled first. A fade out is
 |   a statement about the same palette a pending ramp is walking, and the two
 |   would otherwise write alternating levels for as long as both ran.
 | Author: suinevere
 | Dependencies: video.h, fadecalc.h, platform.h, disc.h, screen.h
 | Globals: g_fadeHold, g_fadeStart, g_fadeActive, g_fadeStep, g_fadeSilent,
 |   g_fadeCeiling
 | Params: hold_ms -- milliseconds per step, clamped to at least 1;
 |   free_when_black -- non-zero to also skip the wait on an already-black
 |   screen, zero to spend the full duration regardless
 | Returns: N/A
 ----------------------*/
static void fade_out_arm(unsigned int hold_ms, int free_when_black)
{
	if (hold_ms < 1)
	{
		hold_ms = 1;
	}

	screen_fade_cancel();
	g_fadeHold = hold_ms;

	/* Starts at step 1, not 0: step 0 is the undimmed picture already on
	   screen, and re-writing it would spend a step of the fade showing the
	   viewer what they are already looking at. */
	g_fadeStart = platform_ticks();
	g_fadeActive = 1;
	g_fadeStep = 0;
	g_fadeCeiling = video_get_fade();
	g_fadeSilent = (g_fadeCeiling <= 0);

	if (g_fadeSilent && free_when_black)
	{
		g_fadeStep = FADECALC_SEGA_CD_STEPS;
		g_fadeActive = 0;
	}

	disc_set_tick(fade_pump);
	fade_pump();
}

void fade_out_begin(void)
{
	fade_out_arm(FADE_HOLD_MS, 1);
}

void fade_out_begin_hold(unsigned int hold_ms)
{
	fade_out_arm(hold_ms, 0);
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

void play_intro(void)
{
	play_anm(anm_files, 4, 0);
}

/*----------------------
 | saturn_save_poll
 | Description: Runs the pause menu, at the top of a frame and outside the task
 |   loop. This position -- between check_events() and the task loop's first
 |   toggle_aux(0) -- is the only place in the program where quicksave's and
 |   quickload's "no active thread" precondition holds, and it is the save
 |   point already verified on real hardware. That is why the function survives
 |   the menu layer replacing its body rather than the call moving into menus/.
 | Author: suinevere
 | Dependencies: menus/menu.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#ifdef HOTA_SATURN
static void saturn_save_poll(void)
{
	menu_pause_poll();
}
#endif

/** Main game loop

    This is where all the magic happens!

    The screen_arm_fade_restore after play_intro is unguarded on purpose. The
    cinematic fades to black on every build, so the thing that lifts that black
    has to exist on every build too. On Saturn the gate arms again a moment
    later and the second arm is a no-op; without a gate this is the only arm on
    the path, and the alternative is a game running behind a screen that never
    comes back. See screen.h.
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
		screen_arm_fade_restore();
		next_script = 7;
	}

	rest(0);

	while (cls.quit == 0)
	{
		int i;

#ifdef HOTA_SATURN
		if (next_script == MENU_PASSWORD_ROOM)
		{
			next_script = menu_gate();
		}
#endif

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

#ifdef HOTA_SATURN
		saturn_save_poll();
#endif

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

#ifdef HOTA_SATURN
/*----------------------
 | boot_key_mask
 | Description: Folds input.h's eight key globals into a BOOT_KEY_* mask.
 | Author: suinevere
 | Dependencies: input.h, bootmenu.h
 | Globals: key_up, key_down, key_left, key_right, key_a, key_b, key_c,
 |          key_select
 | Params: N/A
 | Returns: The mask of keys currently held
 ----------------------*/
static uint32_t boot_key_mask(void)
{
	uint32_t mask = 0u;

	if (key_up)     mask |= BOOT_KEY_UP;
	if (key_down)   mask |= BOOT_KEY_DOWN;
	if (key_left)   mask |= BOOT_KEY_LEFT;
	if (key_right)  mask |= BOOT_KEY_RIGHT;
	if (key_a)      mask |= BOOT_KEY_A;
	if (key_b)      mask |= BOOT_KEY_B;
	if (key_c)      mask |= BOOT_KEY_C;
	if (key_select) mask |= BOOT_KEY_SELECT;

	return mask;
}

/*----------------------
 | boot_fade_out
 | Description: Walks the boot artwork down to black, holding each step for
 |   FADE_HOLD_MS and redrawing the same screen the whole way.
 |
 |   It has to be its own ladder rather than the fade_out_begin pair, because
 |   the two fades act on different hardware. The boot screens are VDP1 sprites
 |   with their own CRAM banks; video_set_fade only reaches the VDP2 palette the
 |   engine's bitmap layer draws through, which is switched off for the whole of
 |   boot_art_load's tenure. A fade out here that only called video_set_fade
 |   would leave the menu on screen at full brightness and dim nothing at all.
 |
 |   The redraw inside the wait is not optional. VDP1's command list is rebuilt
 |   from scratch every present, so a frame that queues nothing shows nothing --
 |   skipping the draw would make the fade a flicker rather than a dim.
 |
 |   The CD-DA level walks down with the picture. SND_SetCdDaLev takes 0..7, so
 |   the eight fade steps are exactly the eight the volume has -- the track is
 |   silent on the same step the screen reaches black, and the disc_stop_track
 |   the caller does next has nothing audible left to cut.
 |
 |   rest(0) on the way out for the reason every long block in this file owes
 |   it: the frame pacer measures from last_tick, and half a second spent here
 |   would otherwise be sprinted through afterwards.
 | Author: suinevere
 | Dependencies: saturn_bootart.h, fadecalc.h, platform.h, disc.h
 | Globals: N/A
 | Params: screen -- the boot_screen to keep drawing; highlight -- the
 |   boot_entry to keep drawing
 | Returns: N/A
 ----------------------*/
static void boot_fade_out(int screen, int highlight)
{
	int step;

	for (step = 1; step <= FADECALC_SEGA_CD_STEPS; step++)
	{
		unsigned int start = platform_ticks();

		boot_art_fade(fadecalc_step_level(step, FADECALC_SEGA_CD_STEPS));
		disc_set_music_volume((uint8_t)(BOOT_VOLUME_MAX
			- (step * BOOT_VOLUME_MAX) / FADECALC_SEGA_CD_STEPS));

		do
		{
			boot_art_draw(screen, highlight);
			boot_art_present();
		}
		while (platform_ticks() - start < FADE_HOLD_MS);
	}

	rest(0);
}

/*----------------------
 | boot_sequence
 | Description: Runs the opening stills and the game-select menu, returning
 |   when the player starts Heart of the Alien. Sits between initialize() and
 |   run() so the disc reads it needs are already done and the drive is free
 |   for CD-DA.
 |
 |   Returns immediately if the artwork will not load: a missing decoration
 |   must never brick the disc, so a build without the TGAs boots into the
 |   game instead of hanging on a black screen.
 |
 |   One frame is drawn and presented before the pad is ever sampled, and this
 |   ordering is the whole defence against a button held at power-on skipping
 |   the opening. check_events() only reads SRL's peripheral array; the thing
 |   that refreshes it is Core::Synchronize, reached solely through
 |   boot_art_present (srl_core.hpp:125). Until that first refresh, port 0
 |   holds its static initialiser 0xff, which reads as not-connected, and
 |   check_events zeroes every key. Priming from that would capture a
 |   synthetic zero rather than the pad, so the first genuine sample would
 |   arrive with a stale zero behind it and report every held button as newly
 |   pressed. Sampling after a present means previous holds what the player is
 |   actually holding.
 |
 |   The final present, after the loop, submits an empty VDP1 command list so
 |   the last menu frame's sprites are not left composited over the game.
 |   boot_art_release only toggles NBG0; nothing else in the port draws
 |   sprites, so nothing would otherwise replace them.
 | Author: suinevere
 | Dependencies: bootmenu.h, saturn_bootart.h, disc.h, input.h, platform.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void boot_sequence(void)
{
	bootmenu_state state;
	boot_frame frame;
	uint32_t previous;
	uint32_t current;
	uint32_t pressed;

	if (!boot_art_load())
	{
		return;
	}

	boot_art_draw((int)BOOT_SCREEN_LEGAL, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
	boot_art_present();

	check_events();
	previous = boot_key_mask();

	bootmenu_init(&state, (uint32_t)platform_ticks());

	for (;;)
	{
		check_events();
		current = boot_key_mask();
		pressed = current & ~previous;
		previous = current;

		bootmenu_step(&state, (uint32_t)platform_ticks(), pressed, &frame);

		if (frame.music_restart)
		{
			disc_play_track(BOOT_MUSIC_INDEX, 0);
		}

		disc_set_music_volume(frame.music_volume);

		if (frame.start_game)
		{
			boot_fade_out((int)frame.screen, (int)frame.highlight);
			break;
		}

		boot_art_draw((int)frame.screen, (int)frame.highlight);
		boot_art_present();
	}

	disc_stop_track();
	disc_set_music_volume((uint8_t)BOOT_VOLUME_MAX);

	/* NBG0 comes back holding whatever initialize() left in it, and comes
	   back on the frame boot_art_release runs rather than the frame the game
	   first draws. Blacking it here is what stops that appearing between the
	   menu that just faded out and the intro that has not started; it also
	   makes play_animation's opening fade free, since a fade out on an
	   already-black screen writes nothing and takes no time. */
	video_set_fade(0);
	boot_art_release();
	boot_art_present();

	/* The palettes are deliberately left black. Restoring them here lit the
	   menu back up for one frame on the way out: boot_art_present submits the
	   empty command list but VDP1 does not display it until the next vblank, so
	   the dimmed sprites are still on screen when this returns, and a CRAM
	   write lands on them immediately. Nothing draws boot art again until a
	   menu opens, and menu_run sets the level itself before its first frame. */
}
#endif /* HOTA_SATURN */

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

#ifdef HOTA_SATURN
	sat_bup_init();
	if (!saturn_saveslot_init())
	{
		printf("saveslot: LWRAM allocation failed, saves disabled\n");
	}
#endif

	if (!disc_open(cue_path))
	{
		panic("failed to open disc");
	}

	initialize();

#ifdef HOTA_SATURN
	boot_sequence();
#endif

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
