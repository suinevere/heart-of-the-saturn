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
#include <stdio.h>
#include <memory.h>
#include "debug.h"
#include "disc.h"
#include "game2bin.h"

#define GAME2BIN_SIZE 409600

#if defined(HOTA_SATURN)
static char *game2bin = NULL;
#else
static char game2bin_storage[GAME2BIN_SIZE];
static char *game2bin = NULL;
#endif

/*----------------------
 | game2bin_alloc
 | Description: Acquires the GAME2BIN_SIZE-byte resident buffer from LWRAM on
 |   Saturn, or hands back the host's static array unchanged. Idempotent so a
 |   retried startup path cannot strand LWRAM.
 | Author: suinevere
 | Dependencies: game2bin.h
 ----------------------*/
int game2bin_alloc(void)
{
	if (game2bin != NULL)
	{
		return 1;
	}

#if defined(HOTA_SATURN)
	game2bin = (char *)saturn_lwram_alloc(GAME2BIN_SIZE);
#else
	game2bin = game2bin_storage;
#endif

	return (game2bin != NULL);
}

/*----------------------
 | game2bin_free
 | Description: Releases the LWRAM allocation backing the GAME2.BIN buffer on
 |   Saturn; no-op on the host, whose backing store is static.
 | Author: suinevere
 | Dependencies: game2bin.h
 ----------------------*/
void game2bin_free(void)
{
#if defined(HOTA_SATURN)
	if (game2bin != NULL)
	{
		saturn_lwram_free(game2bin);
	}
#endif
	game2bin = NULL;
}

/** Copies a chunk from game2bin
    @param dst     address to destination
    @param offset  offset from game2bin
    @param length  length in bytes
    @returns bytes copied (length on success)
*/
int copy_from_game2bin(void *dst, int offset, int length)
{
	if (offset >= 0 && offset < GAME2BIN_SIZE)
	{
		if (offset + length > GAME2BIN_SIZE)
		{
			length = GAME2BIN_SIZE - offset;
		}

		memcpy(dst, game2bin + offset, length);
		return length;
	}

	return 0;
}

/** Loads GAME2.BIN file into memory
    @returns 0 on success, negative value on error
*/
int game2bin_init()
{
	if (disc_read_file("GAME2.BIN", game2bin, GAME2BIN_SIZE) < 0)
	{
		LOG(("failure reading game2bin file\n"));
		return -1;
	}
	
	LOG(("success reading game2bin file\n"));
	return 0;
}
