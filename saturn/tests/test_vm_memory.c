/*----------------------
 | test_vm_memory.c
 | Description: Guards the emulated 68000 map's size contract. get_memory_size()
 |   returned sizeof(memory) while memory was a static array; once it becomes a
 |   pointer, sizeof silently yields 4 and every caller gets a 4-byte buffer
 |   with no diagnostic -- that is the exact failure the pointer-width test
 |   below exists to catch.
 |
 |   The size bound is computed here from DISC_MANIFEST_LIST and the two named
 |   load bases, never written down as a literal. A literal would be a claim
 |   nobody checked: the number that reached this file before 2026-08-04 was
 |   reasoned from "the largest animation file is 432,128", which is false --
 |   MAKE2MB.BIN is 436,224. It happens to load 0x109a lower and so ends 154
 |   bytes below the INTRO/END files, which is the only reason the wrong
 |   reasoning reached a right answer. Deriving it removes that luck.
 |
 |   The derivation ignores each animation's fileoffset, which only ever lowers
 |   the base, so the bound stands even if a load site's offset changes or a new
 |   animation is added. That makes it 469,146 against a true current worst case
 |   of 465,050 (INTRO/MID/END at ANIMATION_LOAD_BASE) -- 4,096 bytes
 |   conservative, which costs nothing against the 55 KB of headroom left.
 |
 |   The map is not the only guard: disc_read_file rejects a file larger than
 |   max_size and returns negative rather than truncating, so a map that was too
 |   small would fail loudly at the load. This test is what stops it failing at
 |   all. The delta-unpack scratch that used to sit at 0xdc000 is a host array
 |   in animation.c as of 2026-08-04 and no longer bounds anything here.
 | Author: suinevere
 | Dependencies: vm.h, disc_manifest.h
 ----------------------*/
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "disc_manifest.h"

typedef struct
{
	const char *name;
	long size;
} manifest_row_t;

#define MANIFEST_ROW(name, lba, size) { name, size },
static const manifest_row_t manifest[] = { DISC_MANIFEST_LIST(MANIFEST_ROW) };
#undef MANIFEST_ROW

#define MANIFEST_COUNT ((int)(sizeof(manifest) / sizeof(manifest[0])))

static long load_base_for(const char *name)
{
	if (strncmp(name, "ROOMS", 5) == 0)
	{
		return ROOMS_LOAD_BASE;
	}

	return ANIMATION_LOAD_BASE;
}

static long largest_load_end(void)
{
	long worst = 0;
	int i;

	for (i = 0; i < MANIFEST_COUNT; i++)
	{
		long end = load_base_for(manifest[i].name) + manifest[i].size;
		if (end > worst)
		{
			worst = end;
		}
	}

	return worst;
}

static void test_size_is_the_named_constant(void)
{
	assert(get_memory_size() == MEMORY_SIZE);
	assert(MEMORY_SIZE == 0x80000);
}

static void test_size_covers_the_largest_load(void)
{
	long worst = largest_load_end();

	assert(MANIFEST_COUNT == 19);
	assert(worst > 0);
	assert((long)get_memory_size() >= worst);

	printf("test_vm_memory: worst-case load end %ld, map %d, headroom %ld\n",
	       worst, get_memory_size(), (long)get_memory_size() - worst);
}

static void test_size_is_not_a_pointer_width(void)
{
	assert(get_memory_size() != (int)sizeof(void *));
}

int main(void)
{
	test_size_is_the_named_constant();
	test_size_covers_the_largest_load();
	test_size_is_not_a_pointer_width();
	printf("test_vm_memory: all passed\n");
	return 0;
}
