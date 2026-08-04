/*----------------------
 | test_vm_memory.c
 | Description: Guards the emulated 68000 map's size contract. get_memory_size()
 |   returned sizeof(memory) while memory was a static array; once it becomes a
 |   pointer, sizeof silently yields 4 and every caller gets a 4-byte buffer
 |   with no diagnostic -- that is the exact failure the pointer-width test
 |   below exists to catch. The size itself is not arbitrary: main.c:116 loads
 |   ROOMSn.BIN at 0xf900 (largest 370,688), animation.c:869 loads animations
 |   at up to 0x809a (largest 432,128), and animation.c:746 writes delta-unpack
 |   scratch at 0xdc000 that routine playback reaches via play_sequence. The
 |   scratch base is the highest fixed offset any call site touches, so the
 |   map must extend past it.
 | Author: suinevere
 | Dependencies: vm.h
 ----------------------*/
#include <assert.h>
#include <stdio.h>
#include "vm.h"

#define DELTA_UNPACK_SCRATCH_BASE 0xdc000

static void test_size_is_the_named_constant(void)
{
	assert(get_memory_size() == MEMORY_SIZE);
	assert(MEMORY_SIZE == 0x100000);
}

static void test_size_covers_the_delta_unpack_scratch(void)
{
	assert(get_memory_size() > DELTA_UNPACK_SCRATCH_BASE);
}

static void test_size_is_not_a_pointer_width(void)
{
	assert(get_memory_size() != (int)sizeof(void *));
}

int main(void)
{
	test_size_is_the_named_constant();
	test_size_covers_the_delta_unpack_scratch();
	test_size_is_not_a_pointer_width();
	printf("test_vm_memory: all passed\n");
	return 0;
}
