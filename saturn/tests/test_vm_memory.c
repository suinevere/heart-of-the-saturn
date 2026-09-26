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

static void test_alloc_then_pointer_is_usable(void)
{
	unsigned char *base;

	assert(vm_alloc_memory() == 1);

	base = get_memory_ptr(0);
	assert(base != NULL);

	base[0] = 0xAA;
	base[MEMORY_SIZE - 1] = 0x55;
	assert(base[0] == 0xAA);
	assert(base[MEMORY_SIZE - 1] == 0x55);

	assert(vm_alloc_memory() == 1);
	assert(get_memory_ptr(0) == base);

	vm_free_memory();
}

int main(void)
{
	test_size_is_the_named_constant();
	test_size_covers_the_largest_load();
	test_size_is_not_a_pointer_width();
	test_alloc_then_pointer_is_usable();
	printf("test_vm_memory: all passed\n");
	return 0;
}
