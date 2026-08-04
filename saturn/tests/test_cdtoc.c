/*----------------------
 | test_cdtoc.c
 | Description: Host unit tests for cdtoc.c. Built and run by run_tests.sh with
 |   the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: cdtoc.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "cdtoc.h"

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

#define CTRL_AUDIO   0x0
#define CTRL_DATA    0x4
#define CTRL_ABSENT  0xf

static uint32_t entry(int ctrl, uint32_t fad)
{
    return ((uint32_t)ctrl << 28) | (fad & 0x00ffffffu);
}

static uint32_t record(int ctrl, int track)
{
    return ((uint32_t)ctrl << 28) | ((uint32_t)track << 16);
}

/* This disc: TRACK 01 data, TRACK 02..42 audio, mirroring what
   HOTA_AUDIO=full lays down. Each track is given a distinct, easily checked
   frame address. */
static void build_hota_disc(uint32_t *toc)
{
    int t;

    for (t = 0; t < CDTOC_WORDS; t++) {
        toc[t] = 0xffffffffu;
    }

    toc[0] = entry(CTRL_DATA, 150);
    for (t = 2; t <= 42; t++) {
        toc[t - 1] = entry(CTRL_AUDIO, 1000u + (uint32_t)t * 100u);
    }

    toc[CDTOC_FIRST_WORD]   = record(CTRL_DATA, 1);
    toc[CDTOC_LAST_WORD]    = record(CTRL_AUDIO, 42);
    toc[CDTOC_LEADOUT_WORD] = entry(CTRL_AUDIO, 9000);
}

/* The default HOTA_AUDIO=none build: one data track and nothing else. */
static void build_data_only_disc(uint32_t *toc)
{
    int t;

    for (t = 0; t < CDTOC_WORDS; t++) {
        toc[t] = 0xffffffffu;
    }

    toc[0] = entry(CTRL_DATA, 150);
    toc[CDTOC_FIRST_WORD]   = record(CTRL_DATA, 1);
    toc[CDTOC_LAST_WORD]    = record(CTRL_DATA, 1);
    toc[CDTOC_LEADOUT_WORD] = entry(CTRL_DATA, 5300);
}

static void test_is_audio(void)
{
    uint32_t toc[CDTOC_WORDS];
    build_hota_disc(toc);

    CHECK_EQ(cdtoc_is_audio(toc, 1), 0);
    CHECK_EQ(cdtoc_is_audio(toc, 2), 1);
    CHECK_EQ(cdtoc_is_audio(toc, 33), 1);
    CHECK_EQ(cdtoc_is_audio(toc, 42), 1);

    /* Past the end of the disc: the entry reads 0xFFFFFFFF, control 0x0f. */
    CHECK_EQ(cdtoc_is_audio(toc, 43), 0);
    CHECK_EQ(cdtoc_is_audio(toc, 99), 0);

    CHECK_EQ(cdtoc_is_audio(toc, 0), 0);
    CHECK_EQ(cdtoc_is_audio(toc, -1), 0);
    CHECK_EQ(cdtoc_is_audio(toc, 100), 0);
    CHECK_EQ(cdtoc_is_audio(0, 2), 0);
}

static void test_track_start(void)
{
    uint32_t toc[CDTOC_WORDS];
    build_hota_disc(toc);

    CHECK_EQ(cdtoc_track_start(toc, 1), 150);
    CHECK_EQ(cdtoc_track_start(toc, 2), 1200);

    /* Engine music index 31, the first intro animation, maps to cue 33. */
    CHECK_EQ(cdtoc_track_start(toc, 33), 4300);
    CHECK_EQ(cdtoc_track_start(toc, 42), 5200);

    CHECK_EQ(cdtoc_track_start(toc, 43), 0);
    CHECK_EQ(cdtoc_track_start(toc, 0), 0);
    CHECK_EQ(cdtoc_track_start(toc, 100), 0);
    CHECK_EQ(cdtoc_track_start(0, 2), 0);
}

