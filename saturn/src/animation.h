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
#ifndef __ANIMATION_INCLUDED__
#define __ANIMATION_INCLUDED__

/** Plays a death sequence
    @param index    animation index in resources
    @param chained  non-zero if another death opcode follows this one

    Death animations are stored as resources in the room file itself,
    along with the rest of the level

    A death written as two adjacent script opcodes is one death, and `chained`
    is how the caller says so: the first segment then spends both segments'
    delay in a single fade and the second cuts straight in. Passing zero
    everywhere is the old behaviour, a fade per segment.
*/
int play_death_animation(int index, int chained);

/** Plays an animation file
    @param filename    name as appears on cd
    @param fileoffset  offset in bytes where to start reading from
    @param track       music index to start once loaded, 0 for none
    @returns zero if played completely, 1 if aborted, negative on error

    The track is passed in rather than started by the caller because there is
    one drive and the load below seizes it. A disc_play_track issued before
    this call is silenced by that read and restarted from its first frame
    afterwards, which is audible as the track beginning twice. Handing the
    index over lets the read finish first and the music start once.
*/
int play_animation(const char *filename, int fileoffset, int track);

#endif
