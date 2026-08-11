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
#include "platform.h"
#include "main.h"

///////
void rest(int fps);

/* used for 4->8 bit convertion (140K penalty) */
static unsigned char dummy[304*192/2];
static unsigned char screenX[(1+192)*304*2];
static unsigned char *screen0 = screenX + 1*304;
static unsigned char *screen2 = screenX + (192+1)*304;
static unsigned char screen4[192*304];

/*----------------------
 | delta_scratch
 | Description: Decode buffer for unpack_animation_delta. Lived at offset 0xdc000
 |   inside the emulated 68000 map until 2026-08-04, which forced the map to stay
 |   1 MB and put the Saturn port 189 KB over its total work RAM. It is not
 |   emulated state -- nothing reads it through get_byte, and the original code
 |   carried its own note to move it out. Sized 0x100000 - 0xdc000, the space the
 |   original reserved above the scratch base, because unpack_animation_delta
 |   writes a data-driven stream with no length of its own.
 |   The read side moved too: unpack_animation_delta back-references with
 |   src = out - d3, and while out was &memory[0xdc000] a back-reference past
 |   the first written byte landed on low map bytes -- in bounds and defined.
 |   The same read now falls before this array and is genuinely out of bounds.
 |   Well-formed streams never reach back that far, so behaviour is unchanged,
 |   but a malformed one now has undefined behaviour where it used to have
 |   merely wrong pixels.
 | Author: suinevere
 ----------------------*/
#define DELTA_SCRATCH_SIZE (0x100000 - 0xdc000)
static unsigned char delta_scratch[DELTA_SCRATCH_SIZE];

/*----------------------
 | post_render
 | Description: The animation player's per-frame tail, run once for every
 |   copy_to_screen. platform_frame comes first because on Saturn it is the
 |   call that actually presents the frame copy_to_screen just wrote: these
 |   loops never reach run()'s platform_frame, so without it the intro would
 |   present only by accident, on the frames where rest()'s busy-wait happened
 |   to spin. Presenting before check_events also means the input read below
 |   sees peripherals refreshed by the same sync.
 | Author: suinevere
 | Dependencies: platform.h, input.h, main.h (rest)
 | Globals: N/A
 | Params: fps -- frame budget handed to rest(), 0 to only reset the clock
 | Returns: N/A
 ----------------------*/
static void post_render(int fps)
{
	platform_frame();
	check_events();
	update_keys();
	rest(fps);
}

/*----------------------
 | g_restorePending
 | Description: Set by play_animation when it has faded to black and wants
 |   brightness back on the first frame that has something new in it, rather
 |   than immediately. Only play_animation sets it; play_death_animation
 |   restores its own level directly, exactly as before, so nothing here
 |   changes what a death does.
 | Author: suinevere
 ----------------------*/
static int g_restorePending = 0;

/*----------------------
 | DEATH_FADE_IN_HOLD_MS
 | Description: Per-step hold for the fade in at the front of a death
 |   sequence. Eight steps at 60 ms is 480 ms, the same speed as an ordinary
 |   fade out, so a death fades down and back up at one rate.
 | Author: suinevere
 ----------------------*/
#define DEATH_FADE_IN_HOLD_MS 60

/*----------------------
 | g_fadeInStep
 | Description: Where the death fade in has got to: FADECALC_SEGA_CD_STEPS is
 |   black, 0 is both "normal" and "not running", which is the same state and
 |   so needs no separate flag.
 |
 |   Only the death path arms this. A cutscene still comes back at full
 |   brightness in one write, which is what the Sega CD does and what main.h's
 |   fade banner describes.
 | Author: suinevere
 ----------------------*/
static int g_fadeInStep = 0;
static unsigned int g_fadeInStart = 0;

