/*
 * Heart of The Alien: GAME2BIN file loader
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
#ifndef __GAME2BIN_INCLUDED__
#define __GAME2BIN_INCLUDED__

/*----------------------
 | game2bin_alloc / game2bin_free
 | Description: Acquires the GAME2BIN_SIZE-byte resident GAME2.BIN buffer.
 |   Split from a plain static array because the Saturn build cannot afford it
 |   in HWRAM alongside vm.c's memory map, code, and framebuffers, so this
 |   buffer lives in LWRAM instead. The host keeps the static array -- it has
 |   the address space, and keeping its layout unchanged is what makes it a
 |   valid reference to bisect Saturn bugs against. Idempotent: a second call
 |   returns the same pointer rather than leaking, so a retried startup path
 |   cannot strand LWRAM. Returns 1 on success, 0 on failure; the caller must
 |   treat failure as fatal, because game2bin_init would then read the disc
 |   into a NULL buffer.
 | Author: suinevere
 ----------------------*/
int game2bin_alloc(void);
void game2bin_free(void);

/** loads GAME2.BIN file into memory
    @returns 0 on success, negative value on error
*/
int game2bin_init();

/** copies a chunk from game2bin
    @param dst     address to destination
    @param offset  offset from game2bin
    @param length  length in bytes
    @returns bytes copied (length on success)
*/
int  copy_from_game2bin(void *dst, int offset, int length);

#endif
