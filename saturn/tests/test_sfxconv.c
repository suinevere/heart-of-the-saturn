/*----------------------
 | test_sfxconv.c
 | Description: Host unit tests for sfxconv.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 |
 |   The decode table is pinned entry by entry against the two-branch form in
 |   src/sound.c, which is the only reference implementation anyone here can
 |   run. A wrong table does not error; it produces sound, just the wrong
 |   sound, and diagnosing that by ear on an emulator costs a round trip per
 |   attempt.
 | Author: suinevere
 | Dependencies: sfxconv.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "sfxconv.h"
#include "vm.h"

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n"                      \
                   "  expected = %lld\n",                                     \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

/* src/sound.c's decode, transcribed byte for byte including its overflow:
   `s` is a signed char, so u == 0x80 assigns 128 and lands on -128. That is
   sign-magnitude negative zero, whose correct value is 0. The host is the
   reference this port is matched against, so the host's answer is what is
   pinned -- see the design spec's "The 0x80 decode edge". */
static signed char host_decode(unsigned char u)
{
	signed char s = 0;

	if (u > 0x80)
	{
		s = 0 - (u & 0x7f);
	}
	else if (u <= 0x80)
	{
		s = u;
	}

	return s;
}

static void test_decode_table_matches_host(void)
{
	int u;

	for (u = 0; u <= 0xff; u++)
	{
		CHECK_EQ(sfxconv_decode_byte((unsigned char)u), host_decode((unsigned char)u));
	}
}

static void test_decode_edges(void)
{
	CHECK_EQ(sfxconv_decode_byte(0x00), 0);
	CHECK_EQ(sfxconv_decode_byte(0x7f), 127);
	CHECK_EQ(sfxconv_decode_byte(0x80), -128);
	CHECK_EQ(sfxconv_decode_byte(0x81), -1);
	CHECK_EQ(sfxconv_decode_byte(0xff), -127);
}

/* The three indirections src/sound.c walks, laid into a real map so the walk
   is exercised rather than described:
       table   = get_long(0xf90c)
       entry   = get_long(table + index * 4)
       length  = get_long(entry)
       data    = entry + 8
   The four bytes between the length and the data are called "some unknown
   flags" by sound.c and are never read. */
static void put_long(int offset, unsigned long value)
{
	unsigned char *m = get_memory_ptr(offset);

	m[0] = (unsigned char)(value >> 24);
	m[1] = (unsigned char)(value >> 16);
	m[2] = (unsigned char)(value >> 8);
	m[3] = (unsigned char)(value);
}

#define TEST_TABLE 0x20000
#define TEST_ENTRY 0x30000

/* Every case below uses index 0, so the entry sits at `table` itself. The two
   range guards are not defensiveness: the out-of-range cases deliberately pass
   a table or an entry at or past MEMORY_SIZE, and writing there would run off
   the end of the host's 512 KB static map and corrupt whatever .bss follows
   it -- the test would then be measuring its own damage. */
static void build_map(unsigned long table, unsigned long entry, unsigned long length)
{
	memset(get_memory_ptr(0), 0, MEMORY_SIZE);
	put_long(0xf90c, table);

	if (table + 4 <= (unsigned long)MEMORY_SIZE)
	{
		put_long((int)table, entry);
	}

	if (entry + 8 <= (unsigned long)MEMORY_SIZE)
	{
		put_long((int)entry, length);
	}
}

static void test_locate_well_formed(void)
{
	int offset = -1;
	int length = -1;

	build_map(TEST_TABLE, TEST_ENTRY, 1000);

	CHECK_EQ(sfxconv_locate(0, &offset, &length), 1);
	CHECK_EQ(offset, TEST_ENTRY + 8);
	CHECK_EQ(length, 1000);
}

static void test_locate_refuses_bad_maps(void)
{
	int offset = -1;
	int length = -1;

	build_map(MEMORY_SIZE, TEST_ENTRY, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);
	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);

	build_map(TEST_TABLE, MEMORY_SIZE, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);

	build_map(TEST_TABLE, TEST_ENTRY, 0);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);

	build_map(TEST_TABLE, MEMORY_SIZE - 16, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);

	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);
}

static void test_padded_size(void)
{
	CHECK_EQ(sfxconv_padded_size(1), SFXCONV_MIN_PLAYABLE);
	CHECK_EQ(sfxconv_padded_size(SFXCONV_MIN_PLAYABLE - 1), SFXCONV_MIN_PLAYABLE);
	CHECK_EQ(sfxconv_padded_size(SFXCONV_MIN_PLAYABLE), SFXCONV_MIN_PLAYABLE);
	CHECK_EQ(sfxconv_padded_size(SFXCONV_MIN_PLAYABLE + 1), SFXCONV_MIN_PLAYABLE + 1);
}

static void test_decode_into_pads_short_samples(void)
{
	static signed char dst[SFXCONV_MIN_PLAYABLE];
	unsigned char *m;
	int i;

	memset(get_memory_ptr(0), 0, MEMORY_SIZE);
	m = get_memory_ptr(TEST_ENTRY + 8);
	m[0] = 0x00;
	m[1] = 0x7f;
	m[2] = 0x80;
	m[3] = 0xff;

	memset(dst, 0x5a, sizeof(dst));
	sfxconv_decode_into(TEST_ENTRY + 8, 4, dst, SFXCONV_MIN_PLAYABLE);

	CHECK_EQ(dst[0], 0);
	CHECK_EQ(dst[1], 127);
	CHECK_EQ(dst[2], -128);
	CHECK_EQ(dst[3], -127);

	for (i = 4; i < SFXCONV_MIN_PLAYABLE; i++)
	{
		if (dst[i] != 0)
		{
			g_fail++;
			printf("FAIL padding not zeroed at %d (= %d)\n", i, (int)dst[i]);
			break;
		}
	}
}

static void test_decode_into_writes_nothing_past_length(void)
{
	static signed char dst[SFXCONV_MIN_PLAYABLE + 4];
	int i;

	memset(get_memory_ptr(0), 0, MEMORY_SIZE);
	memset(dst, 0x5a, sizeof(dst));

	sfxconv_decode_into(TEST_ENTRY + 8, SFXCONV_MIN_PLAYABLE + 4, dst,
	                    SFXCONV_MIN_PLAYABLE + 4);

	for (i = 0; i < SFXCONV_MIN_PLAYABLE + 4; i++)
	{
		if (dst[i] != 0)
		{
			g_fail++;
			printf("FAIL decoded byte %d is %d, expected 0\n", i, (int)dst[i]);
			break;
		}
	}
}

int main(void)
{
	test_decode_table_matches_host();
	test_decode_edges();

	if (vm_alloc_memory() == 0)
	{
		printf("test_sfxconv: vm_alloc_memory failed\n");
		return 1;
	}

	test_locate_well_formed();
	test_locate_refuses_bad_maps();
	test_padded_size();
	test_decode_into_pads_short_samples();
	test_decode_into_writes_nothing_past_length();

	if (g_fail != 0)
	{
		printf("test_sfxconv: %d failure(s)\n", g_fail);
		return 1;
	}

	printf("test_sfxconv: all pass\n");
	return 0;
}