/*----------------------
 | death_fade_in_begin
 | Description: Arms the fade in, leaving the screen black. Deliberately
 |   writes no palette: the fade out that precedes it has already put the
 |   screen where this starts from, and writing it again would spend a step
 |   showing the viewer what they are already looking at.
 | Author: suinevere
 | Dependencies: platform.h, fadecalc.h
 | Globals: g_fadeInStep, g_fadeInStart
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void death_fade_in_begin(void)
{
	g_fadeInStart = platform_ticks();
	g_fadeInStep = FADECALC_SEGA_CD_STEPS;
}

/*----------------------
 | death_fade_in_pump
 | Description: Walks the fade in down to normal on the clock, one write per
 |   step it has actually reached.
 |
 |   Scheduled on elapsed time rather than counted per frame so the fade takes
 |   the same wall clock whatever the sequence's frame rate is -- and, more to
 |   the point, so it costs no time of its own. It runs over the animation's
 |   opening frames rather than in front of them. Anything inserted ahead of
 |   the sequence would push every frame after it back, and where those frames
 |   fall against the death cue is the one thing on this path that is not free
 |   to move.
 | Author: suinevere
 | Dependencies: video.h, fadecalc.h, platform.h
 | Globals: g_fadeInStep, g_fadeInStart
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void death_fade_in_pump(void)
{
	unsigned int elapsed;
	int step;

	if (g_fadeInStep <= 0)
	{
		return;
	}

	elapsed = platform_ticks() - g_fadeInStart;
	step = FADECALC_SEGA_CD_STEPS - (int)(elapsed / DEATH_FADE_IN_HOLD_MS);

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

/*----------------------
 | copy_to_screen
 | Description: Hands the animation player's working buffer across the video
 |   seam, then brings brightness back if a fade is waiting on new pixels --
 |   in one write for a cutscene, a step at a time for a death.
 |   Does not present the frame -- post_render does, on the next line at
 |   every call site.
 | Author: suinevere
 | Dependencies: video.h, fadecalc.h
 | Globals: screen0, g_restorePending, g_fadeInStep
 | Params: a4 -- byte offset into screen0 the frame starts at
 | Returns: N/A
 ----------------------*/
