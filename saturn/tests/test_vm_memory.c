/*----------------------
 | test_vm_memory.c
 | Description: Guards the emulated 68000 map's size contract. get_memory_size()
 |   returned sizeof(memory) while memory was a static array; once it becomes a
 |   pointer, sizeof silently yields 4 and every caller gets a 4-byte buffer
 |   with no diagnostic. These tests pin the value so that regression cannot
 |   land quietly. The bound itself comes from the three fixed load sites --
 |   main.c:116 loads ROOMSn.BIN at 0xf900 (largest 370,688) and
 |   animation.c:870 loads animations at up to 0x809a (largest 436,224) --
 |   whose worst case is 469,146 bytes.
 | Author: suinevere
 | Dependencies: vm.h
 ----------------------*/
#include <assert.h>
#include <stdio.h>
#include "vm.h"

/* The worst case computed from the manifest in disc_cue.c and the two fixed
   load offsets. If a future disc or load site pushes past this, the assert
   below is where it must be noticed. */
#define WORST_CASE_HIGH_WATER 469146

static void test_size_is_the_named_constant(void)
{
	assert(get_memory_size() == MEMORY_SIZE);
	assert(MEMORY_SIZE == 0x80000);
}

static void test_size_covers_the_worst_measured_load(void)
{
	assert(get_memory_size() > WORST_CASE_HIGH_WATER);
}

static void test_size_is_not_a_pointer_width(void)
{
	/* The exact failure this file exists to catch. */
	assert(get_memory_size() != (int)sizeof(void *));
}

int main(void)
{
	test_size_is_the_named_constant();
	test_size_covers_the_worst_measured_load();
	test_size_is_not_a_pointer_width();
	printf("test_vm_memory: all passed\n");
	return 0;
}
