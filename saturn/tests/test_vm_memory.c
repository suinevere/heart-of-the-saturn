/*----------------------
 | test_vm_memory.c
 | Description: Guards the emulated 68000 map's size contract. get_memory_size()
 |   returned sizeof(memory) while memory was a static array; once it becomes a
 |   pointer, sizeof silently yields 4 and every caller gets a 4-byte buffer
 |   with no diagnostic -- that is the exact failure the pointer-width test
 |   below exists to catch. The size itself is not arbitrary: main.c:116 loads
 |   ROOMSn.BIN at 0xf900 (largest 370,688) and animation.c:869 loads
 |   animations at up to 0x809a (largest 432,128), so the last byte any load
 |   site writes is 465,050 and the map must hold at least that. The delta-
 |   unpack scratch that used to sit at 0xdc000 is a host array in animation.c
 |   as of 2026-08-04 and no longer bounds this.
 | Author: suinevere
 | Dependencies: vm.h
 ----------------------*/
#include <assert.h>
#include <stdio.h>
#include "vm.h"

#define LARGEST_LOAD_END 465050

static void test_size_is_the_named_constant(void)
{
	assert(get_memory_size() == MEMORY_SIZE);
	assert(MEMORY_SIZE == 0x80000);
}

static void test_size_covers_the_largest_load(void)
{
	assert(get_memory_size() >= LARGEST_LOAD_END);
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
