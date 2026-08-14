/*
 * Heart of The Alien: Virtual screens handling
 * Copyright (c) 2004 Gil Megidish
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
#include <memory.h>

#include "vm.h"
#include "debug.h"
#include "video.h"
#include "fadecalc.h"
#include "platform.h"
#include "screen.h"

#define VAR_VSCROLL 249

/*----------------------
 | SCREEN_FADE_IN_HOLD_MS
 | Description: Per-step hold for the deferred fade in. Eight steps at 60 ms is
 |   just under half a second, the same speed main.c's FADE_HOLD_MS gives a fade
 |   out, so the game fades down and back up at one rate.
 | Author: suinevere
 ----------------------*/
#define SCREEN_FADE_IN_HOLD_MS 60

/*----------------------
 | s_fadeRestorePending
 | Description: Whether a caller has left the screen black and is waiting on
 |   the game to draw before brightness comes back. Set by
 |   screen_arm_fade_restore, cleared by the first update_screen that renders
 |   after it. See screen.h for why the restore is deferred at all.
 | Author: suinevere
 ----------------------*/
static int s_fadeRestorePending = 0;

/*----------------------
 | s_fadeInPending / s_fadeInStep / s_fadeInStart
 | Description: The ramped arm and the ramp it starts. s_fadeInPending is the
 |   deferral, spent by the first update_screen that renders; s_fadeInStep is
 |   where the ramp has got to, FADECALC_SEGA_CD_STEPS being black and 0 both
 |   "normal" and "not running", which is the same state and so needs no
 |   separate flag.
 |
 |   Same shape as animation.c's g_fadeInStep, and for the same reason: the two
 |   sit on opposite sides of the draw seam -- that one pumps from
 |   copy_to_screen, this one from update_screen -- and neither can see the
 |   other's frames.
 | Author: suinevere
 ----------------------*/
static int s_fadeInPending = 0;
static int s_fadeInStep = 0;
static unsigned int s_fadeInStart = 0;

/* instead of allocations. makes porting to consoles easier. */
static char huge_buf[304*192*3];

static char *screens[4];
static char *screen_visible;
static char *screen_invisible;

static int selected_screen;
static char *selected_screen_ptr;

static short scroll_reg = 0;

/** Fills the entire screen with a single color
    @param dest     --unused--
    @param color    entry from 4 bit palette
*/
void fill_screen(int dest, char color)
{
	memset((void *)selected_screen_ptr, color, 304*192);
}

void fill_line_reversed(int count, int x, int y, int color)
{
	unsigned char *bufp;

	if (x < 0 || y < 0 || y > 191)
	{
		LOG(("out of screen\n"));
		return;
	}

	if (x - count < 0)
	{
		count = x + 1;
	}

	if (x > 303)
	{
		count = count - (x - 303);
		x = 303;
	}

	bufp = (unsigned char *)get_selected_screen_ptr() + (y * 304) + x;
	while (count > 0)
	{
		*bufp-- = color;
		count--;
	}
}

void fill_line(int count, int x, int y, int color)
{
	unsigned char *bufp;

	//LOG(("fill line count=%d, x=%d, y=%d, color=%d\n", count, x, y, color));

	/* output offset */
	if (x > 303 || y > 191 || y < 0)
	{
		LOG(("WARN: out of screen! (x=%d, y=%d)\n", x, y));
		return;
	}

	while (x < 0)
	{
		x++;
		count--;
	}

	if (x + count > 303)
	{
		count = 303 - x + 1;
	}

	bufp = (unsigned char *)get_selected_screen_ptr() + (y * 304) + x;
	while (count > 0)
	{
		*bufp++ = color;
		count--;
	}
}

void copy_screen(int dest, int src)
{
	int masked_src;
	int masked_dest;
	char *src_surface;
	char *dest_surface;

	if (src == 0xc0)
	{
		set_variable(VAR_VSCROLL, scroll_reg);
		/* XXX return ? */
	}

	masked_src = src;
	if (masked_src < 0xfe)
	{
		masked_src = masked_src & 0xbf;
	}

	masked_dest = dest;
	if (masked_dest < 0xfe)
	{
		masked_dest = masked_dest & 0xbf;
	}

	src_surface = get_screen_ptr(masked_src);
	dest_surface = get_screen_ptr(masked_dest);

	LOG(("copying surface %x onto %x\n", (unsigned)(unsigned long)src_surface, (unsigned)(unsigned long)dest_surface));
	memcpy(dest_surface, src_surface, 304*192);
}

