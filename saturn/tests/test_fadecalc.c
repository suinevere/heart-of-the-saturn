/*----------------------
 | test_fadecalc.c
 | Description: Host unit tests for fadecalc.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: fadecalc.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "fadecalc.h"

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

static void test_scale_endpoints(void)
{
    /* Level 255 must be the identity at both backends' depths. Anything less
       and merely setting a palette would dim the picture, which is the kind
       of defect that reads as "the Saturn build looks a bit dark" rather
       than as a fade bug. */
    CHECK_EQ(fadecalc_scale(31, FADECALC_LEVEL_NORMAL), 31);
    CHECK_EQ(fadecalc_scale(255, FADECALC_LEVEL_NORMAL), 255);
    CHECK_EQ(fadecalc_scale(1, FADECALC_LEVEL_NORMAL), 1);

    /* Level 0 is black at every input, including full intensity. */
    CHECK_EQ(fadecalc_scale(31, 0), 0);
    CHECK_EQ(fadecalc_scale(255, 0), 0);

    /* Black stays black at every level -- nothing brightens. */
    for (int level = 0; level <= 255; level++) {
        CHECK_EQ(fadecalc_scale(0, level), 0);
    }
}

static void test_scale_is_monotonic_and_bounded(void)
{
    /* Across the whole 5-bit CRAM range and every level, the result must
       never exceed the input and never go negative, and must never rise as
       the level falls. A fade that brightens one channel on one step shows
       up as a colour shift mid-fade, not as an obvious bug. */
    for (int value = 0; value <= 31; value++) {
        int prev = -1;

        for (int level = 0; level <= 255; level++) {
            int got = fadecalc_scale(value, level);

            CHECK(got >= 0);
            CHECK(got <= value);
            CHECK(got >= prev);
            prev = got;
        }
    }
}

static void test_scale_rounds_to_nearest(void)
{
    /* Half intensity of 31 is 15.5, which must round to 16 rather than
       truncate to 15. Truncation across a whole palette is a systematic
       darkening, and it compounds with the ladder below. */
    CHECK_EQ(fadecalc_scale(31, 128), 16);

    /* 255 at half is 128.0 exactly. */
    CHECK_EQ(fadecalc_scale(255, 128), 128);

    /* A quarter of 31 is 7.75 -> 8. */
    CHECK_EQ(fadecalc_scale(31, 64), 8);
}

static void test_scale_clamps_hostile_input(void)
{
    /* Levels outside 0..255 clamp rather than producing a channel brighter
       than it started or a negative one that wraps in the backends' casts. */
    CHECK_EQ(fadecalc_scale(31, 999), 31);
    CHECK_EQ(fadecalc_scale(31, -1), 0);
    CHECK_EQ(fadecalc_scale(-5, 255), 0);
}

static void test_step_ladder_endpoints(void)
{
    /* Step 0 is the undimmed picture and the last step is black, for any
       step count. Both directions of a fade read off this one ladder, so an
       asymmetry here would make a fade in and a fade out disagree. */
    CHECK_EQ(fadecalc_step_level(0, FADECALC_SEGA_CD_STEPS), FADECALC_LEVEL_NORMAL);
    CHECK_EQ(fadecalc_step_level(FADECALC_SEGA_CD_STEPS, FADECALC_SEGA_CD_STEPS), 0);

    CHECK_EQ(fadecalc_step_level(0, 1), FADECALC_LEVEL_NORMAL);
    CHECK_EQ(fadecalc_step_level(1, 1), 0);
}

static void test_step_ladder_is_the_sega_cd_shape(void)
{
    /* Eight steps, evenly spaced, ending at black. Pinned as literals rather
       than recomputed with the implementation's own formula, so a change to
       that formula fails here instead of agreeing with itself.

       Every gap is exactly 32 except the last, which is 31 -- the closest an
       integer ladder gets to even across 0..255 in eight steps. */
    static const int want[FADECALC_SEGA_CD_STEPS + 1] = {
        255, 223, 191, 159, 127, 95, 63, 31, 0
    };
    int i;

    for (i = 0; i <= FADECALC_SEGA_CD_STEPS; i++) {
        CHECK_EQ(fadecalc_step_level(i, FADECALC_SEGA_CD_STEPS), want[i]);
    }

    /* The even spacing itself, checked rather than eyeballed off the table. */
    for (i = 1; i < FADECALC_SEGA_CD_STEPS; i++) {
        CHECK_EQ(want[i - 1] - want[i], 32);
    }
}

static void test_step_ladder_descends_and_clamps(void)
{
    /* Strictly descending, so every step of a fade out is a visibly darker
       picture than the one before -- eight steps that included a repeat
       would show seven. */
    int i;

    for (i = 1; i <= FADECALC_SEGA_CD_STEPS; i++) {
        CHECK(fadecalc_step_level(i, FADECALC_SEGA_CD_STEPS) <
              fadecalc_step_level(i - 1, FADECALC_SEGA_CD_STEPS));
    }

    /* Out-of-range steps clamp to the ends rather than extrapolating past
       black into negative levels. */
    CHECK_EQ(fadecalc_step_level(-3, FADECALC_SEGA_CD_STEPS), FADECALC_LEVEL_NORMAL);
    CHECK_EQ(fadecalc_step_level(99, FADECALC_SEGA_CD_STEPS), 0);

    /* A step count of zero or less has no ladder to walk; yield black rather
       than dividing by it. */
    CHECK_EQ(fadecalc_step_level(0, 0), 0);
    CHECK_EQ(fadecalc_step_level(1, -4), 0);
}

static void test_full_fade_reaches_black_and_back(void)
{
    /* The property that matters at the call site: walking the ladder out
       lands every channel at 0, and walking it back in restores the exact
       original value. A fade that returned 30 of 31 would leave the picture
       permanently, subtly dark after the first death. */
    int value;

    for (value = 0; value <= 31; value++) {
        CHECK_EQ(fadecalc_scale(value,
                 fadecalc_step_level(FADECALC_SEGA_CD_STEPS, FADECALC_SEGA_CD_STEPS)), 0);
        CHECK_EQ(fadecalc_scale(value, fadecalc_step_level(0, FADECALC_SEGA_CD_STEPS)), value);
    }
}

int main(void)
{
    test_scale_endpoints();
    test_scale_is_monotonic_and_bounded();
    test_scale_rounds_to_nearest();
    test_scale_clamps_hostile_input();
    test_step_ladder_endpoints();
    test_step_ladder_is_the_sega_cd_shape();
    test_step_ladder_descends_and_clamps();
    test_full_fade_reaches_black_and_back();

    if (g_fail == 0) {
        printf("test_fadecalc: all pass\n");
        return 0;
    }

    printf("%d check(s) failed\n", g_fail);
    return 1;
}
