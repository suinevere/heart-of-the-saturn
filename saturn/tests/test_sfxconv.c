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
#include "sfxconv.h"

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

int main(void)
{
	test_decode_table_matches_host();
	test_decode_edges();

	if (g_fail != 0)
	{
		printf("test_sfxconv: %d failure(s)\n", g_fail);
		return 1;
	}

	printf("test_sfxconv: all pass\n");
	return 0;
}