char *get_screen_ptr(int which)
{
	if (which <= 3)
	{
		return screens[which];
	}

	if (which == 0xff)
	{
		return screen_invisible;
	}

	if (which == 0xfe)
	{
		return screen_visible;
	}

	/* else */
	return screens[0];
}

/** Selects working-screen
    @param which   identifier (see screen identification comment above)
*/
void select_screen(int which)
{
	selected_screen = which;
	selected_screen_ptr = get_screen_ptr(which);
}

/*----------------------
 | screen_arm_fade_restore
 | Description: See screen.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: s_fadeRestorePending
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void screen_arm_fade_restore(void)
{
	s_fadeRestorePending = 1;
	s_fadeInPending = 0;
	s_fadeInStep = 0;
}

/*----------------------
 | screen_arm_fade_in
 | Description: See screen.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: s_fadeInPending, s_fadeRestorePending
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void screen_arm_fade_in(void)
{
	s_fadeInPending = 1;
	s_fadeRestorePending = 0;
}

/*----------------------
 | screen_fade_cancel
 | Description: See screen.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: s_fadeRestorePending, s_fadeInPending, s_fadeInStep
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void screen_fade_cancel(void)
{
	s_fadeRestorePending = 0;
	s_fadeInPending = 0;
	s_fadeInStep = 0;
}

/*----------------------
 | screen_fade_in_pump
 | Description: Walks the deferred ramp down to normal on the clock, one write
 |   per step it has actually reached.
 | Author: suinevere
 | Dependencies: video.h, fadecalc.h, platform.h
 | Globals: s_fadeInStep, s_fadeInStart
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void screen_fade_in_pump(void)
{
	unsigned int elapsed;
	int step;

	if (s_fadeInStep <= 0)
	{
		return;
	}

	elapsed = platform_ticks() - s_fadeInStart;
	step = FADECALC_SEGA_CD_STEPS - (int)(elapsed / SCREEN_FADE_IN_HOLD_MS);

	if (step < 0)
	{
		step = 0;
	}

	if (step != s_fadeInStep)
	{
		s_fadeInStep = step;
		video_set_fade(fadecalc_step_level(step, FADECALC_SEGA_CD_STEPS));
	}
}

/** Renders (rasters) the requested screen
    @param which   screen identifier (see comment above)

    Called by script when a frame is about to complete, and rendering
    is to be done before proceeding to the next frame. this will
    convert the 4-bit framebuffer to the native video output format. it
    is also possible to flip between visible and invisible buffers using
    which == 0xff

    This is also the game's draw choke point, and so the place a deferred
    brightness restore is spent: see screen_arm_fade_restore in screen.h.
*/
void update_screen(int which)
{
	char *src;

	if (which == 0xfe)
	{
		src = screen_visible;
	}
	else if (which == 0xff)
	{
		char *d1, *d2;

		/* swap between visible and invisible */
		d1 = screen_visible;
		d2 = screen_invisible;
		screen_invisible = d1;
		screen_visible = d2;

		src = screen_visible;
	}
	else
	{
		screen_visible = get_screen_ptr(which);
		src = screen_visible;
	}

	video_render(src);

	/* Both arms are spent here, after the pixels, and neither when it was
	   armed. A fade blacks the screen by darkening the palette while the
	   framebuffer still holds the outgoing scene, so lifting it before
	   anything new is drawn shows that old scene at full brightness for the
	   gap before the first frame lands. */
	if (s_fadeInPending)
	{
		s_fadeInPending = 0;

		if (video_get_fade() < FADECALC_LEVEL_NORMAL)
		{
			s_fadeInStep = FADECALC_SEGA_CD_STEPS;
			s_fadeInStart = platform_ticks();
		}
	}

	screen_fade_in_pump();

	if (s_fadeRestorePending)
	{
		s_fadeRestorePending = 0;
		video_set_fade(FADECALC_LEVEL_NORMAL);
	}
}

/** Returns the identifier of the working-screen
    @returns screen identifier
*/
int get_selected_screen()
{
	return selected_screen;
}

/** Returns framebuffer address of working-screen
    @returns pointer to framebuffer
*/
char *get_selected_screen_ptr()
{
	return selected_screen_ptr;
}

/** Initializes screen module
    @returns zero on success, negative value on error
*/
int screen_init()
{
	/* screen 1 and 2 share same framebuffer! */
	screens[0] = (char *)huge_buf + 0;
	screens[1] = (char *)huge_buf + 304*192;
	screens[2] = screens[1];
	screens[3] = (char *)huge_buf + 304*192*2;

	screen_visible = get_screen_ptr(3);
	screen_invisible = get_screen_ptr(1);
	select_screen(0xfe);
	return 0;
}
