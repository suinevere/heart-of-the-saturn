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
#include "system/saturn_bootmark.h"
#ifdef HOTA_SATURN
#include "system/saturn_bootart.h"
#include "system/saturn_backup.h"
#include "system/saturn_saveslot.h"
#include "system/saturn_keymap.h"
#include "system/saturn_reset.h"
#include "menus/menu.h"
#include "chainload.h"
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

extern int script_ptr;

int next_script;

int ending_played;

int access_code_skip;
int access_code_answer;

unsigned int access_code_skip_at;

unsigned int access_code_load_seq;

int death_played;

int return_to_boot;

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

FILE *record_fp = 0;

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

int vm_enter_code(int code, int *playedAnimations)
{
	int room;

	if (playedAnimations != 0)
	{
		*playedAnimations = 0;
	}

	toggle_aux(0);
	set_variable(220, code % 10);

	room = ((code / 10) % 10) + 1;

	if (room == 10)
	{
		room = 8;
	}
	else if (room == 7)
	{
		play_animation("MAKE2MB.BIN", 0x109a, 35);
		play_animation("MID2.BIN", 0, 36);
		disc_stop_track();
		screen_arm_fade_restore();
		room = 6;

		if (playedAnimations != 0)
		{
			*playedAnimations = 1;
		}
	}
	else if (room == 6)
	{
		play_animation("END1.BIN", 0, 37);
		play_animation("END2.BIN", 0, 38);
		play_animation("END3.BIN", 0, 39);
		play_animation("END4.BIN", 0, 40);
		ending_played = 1;
		screen_arm_fade_restore();
		room = 7;

		if (playedAnimations != 0)
		{
			*playedAnimations = 1;
		}
	}

	return room;
}

static void atexit_callback(void)
{
	disc_stop_track();
	disc_close();
	platform_quit();
}

static int initialize()
{
	if (video_init() < 0)
	{
		panic("failed to initialize renderer module");
	}

	if (!vm_alloc_memory())
	{
		panic("out of memory allocating the emulated 68000 map");
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

static void load_part2_data(void)
{
	if (!game2bin_alloc())
	{
		panic("out of memory allocating the GAME2.BIN buffer");
	}

	if (game2bin_init() < 0)
	{
		panic("can't read GAME2.BIN file");
	}
}

void load_room_screen(int room, int index)
{
	int i;
	unsigned char *pixels;

	LOG(("loading room screen %d from room file %d\n", index - 1, room));

	unpack_room(scratchpad, index - 1);

	pixels = (unsigned char *) get_screen_ptr(0);
	for (i=0; i<304*192/2; i++)
	{
		pixels[i*2+0] = scratchpad[i] >> 4;
		pixels[i*2+1] = scratchpad[i] & 0xf;
	}

	current_backdrop = index;
}

void rewind_recorded_keys()
{
	cached_keys_offset = 0;
}

void flush_recorded_keys()
{
	fwrite(cached_recorded_keys, 1, cached_keys_offset, record_fp);
	cached_keys_offset = 0;
}

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
		flush_recorded_keys();
	}
}

static int s_swallowA, s_swallowB, s_swallowC;

void input_swallow_held(void)
{
	s_swallowA = key_a;
	s_swallowB = key_b;
	s_swallowC = key_c;
}

static void swallow_one(int *key, int *swallow)
{
	if (!*swallow)
	{
		return;
	}
	if (*key)
	{
		*key = 0;
	}
	else
	{
		*swallow = 0;
	}
}

void update_keys()
{
	short flags;

	toggle_aux(0);

	swallow_one(&key_a, &s_swallowA);
	swallow_one(&key_b, &s_swallowB);
	swallow_one(&key_c, &s_swallowC);

#ifdef HOTA_SATURN
	if (screen_fade_in_active())
	{
		key_left = key_right = key_up = key_down = 0;
		key_a = key_b = key_c = 0;
	}
#endif

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
	}

	set_variable(253, flags);

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

#ifdef HOTA_SATURN
	if (access_code_answer)
	{
		access_code_answer = 0;
		set_variable(VM_VAR_BUTTONS,
			(unsigned short)(get_variable(VM_VAR_BUTTONS)
					 | VM_BUTTON_MASK_ABC));
	}
#endif
}

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
#ifdef HOTA_SATURN
			input_latch_clear();
#endif
			return;
		}

		if (speed_throttle == 1)
		{
			fps = fps*10;
		}

		unsigned int diff = ((1000 << 16) / fps) + last_tick_fp;
		last_tick_fp = diff & 0xffff;
		diff = diff >> 16;
		unsigned int current_tick = platform_ticks();
		while (current_tick - last_tick < diff)
		{
			platform_delay(1);
#ifdef HOTA_SATURN
			input_latch();
#endif
			current_tick = platform_ticks();
		}
		last_tick += diff;
	}
}

#define FADE_HOLD_MS 60

static unsigned int g_fadeStart = 0;
static int g_fadeActive = 0;
static int g_fadeStep = 0;
static unsigned int g_fadeHold = FADE_HOLD_MS;

static int g_fadeSilent = 0;

static int g_fadeCeiling = FADECALC_LEVEL_NORMAL;

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