static void copy_to_screen(int a4)
{
	//video_set_scroll(0); /////////
	video_render((char *)screen0 + a4);

	death_fade_in_pump();

	/* Brightness returns here, after the pixels, and not when the fade
	   ended. A fade blacks the screen by darkening the palette while the
	   framebuffer still holds the outgoing scene, so restoring the level
	   before anything new is drawn shows that old scene at full brightness
	   for the gap before the first frame lands -- the flash at the end of a
	   cutscene fade. Ordering is the whole fix: pixels, then light. */
	if (g_restorePending)
	{
		g_restorePending = 0;
		video_set_fade(FADECALC_LEVEL_NORMAL);
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

	/* bug in the original code, assumes offset < 0x100 */
	d0 = get_word(offset);
	offset += 2;
	a5 += d0;

	/*
	Original game uses:
	d0 = (d0 & 0xff00) | get_byte(offset);
	d0 = d0 >> 4;
	*/

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

/*----------------------
 | anim_interesting
 | Description: Draws one delta frame, in probably the most interesting
 |   compression ever developed. color_mask says which of the 16 colors changed
 |   this frame; each is associated with a mask of the brush types used to draw
 |   it -- delta-horz-line, rectangle, horizontal line, vertical line, set of
 |   pixels, 3x3, 4x4 and 5x5 patterns. Two of the three cursors are pointers
 |   and one is an offset, which is deliberate: a1 walks the loaded animation
 |   stream, genuine emulated 68000 state that must be read with get_byte,
 |   while a2 and a3 walk delta_scratch, a host buffer that no longer lives in
 |   the map -- reading those through get_byte would fetch whatever the map
 |   happens to hold at those offsets and silently corrupt the frame.
 | Author: suinevere
 ----------------------*/
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
			/* color not used here */
			continue;
		}

		/* each bit is a different drawing pattern, 1x1 etc */
		bitmask = get_byte(a1++);
		if ((bitmask & 1))
		{
			/* start drawing a line, and then continue from there
			 * where each line supplies deltax and delta-count
			 */
			loc_d308:
			offset = get_word(a1);
			a1 += 2;
			count = get_byte(a1++);
			if (count == 0xff)
			{
				/* two bytes */
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

			offset += 304; /* next line */
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
				/* block */
				offset = get_byte(a1++);
				if (offset == 0xff)
				{
					break;
				}

				offset = (offset << 8) | get_byte(a1++);

				/* highnibble=width-2, lownibble=height-1 */
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
			/* horizontal line */
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
			/* vertical line */
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
			/* 5x5 pattern (24 bits, center pixel always set) */
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
						/* center pixel of 5x5 is always set */
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
				/* 4x4 pattern (16 bits) */
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

				/* 3x3 pattern (8 bits, center always set) */
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
			/* collection of pixels */
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

	/* unlzss backdrop */
	unlzss(dummy, a2, a3);

	/* convert 4bpp->8bpp */
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

int play_sequence(int offset, int fps)
{
	int d0, d1, d3, d4, d6, d7;
	int a1, a2, a3, a4, a5;

	rest(0);

	/* This code is obscure. I have no idea why it's written this way,
	 * or how the hell it works, but .. it just works!.
	 */
	video_set_scroll(0);

	/* d0c6 */
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

	/* else d0f2 */
	d6 = 3;
	a2 = get_long(a5);
	a5 += 4;
	goto loc_a2_ne_2;

	loc_a2_is_3:
	/* move.b  #2,($C0401).l */
	a2 = get_long(a5);
	a5 += 4;

	loc_a2_ne_2:
	/* decompress into screen0 */
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

	/* decompress into screen2 */
	a2 = get_long(a5);
	a3 = get_long(a5+4);
	a5 += 8;
	decompress_backdrop(screen2, a2, a3);
	goto loc_d15e;

	loc_a2_is_0:
	/* decompress TWO screens */
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
	/* what the hell ? */
	if (get_byte(a1) == 1)
	{
		goto loc_d1f4;
	}

	set_variable(255, 100);
	// a4 = screen0
	a4 = 0;
	d0 = 0;
	d1 = 0;
	a1 += 2;
	d0 = get_byte(a1++) * 304;   /* y */
	d1 = get_byte(a1++);

	loc_d1d4:
	/* clr.b   ($C0401).l */
	//video_set_scroll(a4/304); ///////////////
	copy_to_screen(a4);
	post_render(fps);
	/* LOG(("d1d4\n")); */
	a4 += d0;
	d1--;
	if (d1 >= 0)
	{
		goto loc_d1d4;
	}

	set_variable(255, 6);
	return 0;

	loc_d1f4:
	if (get_variable(250) != 0 || cls.quit)
	{
		/* hack, to stop animations */
		return 1;
	}

	a1 += 2;
	a2 = a1;
	d0 = get_word(a1);
	a1 += 2;
	if (d0 != 0)
	{
		a2 += d0;
		d3 = get_word(a1); /* colors mask used in this frame */
		d4 = get_word(a1+2); /* offset of something */
		a1 += 4;

		unpack_animation_delta(a2, delta_scratch);
		anim_interesting(a1, delta_scratch, delta_scratch + d4, (unsigned short)d3);
	}

	/* a4 = screen0 */
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
	//this code was never executed. I wonder why it's even here :)

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

/*----------------------
 | DEATH_CHAINED_HOLD_MS
 | Description: Per-step hold for the one fade in front of a two-segment
 |   death. Eight steps at 120 ms is 960 ms, which is exactly what the two
 |   separate fades this replaced used to add up to.
 |
 |   960 is the confirmed number and the tuning range around it is narrower
 |   than it looks. 1200 was tried and was too long; 600 was tried on the
 |   reasoning that holding the last frame had made the head start unnecessary,
 |   and it opened too large a gap before the splat. Both directions were
 |   rejected by eye, which puts this back where five earlier rounds also
 |   landed. Treat it as measured, not chosen.
 | Author: suinevere
 ----------------------*/
#define DEATH_CHAINED_HOLD_MS 200

/*----------------------
 | DEATH_LAST_FRAME_MIN_MS
 | Description: How long a death's final picture is held before the button
 |   that dismisses it is listened for at all.
 |
 |   Deaths are usually reached mid-struggle with a button already down, so
 |   without a floor the held shot would be dismissed by the press that caused
 |   it. The floor also guarantees the beat exists for a player who mashes
 |   through everything.
 |
 |   Free of the synchronisation the rest of this path is built around: the
 |   splat is a frame inside the sequence, and time spent after the last frame
 |   cannot move a frame that has already been drawn. Tune by eye.
 | Author: suinevere
 ----------------------*/
#define DEATH_LAST_FRAME_MIN_MS 400

/*----------------------
 | death_button_down
 | Description: Whether any button a player would press to dismiss a held shot
 |   is down. The d-pad is deliberately not included: a held direction is how
 |   the player arrived at most deaths.
 | Author: suinevere
 | Dependencies: input.h
 | Globals: key_a, key_b, key_c, key_select
 | Params: N/A
 | Returns: non-zero if any dismiss button is down
 ----------------------*/
static int death_button_down(void)
{
	return key_a || key_b || key_c || key_select;
}

/*----------------------
 | hold_last_frame
 | Description: Keeps presenting whatever is already on screen until the
 |   player presses a button, then resets rest()'s baseline.
 |
 |   The press has to be a fresh one. After the floor expires the loop first
 |   waits for every button to be up and only then watches for one going down,
 |   so a button still held from the death itself neither dismisses the shot
 |   nor has to be noticed by the player as the thing they must let go of.
 |
 |   Presents rather than sleeps, for the same reason the fade does: a display
 |   that stops being handed frames reads as a lockup rather than a held shot.
 |   Nothing is drawn, so what stays up is the sequence's own last frame.
 |   check_events is called because it is the only thing that refreshes the pad
 |   -- and, on the host backend, the only thing that can set cls.quit, which
 |   is what stops this being a way to hang the game.
 |
 |   rest(0) on the way out is load bearing. rest() paces the game against
 |   last_tick, and returning long after last_tick was current makes the loop
 |   read itself as behind and sprint to catch up.
 | Author: suinevere
 | Dependencies: platform.h, input.h, client.h, main.h (rest)
 | Globals: cls
 | Params: min_ms -- how long to hold before a press is listened for
 | Returns: N/A
 ----------------------*/
static void hold_last_frame(unsigned int min_ms)
{
	unsigned int start = platform_ticks();
	int armed = 0;

	while (!cls.quit)
	{
		platform_frame();
		check_events();
		update_keys();
		platform_delay(1);

		if (platform_ticks() - start < min_ms)
		{
			continue;
		}

		if (!armed)
		{
			armed = !death_button_down();
		}
		else if (death_button_down())
		{
			break;
		}
	}

	rest(0);
}

/*----------------------
 | g_deathContinues
 | Description: Set when the death opcode just played said another death
 |   opcode follows it, so the next call knows it is the back half of one
 |   death rather than the start of a new one and skips the fade and the wait
 |   that have already been spent on its behalf.
 |
 |   Cleared on anything but a clean finish, so an aborted sequence cannot
 |   leave a later, unrelated death with no fade and no music wait.
 | Author: suinevere
 ----------------------*/
static int g_deathContinues = 0;

/** Plays a death sequence
    @param index    animation index in resources
    @param chained  non-zero if another death opcode follows this one

    Death animations are stored as resources in the room file itself,
    along with the rest of the level
*/
/*----------------------
 | play_death_animation
 | Description: Runs one death sequence, fading out first unless the previous
 |   opcode already paid for this one.
 |
 |   These frames are already in the room file, so unlike play_animation there
 |   is no disc read here -- and therefore no cdda_restore, which is what waits
 |   for the music everywhere else. The script commands its track and arrives
 |   here immediately, so without the wait the sequence plays against a drive
 |   still seeking. The only time being spent is that seek, so the fade runs
 |   across it rather than before it: begin installs the tick, the wait polls
 |   and advances the palette as it goes, and finish spends whatever remains.
 |
 |   A death written as two adjacent opcodes used to fade twice, and the second
 |   fade covered no disc work at all -- there is no cue and no seek between
 |   two opcodes. Its half second was not decoration though: the splat lives in
 |   the second sequence, and four attempts to delete that delay each lost the
 |   splat. So the delay is not skipped, it is moved: `chained` makes the first
 |   segment fade at DEATH_CHAINED_HOLD_MS, long enough to cover both segments'
 |   delay, and the second not fade at all. Nothing now interrupts the middle of
 |   a death, and the splat moves only by however much that hold exceeds the two
 |   fades it replaced. The cost is that the first segment starts that much
 |   later against its music. See mem/2026-08-11-death-animation-cdda-sync.md.
 |
 |   Where a cutscene comes back at full brightness in a single write, a death
 |   fades back in, over its own opening frames so the ramp costs no time and
 |   moves nothing. Deaths are the only path that does this.
 |
 |   The last segment then holds its final frame until the player presses a
 |   button rather than handing the script straight back, so a death ends on
 |   its own picture instead of cutting to the game over screen on the frame
 |   after the animation runs out.
 |
 |   The fade in is disarmed after the sequence whatever happened, since a
 |   sequence that drew nothing would otherwise leave the game behind a black
 |   screen with nothing left running to lift it.
 | Author: suinevere
 | Dependencies: main.h (fades), disc.h, video.h, fadecalc.h
 | Globals: g_deathContinues
 | Params: index -- animation index in the room's resources; chained -- non-zero
 |   if the opcode after this one is another death
 | Returns: zero if played completely, 1 if aborted, negative on error
 ----------------------*/
int play_death_animation(int index, int chained)
{
	unsigned long offset;
	int ret;
	int continuing = g_deathContinues;
	int old = toggle_aux(0);
	toggle_aux(old);

	g_deathContinues = 0;

	if (!continuing)
	{
		if (chained)
		{
			fade_out_begin_hold(DEATH_CHAINED_HOLD_MS);
		}
		else
		{
			fade_out_begin();
		}

		disc_wait_for_music();
		fade_out_finish();
		death_fade_in_begin();
	}

	/* set palette 2 ? */
	offset = 0xf910 + (index << 2);
	ret = play_sequence(get_long(offset), 15);

	if (g_fadeInStep > 0)
	{
		g_fadeInStep = 0;
		video_set_fade(FADECALC_LEVEL_NORMAL);
	}

	if (ret == 0)
	{
		g_deathContinues = chained;

		if (!chained)
		{
			hold_last_frame(DEATH_LAST_FRAME_MIN_MS);
		}
	}

	toggle_aux(old);
	rest(0);
	return ret;
}

/** Plays an animation file
    @param filename    name as appears on cd
    @param fileoffset  offset in bytes where to start reading from
    @returns zero if played completely, 1 if aborted, negative on error
*/
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

	/* Fade to completion first, then read behind the black it leaves.

	   Running it across the read was tried and cannot look right here. A GFS
	   request is uninterruptible and an aligned animation is only two of
	   them, so the tick fires once at the start and once more a second and a
	   half later -- one dim step, a long pause on that step, then a jump
	   straight to black. Manufacturing more yield points means more
	   requests, each costing the drive a revolution to re-acquire its place,
	   which is about a second added to a second-and-a-half read to hide half
	   a second of it.

	   fade_out_finish immediately after begin is what makes this the
	   blocking form: no disc work happens between them, so it walks all
	   eight steps itself and uninstalls the tick before the read starts.

	   play_death_animation is deliberately not changed to match. It waits on
	   a seek rather than a read, and a seek is a poll loop that yields every
	   frame, so the overlapped form works there and is what its timing is
	   currently built on. */
	fade_out_begin();
	fade_out_finish();

	/* animations are loaded into a fixed place in 68000 memory */
	read_offset = ANIMATION_LOAD_BASE - fileoffset;
	ptr = get_memory_ptr(read_offset);
	if (disc_read_file(filename, ptr, get_memory_size() - read_offset) < 0)
	{
		LOG(("play_animation: unable to read %s\n", filename));
		video_set_fade(FADECALC_LEVEL_NORMAL);
		return -1;
	}

	/* The music starts here, after the read, and not at the call site
	   before it. There is one drive: a track commanded before this read is
	   audible for as long as the fade lasts, then silenced by the read's
	   cdda_suspend and started again from its first frame by cdda_restore --
	   heard as a second of music, a pause the length of the read, and the
	   track beginning a second time. Commanding it here costs one seek
	   instead of two and cannot be interrupted by a read that has already
	   finished. */
	disc_play_track(track, 0);

	/* Hold on the black already on screen until the music is up, so the
	   first frame and the first note arrive together. Nothing is fading by
	   now -- fade_out_finish above uninstalled the tick before the read --
	   so this is a plain wait against a black screen and costs the viewer
	   nothing visible. */
	disc_wait_for_music();
	rest(0);

	/* Brightness is not restored here. It comes back in copy_to_screen, on
	   the first frame that has new pixels: the fade blacked the screen
	   through the palette while the framebuffer still holds the outgoing
	   scene, so restoring it now flashes that old scene at full brightness
	   in the gap before anything new is drawn. video_set_palette_rgb12
	   below deliberately does not reset the level either (see video.h), so
	   the animation stays black until that first frame lands and then comes
	   up with the new picture already in place. */
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
			/* special case for credits */
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
			/* no more scenes */
			break;
		}

		rest(fps_);
	}

	rest(0);

	/* If the sequence drew nothing at all, copy_to_screen never ran and the
	   deferred restore above is still pending -- leaving the game behind a
	   black screen with nothing to explain it. Clear it here so the fade can
	   only ever outlast the thing it was covering, never the animation. */
	if (g_restorePending)
	{
		g_restorePending = 0;
		video_set_fade(FADECALC_LEVEL_NORMAL);
	}

	/* just in case, clean variable 250 (key_a pressed) */
	set_variable(250, 0);

	LOG(("leaving animation player\n"));
	return stop;
}
