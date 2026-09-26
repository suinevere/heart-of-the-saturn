/*
 * Heart of The Alien: Virtual machine primitives (memory and variables)
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
#include <memory.h>
#include <stddef.h>
#include "vm.h"

#define MAX_VARIABLES 256

short variables[MAX_VARIABLES];

#if defined(HOTA_SATURN)
#include "saturn_compat.h"
static unsigned char *memory = NULL;
#else
static unsigned char memory_storage[MEMORY_SIZE];
static unsigned char *memory = NULL;
#endif

int vm_alloc_memory(void)
{
	if (memory != NULL)
	{
		return 1;
	}

#if defined(HOTA_SATURN)
	memory = (unsigned char *)saturn_lwram_alloc(MEMORY_SIZE);
#else
	memory = memory_storage;
#endif

	if (memory == NULL)
	{
		return 0;
	}

	memset(memory, 0, MEMORY_SIZE);

	return 1;
}

void vm_free_memory(void)
{
#if defined(HOTA_SATURN)
	if (memory != NULL)
	{
		saturn_lwram_free(memory);
	}
#endif
	memory = NULL;
}

int auxptr;
static int using_aux = 0;
short auxvars[MAX_TASKS*32];

void set_aux_bank(int bank)
{
	auxptr = (bank * 32);
}

unsigned char get_byte(int offset)
{
	return memory[offset];
}

unsigned short get_word(int offset)
{
	return (get_byte(offset) << 8) | get_byte(offset + 1);
}

unsigned long get_long(int offset)
{
	return (get_word(offset) << 16) | get_word(offset + 2);
}

short get_variable(int var)
{
	if (using_aux)
	{
		return auxvars[auxptr + var];
	}

	return variables[var];
}

void set_variable(int var, short value)
{
	if (using_aux)
	{
		auxvars[auxptr + var] = value;
		return;
	}

	variables[var] = value;
}

void copy_tls_to_global(int dst_index, int src_index, int count)
{
	while (count >= 0)
	{
		variables[dst_index++] = get_variable(src_index++);
		count--;
	}
}

void copy_global_to_tls(int dst_index, int src_index, int count)
{
	while (count >= 0)
	{
		set_variable(dst_index++, variables[src_index++]);
		count--;
	}
}

int toggle_aux(int toggle)
{
	int old = using_aux;
	using_aux = toggle;
	return old;
}

unsigned char *get_memory_ptr(int offset)
{
	return (unsigned char *)memory + offset;
}

int get_memory_size(void)
{
	return MEMORY_SIZE;
}

void vm_reset()
{
	auxptr = 0;
	using_aux = 0;
	memset(variables, '\0', sizeof(variables));
	memset(auxvars, '\0', sizeof(auxvars));
}