static void fade_out_arm(unsigned int hold_ms, int free_when_black)
{
	if (hold_ms < 1)
	{
		hold_ms = 1;
	}

	screen_fade_cancel();
	g_fadeHold = hold_ms;

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

#ifdef HOTA_SATURN
static void saturn_save_poll(void)
{
	menu_pause_poll();
}
#endif

static void run()
{
	cls.quit = 0;
	init_tasks();

	if (next_script == 0)
	{
#ifdef HOTA_SATURN
		next_script = menu_front();
#else
		play_intro();
		screen_arm_fade_restore();
		next_script = 7;
#endif
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

#ifdef HOTA_SATURN
			access_code_skip_at = platform_ticks();
			access_code_load_seq++;

			if (access_code_skip == ACCESS_CODE_SKIP_ARMED)
			{
				access_code_skip = ACCESS_CODE_SKIP_LIVE;
			}
			else
			{
				access_code_skip = 0;
			}

			menu_note_checkpoint();
#endif
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
				break;
			}
		}

		rest(12);
		platform_frame();
	}
}

static void animation_test()
{
	int files = sizeof(anm_files) / sizeof(anm_files[0]);
	play_anm(anm_files, files, 1);
}

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

static const char *find_cue_path(void)
{
	static char found[512];
	static char best_name[508];
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
#endif

#ifdef HOTA_SATURN
static uint32_t boot_key_mask(void)
{
	unsigned int raw = input_raw_buttons();
	uint32_t mask = 0;

	if (raw & PAD_BIT_UP)    mask |= BOOT_KEY_UP;
	if (raw & PAD_BIT_DOWN)  mask |= BOOT_KEY_DOWN;
	if (raw & PAD_BIT_LEFT)  mask |= BOOT_KEY_LEFT;
	if (raw & PAD_BIT_RIGHT) mask |= BOOT_KEY_RIGHT;
	if (raw & PAD_BIT_A)     mask |= BOOT_KEY_A;
	if (raw & PAD_BIT_B)     mask |= BOOT_KEY_B;
	if (raw & PAD_BIT_C)     mask |= BOOT_KEY_C;

	return mask;
}

static void boot_fade_out(int highlight)
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
			boot_art_draw(highlight);
			boot_art_present();
		}
		while (platform_ticks() - start < FADE_HOLD_MS);
	}

	rest(0);
}

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

	boot_art_fade(FADECALC_LEVEL_NORMAL);
	boot_mark(BOOTMARK_BOOTART);

	boot_art_draw((int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
	boot_art_present();
	boot_mark(BOOTMARK_FIRSTFRAME);

	check_events();
	previous = boot_key_mask();

	bootmenu_init(&state, (uint32_t)platform_ticks(),
	              chainload_available(), disc_part2_available());

	for (;;)
	{
		check_events();
		current = boot_key_mask();
		pressed = current & ~previous;
		previous = current;

		bootmenu_step(&state, (uint32_t)platform_ticks(), pressed, &frame);

		saturn_reset_taken();

		if (frame.music_restart)
		{
			disc_play_track(BOOT_MUSIC_INDEX, 0);
		}

		disc_set_music_volume(frame.music_volume);

		if (frame.start_part1)
		{
			boot_fade_out((int)frame.highlight);
			chainload_run();
			disc_play_track(BOOT_MUSIC_INDEX, 0);
		}

		if (frame.start_game)
		{
			boot_fade_out((int)frame.highlight);
			break;
		}

		boot_art_draw((int)frame.highlight);
		boot_art_present();
	}

	disc_stop_track();
	disc_set_music_volume((uint8_t)BOOT_VOLUME_MAX);

	boot_mark(0);

	video_set_fade(0);
	boot_art_release();
	boot_art_present();

}
#endif

#ifdef HOTA_SATURN
static void soft_return_reset(void)
{
	game2bin_free();

	vm_reset();
	set_variable(227, 1);

	next_script = 0;
	current_room = 0;
	death_played = 0;
	ending_played = 0;
	access_code_skip = 0;
	access_code_answer = 0;
	return_to_boot = 0;

	menu_reset_for_boot();

	disc_stop_track();
	screen_fade_cancel();
	video_set_fade(FADECALC_LEVEL_NORMAL);
}
#endif

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
			break;
		}

		switch(c)
		{
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
	boot_mark(BOOTMARK_PLATFORM);

#ifdef HOTA_SATURN
	sat_bup_init();
	if (!saturn_saveslot_init())
	{
		printf("saveslot: LWRAM allocation failed, saves disabled\n");
	}
	saturn_keymap_load();
#endif

	if (!disc_open(cue_path))
	{
		panic("failed to open disc");
	}

	boot_mark(BOOTMARK_DISC);

	initialize();

	boot_mark(BOOTMARK_INIT);

#ifdef HOTA_SATURN
	for (;;)
	{
		boot_sequence();
		load_part2_data();
		run();

		if (!return_to_boot)
		{
			break;
		}

		soft_return_reset();
	}
#else
	load_part2_data();

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
#endif

	if (record_fp != NULL)
	{
		flush_recorded_keys();
		fclose(record_fp);
	}

	return 0;
}
