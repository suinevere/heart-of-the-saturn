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

static void build_map(unsigned long table, unsigned long entry, unsigned long length)
{
	memset(get_memory_ptr(0), 0, MEMORY_SIZE);
	put_long(0xf90c, table);

	if (table <= (unsigned long)MEMORY_SIZE - 4)
	{
		put_long((int)table, entry);
	}

	if (entry <= (unsigned long)MEMORY_SIZE - 8)
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

static void test_locate_accepts_sample_ending_exactly_at_memory_size(void)
{
	int offset = -1;
	int length = -1;
	unsigned long entry = (unsigned long)MEMORY_SIZE - 1000 - 8;

	build_map(TEST_TABLE, entry, 1000);

	CHECK_EQ(sfxconv_locate(0, &offset, &length), 1);
	CHECK_EQ(offset, (int)(entry + 8));
	CHECK_EQ(length, 1000);
}

static void test_locate_refuses_sample_ending_one_past_memory_size(void)
{
	int offset = -1;
	int length = -1;
	unsigned long entry = (unsigned long)MEMORY_SIZE - 1000 - 8 + 1;

	build_map(TEST_TABLE, entry, 1000);

	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);
	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);
}

static void test_locate_refuses_invalid_args(void)
{
	int offset = -1;
	int length = -1;

	build_map(TEST_TABLE, TEST_ENTRY, 1000);

	CHECK_EQ(sfxconv_locate(-1, &offset, &length), 0);
	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);

	CHECK_EQ(sfxconv_locate(0, 0, &length), 0);
	CHECK_EQ(length, -1);

	CHECK_EQ(sfxconv_locate(0, &offset, 0), 0);
	CHECK_EQ(offset, -1);
}

static void test_locate_refuses_negative_table(void)
{
	int offset = -1;
	int length = -1;

	build_map(0xFFFFFFFFUL, TEST_ENTRY, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);
	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);

	build_map(0x80000000UL, TEST_ENTRY, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);
	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);
}

static void test_locate_refuses_negative_entry(void)
{
	int offset = -1;
	int length = -1;

	build_map(TEST_TABLE, 0xFFFFFFFFUL, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);
	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);

	build_map(TEST_TABLE, 0x80000000UL, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);
	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);
}

static void test_locate_refuses_negative_length(void)
{
	int offset = -1;
	int length = -1;

	build_map(TEST_TABLE, TEST_ENTRY, 0xFFFFFFFFUL);
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
	test_locate_accepts_sample_ending_exactly_at_memory_size();
	test_locate_refuses_sample_ending_one_past_memory_size();
	test_locate_refuses_invalid_args();
	test_locate_refuses_negative_table();
	test_locate_refuses_negative_entry();
	test_locate_refuses_negative_length();
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