static void test_track_end(void)
{
    uint32_t toc[CDTOC_WORDS];
    build_hota_disc(toc);

    /* A track ends where the next one begins. */
    CHECK_EQ(cdtoc_track_end(toc, 33), cdtoc_track_start(toc, 34));
    CHECK_EQ(cdtoc_track_end(toc, 33), 4400);

    /* The last track ends at the lead-out, which is the only thing that can
       bound it -- there is no next track to read. */
    CHECK_EQ(cdtoc_track_end(toc, 42), 9000);

    CHECK_EQ(cdtoc_track_end(toc, 43), 0);
    CHECK_EQ(cdtoc_track_end(toc, 0), 0);
    CHECK_EQ(cdtoc_track_end(0, 2), 0);

    /* Every track must measure positive, or a resume would compute a
       negative-length playback range. */
    {
        int t;
        for (t = 2; t <= 42; t++) {
            CHECK(cdtoc_track_end(toc, t) > cdtoc_track_start(toc, t));
        }
    }
}

static void test_max_audio_track(void)
{
    uint32_t toc[CDTOC_WORDS];

    build_hota_disc(toc);
    CHECK_EQ(cdtoc_max_audio_track(toc), 42);

    build_data_only_disc(toc);
    CHECK_EQ(cdtoc_max_audio_track(toc), 0);

    CHECK_EQ(cdtoc_max_audio_track(0), 0);
}

static void test_unreadable_toc(void)
{
    uint32_t toc[CDTOC_WORDS];
    int t;

    /* No disc, or a read issued before the drive was ready: every longword
       reads back as absent, including the first/last records. Everything must
       answer "unknown" rather than inventing a frame address. */
    for (t = 0; t < CDTOC_WORDS; t++) {
        toc[t] = 0xffffffffu;
    }

    CHECK_EQ(cdtoc_max_audio_track(toc), 0);
    CHECK_EQ(cdtoc_is_audio(toc, 2), 0);
    CHECK_EQ(cdtoc_track_start(toc, 2), 0);
    CHECK_EQ(cdtoc_track_end(toc, 2), 0);
}

static void test_srl_offset_bug_regression(void)
{
    uint32_t toc[CDTOC_WORDS];
    int t;

    /* The bug this module exists to avoid: SRL::Cd::TableOfContents reads
       track t from longword 2t, because sizeof(TrackLocation) is 8 rather
       than 4. A 99-track disc populates both t-1 and 2t with distinct frame
       addresses, so a decoder with that fault returns the wrong one instead
       of failing to compile or reading absent data. */
    for (t = 0; t < CDTOC_WORDS; t++) {
        toc[t] = 0xffffffffu;
    }
    for (t = 1; t <= CDTOC_MAX_TRACK; t++) {
        toc[t - 1] = entry(CTRL_AUDIO, 10000u + (uint32_t)t);
    }
    toc[CDTOC_FIRST_WORD]   = record(CTRL_AUDIO, 1);
    toc[CDTOC_LAST_WORD]    = record(CTRL_AUDIO, CDTOC_MAX_TRACK);
    toc[CDTOC_LEADOUT_WORD] = entry(CTRL_AUDIO, 20000);

    CHECK_EQ(cdtoc_track_start(toc, 33), 10033);
    CHECK_EQ(cdtoc_track_start(toc, 33) != 10066, 1);
    CHECK_EQ(cdtoc_track_start(toc, 2), 10002);
    CHECK_EQ(cdtoc_track_start(toc, 99), 10099);
    CHECK_EQ(cdtoc_max_audio_track(toc), 99);
}

int main(void)
{
    test_is_audio();
    test_track_start();
    test_track_end();
    test_max_audio_track();
    test_unreadable_toc();
    test_srl_offset_bug_regression();

    if (g_fail != 0) {
        printf("%d cdtoc check(s) failed\n", g_fail);
        return 1;
    }

    printf("cdtoc: all checks passed\n");
    return 0;
}
