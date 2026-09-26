/*
 * Heart of The Alien Redux: Cutscene and deathscene animation player
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
#include <assert.h>
#include <string.h>

#include "vm.h"
#include "disc_manifest.h"
#include "lzss.h"
#include "debug.h"
#include "disc.h"
#include "video.h"
#include "fadecalc.h"
#include "main.h"
#include "common.h"
#include "client.h"
#include "input.h"
#include "keymap.h"
#include "platform.h"
#include "main.h"
#ifdef HOTA_SATURN
#include "system/saturn_reset.h"
#include "menus/menu.h"
#endif

void rest(int fps);

extern int death_played;

static unsigned char dummy[304*192/2];
static unsigned char screenX[(1+192)*304*2];
static unsigned char *screen0 = screenX + 1*304;
static unsigned char *screen2 = screenX + (192+1)*304;
static unsigned char screen4[192*304];

#define DELTA_SCRATCH_SIZE (0x100000 - 0xdc000)
static unsigned char delta_scratch[DELTA_SCRATCH_SIZE];

static void post_render(int fps)
{
	platform_frame();

#ifdef HOTA_SATURN
	if (saturn_reset_taken())
	{
		menu_soft_reset();
	}
#endif

	check_events();
	update_keys();
	rest(fps);
}

static int g_restorePending = 0;

#define ANIM_FADE_IN_HOLD_MS 60

static int g_fadeInStep = 0;
static unsigned int g_fadeInStart = 0;

static void anim_fade_in_begin(void)
{
	g_fadeInStart = platform_ticks();
	g_fadeInStep = FADECALC_SEGA_CD_STEPS;
}

static void anim_fade_in_pump(void)
{
	unsigned int elapsed;
	int step;

	if (g_fadeInStep <= 0)
	{
		return;
	}

	elapsed = platform_ticks() - g_fadeInStart;
	step = FADECALC_SEGA_CD_STEPS - (int)(elapsed / ANIM_FADE_IN_HOLD_MS);

	if (step < 0)
	{
		step = 0;
	}

	if (step != g_fadeInStep)
	{
		g_fadeInStep = step;
		video_set_fade(fadecalc_step_level(step, FADECALC_SEGA_CD_STEPS));
	}
}

static void copy_to_screen(int a4)
{
	video_render((char *)screen0 + a4);

	if (g_restorePending)
	{
		g_restorePending = 0;
		anim_fade_in_begin();
	}
	else
	{
		anim_fade_in_pump();
	}
}

static void draw_pixel(unsigned char *out, int offset, int color)
{
	if (offset >= 304*192)
	{
		LOG(("out of screen %d\n", offset));
		return;
	}

	out[offset] = color & 0xf;
}

static void fillline(unsigned char *screen, int offset, int count, int color)
{
	while (count > 0)
	{
		draw_pixel(screen, offset++, color);
		count--;
	}
}

static void unpack_animation_delta(int offset, unsigned char *out)
{
	int a5;
	unsigned char *src;
	int d0, d2, d3, d5, d6, d7, d1;

	a5 = offset;

	d0 = get_word(offset);
	offset += 2;
	a5 += d0;

	d0 = get_byte(offset) >> 4;
	*out++ = d0;

	d6 = 0;
	d7 = 0x0f;

	loc_d046:
	d0 = get_long(a5);
	a5 += 4;
	d1 = 0x1f;
	goto loc_d062;

	loc_d04c:
	if (d6 != 0)
	{
		d2 = get_byte(offset) >> 4;
	}
	else
	{
		d2 = get_byte(offset++) & d7;
	}

	*out++ = d2;
	d6 = ~d6;

	loc_d05e:
	d1--;
	if (d1 < 0)
	{
		goto loc_d046;
	}

	loc_d062:
	if (d0 & (1 << d1))
	{
		goto loc_d04c;
	}

	if (d6 != 0)
	{
		d3 = get_byte(offset++);
	}
	else
	{
		d3 = get_byte(offset++) & d7;
		d3 <<= 8;
		d3 |= get_byte(offset);
		d3 >>= 4;
	}

	d1--;
	if (d1 < 0)
	{
		goto loc_d0a2;
	}

	loc_d07e:
	d2 = 0;
	d5 = 1;

	loc_d082:
	if (d0 & (1 << d1))
	{
		goto loc_d0a8;
	}

	d1--;
	if (d1 < 0)
	{
		goto loc_d09c;
	}

	loc_d08a:
	if ((d0 & (1 << d1)) == 0)
	{
		goto loc_d090;
	}

	d2 += d5;

	loc_d090:
	d5 = d5 + d5;
	d1--;
	if (d1 >= 0)
	{
		goto loc_d082;
	}

	d0 = get_long(a5);
	a5 += 4;
	d1 = 0x1f;
	goto loc_d082;

	loc_d09c:
	d0 = get_long(a5);
	a5 += 4;
	d1 = 0x1f;
	goto loc_d08a;

	loc_d0a2:
	d0 = get_long(a5);
	a5 += 4;
	d1 = 0x1f;
	goto loc_d07e;

	loc_d0a8:
	d2 = d2 + d5;
	if (d2 >= 3000)
	{
		return;
	}

	d2++;
	src = out - d3;
	while (d2 >= 0)
	{
		*out++ = *src++;
		d2--;
	}

	goto loc_d05e;
}

static void anim_interesting(int a1, const unsigned char *a2, const unsigned char *a3, unsigned short color_mask)
{
	unsigned char *out;
	int d4;
	int count, offset;
	int bitmask, color;

	out = screen0;

	for (color = 0; color < 16; color++)
	{
		if ((color_mask & (1 << color)) == 0)
		{
			continue;
		}

		bitmask = get_byte(a1++);
		if ((bitmask & 1))
		{
			loc_d308:
			offset = get_word(a1);
			a1 += 2;
			count = get_byte(a1++);
			if (count == 0xff)
			{
				count = get_byte(a1++) + 0xff;
			}

			count++;
			fillline(out, offset, count, color);

			loc_d324:
			if (*a2 == 9 && *a3 == 9)
			{
				a2++;
				a3++;
				goto loc_d308;
			}

			if (*a2 == 8 && *a3 == 8)
			{
				a2++;
				a3++;
				goto loc_d36e;
			}

			offset += 304;
			d4 = extn(*a2++);
			offset += d4;
			count -= d4;
			d4 = extn(*a3++);
			count += d4;

			fillline(out, offset, count, color);
			goto loc_d324;
		}

		loc_d36e:
		if (bitmask & 0x10)
		{
			while (1)
			{
				offset = get_byte(a1++);
				if (offset == 0xff)
				{
					break;
				}

				offset = (offset << 8) | get_byte(a1++);

				count = get_byte(a1++);
				d4 = count & 0x0f;
				count = (count >> 4) + 2;
				d4++;

				LOG(("block offset=%d w=%d h=%d color=%d\n", offset, count, d4, color));

				do
				{
					fillline(out, offset, count, color);
					offset += 304;
					d4--;
				} while (d4 >= 0);
			}
		}

		if (bitmask & 0x4)
		{
			while (1)
			{
				offset = get_byte(a1++);
				if (offset == 0xff)
				{
					break;
				}

				offset = (offset << 8) | get_byte(a1++);
				count = get_byte(a1++) + 1;

				LOG(("horizontal line offset=%d count=%d\n", offset, count));
				fillline(out, offset, count, color);
			}
		}

		if (bitmask & 0x8)
		{
			while (1)
			{
				offset = get_byte(a1++);
				if (offset == 0xff)
				{
					break;
				}

				offset = (offset << 8) | get_byte(a1++);
				count = get_byte(a1++);

				LOG(("vertical line offset=%d count=%d\n", offset, count));

				while (count >= 0)
				{
					draw_pixel(out, offset, color);
					offset = offset + 304;
					count--;
				}
			}
		}

		if (bitmask & 0x80)
		{
			while (1)
			{
				int cnt;

				offset = get_byte(a1++);
				if (offset == 0xff)
				{
					break;
				}

				offset = (offset << 8) | get_byte(a1++);
				d4 = get_word(a1) << 8;
				d4 |= get_byte(a1+2);
				a1 += 3;

 				cnt = 0x17;
				while (cnt >= 0)
				{
					if (d4 & (1 << cnt))
					{
						draw_pixel(out, offset, color);
					}

					offset++;
					if (cnt == 0x0c)
					{
						draw_pixel(out, offset, color);
						offset++;
					}

					if (cnt == 0x13 || cnt == 0x0e || cnt == 0x0a || cnt == 0x05)
					{
						offset = offset + 299;
					}

					cnt--;
				}
			}
		}

		if (bitmask & 0x40)
		{
			while (1)
			{
				int cnt;

				offset = get_byte(a1++);
				if (offset == 0xff)
				{
					break;
				}

				offset = (offset << 8) | get_byte(a1++);
				d4 = get_word(a1);
				a1 += 2;

				cnt = 0x0f;
				while (cnt >= 0)
				{
					if ((d4 & (1 << cnt)))
					{
						draw_pixel(out, offset, color);
					}

					offset++;

					if (cnt == 0x0c || cnt == 0x08 || cnt == 0x04)
					{
						offset = offset + 300;
					}

					cnt--;
				}
			}
		}

		if (bitmask & 0x20)
		{
			while (1)
			{
				int cnt;

				offset = get_byte(a1++);
				if (offset == 0xff)
				{
					break;
				}

				offset = (offset << 8) | get_byte(a1++);

				d4 = get_byte(a1++);

				cnt = 0x07;
				while (cnt >= 0)
				{
					if ((d4 & (1 << cnt)))
					{
						draw_pixel(out, offset, color);
					}

					offset++;

					if (cnt == 0x05 || cnt == 0x03)
					{
						offset = offset + 301;
					}

					if (cnt == 0x04)
					{
						draw_pixel(out, offset, color);
						offset++;
					}

					cnt--;
				}
			}
		}

		if ((bitmask & 0x02))
		{
			while (1)
			{
				offset = get_byte(a1++);
				if (offset == 0xff)
				{
					break;
				}

				offset = (offset << 8) | get_byte(a1++);

				draw_pixel(out, offset, color);
			}
		}
	}
}

void flip_screens(int d0, int d1)
{
	if (d0 == 0)
	{
		memcpy(screen2, screen0, sizeof(screen4));
	}
	else if (d0 < 8)
	{
		memcpy((d1)?screen4:screen0, screen2, sizeof(screen4));
	}
	else
	{
		int y;
		for (y=0; y<192; y++)
		{
			memcpy(screen0 + y*304, screen4 + y*304 + d0*2, 304 - d0*2);
			memcpy(screen0 + y*304 + 304 - d0*2, screen2 + y*304, d0*2);
		}
	}
}

void decompress_backdrop(unsigned char *out, int a2, int a3)
{
	int x, y;
	unsigned char *dummyp, c;

	unlzss(dummy, a2, a3);

	dummyp = dummy;
	for (y=0; y<192; y++)
	{
		for (x=0; x<304/2; x++)
		{
			c = *dummyp++;
			*out++ = (c >> 4);
			*out++ = c & 0xf;
		}
	}
}

#define ANIM_SKIP_BITS (PAD_BIT_A | PAD_BIT_B | PAD_BIT_C | PAD_BIT_X | PAD_BIT_Y | PAD_BIT_Z | PAD_BIT_L | PAD_BIT_R | PAD_BIT_START)

static unsigned int g_skipHeld = ~0u;
static int g_skipArmed = 0;

static void anim_skip_armed(void)
{
	g_skipArmed = 1;
	g_skipHeld = input_raw_buttons();
}

static void anim_skip_disarm(void)
{
	g_skipArmed = 0;
}

static int anim_skip_pressed(void)
{
	unsigned int raw;
	unsigned int fresh;

	if (!g_skipArmed)
	{
		return 0;
	}

	raw = input_raw_buttons();
	fresh = raw & ~g_skipHeld;
	g_skipHeld &= raw;

	return (fresh & ANIM_SKIP_BITS) != 0;
}

int play_sequence(int offset, int fps)
{
	int d0, d1, d3, d4, d6, d7;
	int a1, a2, a3, a4, a5;

	rest(0);

	video_set_scroll(0);

	a5 = offset;
	d7 = 0;
	d6 = 1;
	a2 = get_long(a5);
	a5 += 4;
	if (a2 == 0)
	{
		goto loc_a2_is_0;
	}

	if (a2 == 1)
	{
		goto loc_a2_is_1;
	}

	if (a2 == 3)
	{
		goto loc_a2_is_3;
	}

	if (a2 != 2)
	{
		goto loc_a2_ne_2;
	}

	d6 = 3;
	a2 = get_long(a5);
	a5 += 4;
	goto loc_a2_ne_2;

	loc_a2_is_3:
	a2 = get_long(a5);
	a5 += 4;

	loc_a2_ne_2:
	a3 = get_long(a5);
	a5 += 4;
	decompress_backdrop(screen0, a2, a3);

	d0 = 0;
	d1 = 3;
	flip_screens(d0, d1);
	goto loc_d15e;

	loc_a2_is_1:
	d6 = 0;
	a5 += 8;
	d0 = 3;
	d1 = 0;
	flip_screens(d0, d1);

	d0 = 3;
	d1 = 4;
	flip_screens(d0, d1);

	a2 = get_long(a5);
	a3 = get_long(a5+4);
	a5 += 8;
	decompress_backdrop(screen2, a2, a3);
	goto loc_d15e;

	loc_a2_is_0:
	d6 = 2;
	decompress_backdrop(screen0, get_long(a5), get_long(a5+4));
	decompress_backdrop(screen2, get_long(a5+8), get_long(a5+12));
	a5 += 16;

	loc_d15e:
	d0 = 0;

	loc_d160:
	a1 = a5;
	d0 = get_word(a1);
	a1 += 2;
	if (d0 == 0)
	{
		return 0;
	}

	a5 = a5 + d0;
	if (d6 == 0)
	{
		goto loc_d194;
	}

	if (d6 >= 8)
	{
		d0 = d6;
		d1 = 5;
		flip_screens(d0, d1);
		d6 += 8;
		goto loc_d1f4;
	}

	if (get_byte(a1) != 0)
	{
		goto loc_d1ae;
	}

	if (d6 == 2)
	{
		goto loc_d1ae;
	}

	if (d6 == 3 || d6 == 1)
	{
		d0 = 3;
		d1 = 0;
		flip_screens(d0, d1);
		goto loc_d1f4;
	}

	if (d6 != 0)
	{
		d0 = d6;
		d1 = 5;
		flip_screens(d0, d1);
		d6 += 8;
		goto loc_d1f4;
	}

	loc_d194:
	d6 = 8;
	goto loc_d1f4;

	loc_d1ae:
	if (get_byte(a1) == 1)
	{
		goto loc_d1f4;
	}

	set_variable(255, 100);
	a4 = 0;
	d0 = 0;
	d1 = 0;
	a1 += 2;
	d0 = get_byte(a1++) * 304;
	d1 = get_byte(a1++);

	loc_d1d4:
	copy_to_screen(a4);
	post_render(fps);
	a4 += d0;
	d1--;
	if (d1 >= 0)
	{
		goto loc_d1d4;
	}

	set_variable(255, 6);
	return 0;

	loc_d1f4:
	if (get_variable(250) != 0 || anim_skip_pressed() || cls.quit)
	{
		return 1;
	}

	a1 += 2;
	a2 = a1;
	d0 = get_word(a1);
	a1 += 2;
	if (d0 != 0)
	{
		a2 += d0;
		d3 = get_word(a1);
		d4 = get_word(a1+2);
		a1 += 4;

		unpack_animation_delta(a2, delta_scratch);
		anim_interesting(a1, delta_scratch, delta_scratch + d4, (unsigned short)d3);
	}

	a4 = 0;
	if (d6 != 3)
	{
		goto loc_d268;
	}

	if (d7 == 7 || d7 == 9)
	{
		goto loc_d244;
	}

	if (d7 != 17)
	{
		goto loc_d248;
	}

	loc_d244:
	a4 -= 2;
	goto loc_d268;

	loc_d248:
	if (d7 == 12 || d7 == 13 || d7 == 15 || d7 == 20)
	{
		goto loc_d266;
	}

	if (d7 != 21)
	{
		goto loc_d268;
	}

	loc_d266:
	a4 += 2;

	loc_d268:
	d7++;

#if 0

	if (byte_0_7FF96 == 0)
	{
		goto loc_d2ae;
	}

	copy_to_screen();
	post_render(fps);
	copy_to_screen();
	post_render(fps);

	d0 = 7;

	do
	{
		copy_to_screen();
		post_render(fps);
		d0--;
	} while (d0 >= 0);

	byte_0_7FF96 = 0;
	goto loc_d160;

	loc_d2ae:
#endif

	copy_to_screen(a4*2);
	post_render(fps);
	goto loc_d160;

	return 0;
}

#define DEATH_FRONT_HOLD_MS 143

#define DEATH_CUE_DIAG HOTA_DIAG

static unsigned int g_deathT0 = 0;

static void death_probe(const char *where)
{
#if DEATH_CUE_DIAG
	int st = 0;
	int rel = 0;
	int len = 0;

	if (!disc_music_probe(&st, &rel, &len))
	{
		fprintf(stderr, "dth %s t%u NO CUE\n", where,
			platform_ticks() - g_deathT0);
		return;
	}

	fprintf(stderr, "dth %s t%u st%d fad%d/%d\n", where,
		platform_ticks() - g_deathT0, st, rel, len);
#else
	(void)where;
#endif
}

#define DEATH_CUE_TAIL_CAP_MS 5000

static int g_deathContinues = 0;

int play_death_animation(int index, int chained)
{
	unsigned long offset;
	int ret;
	int continuing = g_deathContinues;
	int old = toggle_aux(0);
	toggle_aux(old);

	g_deathContinues = 0;
	death_played = 0;

	anim_skip_disarm();
	g_deathT0 = platform_ticks();
	death_probe("in");

	if (!continuing)
	{
		fade_out_begin_hold(DEATH_FRONT_HOLD_MS);
		disc_wait_for_music();
		death_probe("wait");
		fade_out_finish();
		anim_fade_in_begin();
	}

	offset = 0xf910 + (index << 2);
	ret = play_sequence(get_long(offset), 15);
	death_probe("seq");

	if (g_fadeInStep > 0)
	{
		g_fadeInStep = 0;
		video_set_fade(FADECALC_LEVEL_NORMAL);
	}

	if (ret == 0)
	{
		g_deathContinues = chained;
	}

	if (!chained)
	{
		disc_wait_for_music_end(DEATH_CUE_TAIL_CAP_MS);
		death_probe("tail");
		death_played = 1;
	}

	toggle_aux(old);
	rest(0);
	return ret;
}

int play_animation(const char *filename, int fileoffset, int track)
{
	int pattern;
	int pattern_offset;
	int total_patterns;
	int scene;
	int scene_offset;
	int palette_offset;
	int fps_;
	int read_offset;
	int stop;
	unsigned char *ptr;

	LOG(("playing animation %s\n", filename));

	anim_skip_armed();

	fade_out_begin();
	fade_out_finish();

	read_offset = ANIMATION_LOAD_BASE - fileoffset;
	ptr = get_memory_ptr(read_offset);
	if (disc_read_file(filename, ptr, get_memory_size() - read_offset) < 0)
	{
		LOG(("play_animation: unable to read %s\n", filename));
		video_set_fade(FADECALC_LEVEL_NORMAL);
		return -1;
	}

	disc_play_track(track, 0);

	disc_wait_for_music();
	rest(0);

	g_restorePending = 1;

	pattern = 0;
	pattern_offset = get_long(0x809e);
	total_patterns = get_long(0x80a2);

	stop = 0;
	while (stop == 0)
	{
		unsigned char *ptr;

		fps_ = 10;

		scene = get_byte(pattern_offset + pattern);
		if (scene == 0x17)
		{
			scene = 3;
			fps_ = 1;
		}

		palette_offset = get_long(0x809a) + (scene << 5);
		ptr = get_memory_ptr(palette_offset);
		video_set_palette_rgb12(ptr);

		scene_offset = get_long(0x80a6 + (pattern << 2));
		stop = play_sequence(scene_offset, fps_);

		pattern++;
		if (pattern == total_patterns)
		{
			break;
		}

		rest(fps_);
	}

	rest(0);

	g_fadeInStep = 0;

	if (g_restorePending)
	{
		g_restorePending = 0;
	}
	else
	{
		fade_out_begin();
		fade_out_finish();
	}

	set_variable(250, 0);

	LOG(("leaving animation player\n"));
	return stop;
}
