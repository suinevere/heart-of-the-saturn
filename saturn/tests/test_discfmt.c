/*----------------------
 | test_discfmt.c
 | Description: Host unit tests for discfmt.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: discfmt.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "discfmt.h"

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

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static void test_mode1_user_offset(void)
{
    /* 12 bytes of sync plus a 4-byte header precede the 2048 bytes of user
       data in every MODE1/2352 sector. Sector 0 is the Sega CD header. */
    CHECK_EQ(discfmt_mode1_user_offset(0),  16);

    /* The ISO9660 Primary Volume Descriptor. */
    CHECK_EQ(discfmt_mode1_user_offset(16), 16 * 2352 + 16);

    /* The root directory of this disc. */
    CHECK_EQ(discfmt_mode1_user_offset(20), 20 * 2352 + 16);

    /* END1.BIN, the first entry of the validation manifest. */
    CHECK_EQ(discfmt_mode1_user_offset(2593), 2593 * 2352 + 16);

    /* The last sector of the data track: 5132 sectors total. Computed in 32
       bits without overflow -- 5131 * 2352 is ~12 M, comfortably inside. */
    CHECK_EQ(discfmt_mode1_user_offset(5131), 5131 * 2352 + 16);
}

static void test_sector_span(void)
{
    /* Reads are whole sectors even when the file is not a multiple of one.
       Getting this wrong by a sector truncates the tail of every odd-sized
       file -- of which ROOMS7.BIN is the one that matters. */
    CHECK_EQ(discfmt_sector_span(0),      0);
    CHECK_EQ(discfmt_sector_span(1),      1);
    CHECK_EQ(discfmt_sector_span(2047),   1);
    CHECK_EQ(discfmt_sector_span(2048),   1);
    CHECK_EQ(discfmt_sector_span(2049),   2);

    /* GAME2.BIN: 409600 bytes, exactly 200 sectors. */
    CHECK_EQ(discfmt_sector_span(409600), 200);

    /* ROOMS7.BIN: 160826 bytes, NOT sector-aligned -- 78.5 sectors. */
    CHECK_EQ(discfmt_sector_span(160826), 79);

    /* MAKE2MB.BIN, the largest blob: 436224 bytes. */
    CHECK_EQ(discfmt_sector_span(436224), 213);
}

static void test_iso_name_eq(void)
{
    /* ISO9660 stores a version suffix. A plain strcmp against "END1.BIN"
       matches nothing on this disc, which presents as "every file missing". */
    CHECK(discfmt_iso_name_eq("END1.BIN;1", 10, "END1.BIN"));

    /* Some masterings omit the suffix. Both must work. */
    CHECK(discfmt_iso_name_eq("END1.BIN", 8, "END1.BIN"));

    /* Case-insensitive: the engine asks in uppercase and so does this disc,
       but nothing guarantees that of a remaster. */
    CHECK(discfmt_iso_name_eq("end1.bin;1", 10, "END1.BIN"));

    /* A shared prefix must NOT match. This is the check that stops a lookup
       for ROOMS1.BIN silently returning ROOMS11.BIN on a disc that has one. */
    CHECK(!discfmt_iso_name_eq("ROOMS11.BIN;1", 13, "ROOMS1.BIN"));
    CHECK(!discfmt_iso_name_eq("ROOMS1.BIN;1", 12, "ROOMS11.BIN"));

    /* Different files entirely. */
    CHECK(!discfmt_iso_name_eq("END2.BIN;1", 10, "END1.BIN"));

    /* The record length is authoritative, not a NUL: ISO9660 names are not
       NUL-terminated in the record. Passing a short length must truncate. */
    CHECK(!discfmt_iso_name_eq("END1.BINXX", 4, "END1.BIN"));
}

static void test_cue_track_for_music(void)
{
    /* The disc has 41 audio tracks, TRACK 02 through TRACK 42, and the engine
       indexes music 0..40. 41 onto 41, in order. */
    CHECK_EQ(discfmt_cue_track_for_music(0),  2);
    CHECK_EQ(discfmt_cue_track_for_music(1),  3);
    CHECK_EQ(discfmt_cue_track_for_music(31), 33);  /* INTRO1.BIN's track */
    CHECK_EQ(discfmt_cue_track_for_music(35), 37);  /* MAKE2MB.BIN's track */
    CHECK_EQ(discfmt_cue_track_for_music(40), 42);  /* END4.BIN's track */

    /* The whole point. The deleted music.c built its filename as track + 1,
       because the mp3 rip numbered the 41 AUDIO tracks 01..41 -- audio track 1
       is disc track 2. Reading that as a cue-track formula aims engine index 0
       at TRACK 01, which is the DATA track: noise or a hang on hardware, and
       often silence in an emulator. Nothing may ever return 1. */
    for (int i = 0; i <= 40; i++) {
        CHECK(discfmt_cue_track_for_music(i) >= 2);
        CHECK(discfmt_cue_track_for_music(i) <= 42);
    }

    /* Out of range is refused rather than clamped: a bytecode operand that
       lands here is a bug worth seeing, not one worth papering over. */
    CHECK_EQ(discfmt_cue_track_for_music(-1), 0);
    CHECK_EQ(discfmt_cue_track_for_music(41), 0);
}

int main(void)
{
    test_mode1_user_offset();
    test_sector_span();
    test_iso_name_eq();
    test_cue_track_for_music();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }

    printf("%d check(s) failed\n", g_fail);
    return 1;
}
