/*----------------------
 | test_cdda_classify.c
 | Description: Host unit tests for cdda_classify.c. Built and run by
 |   run_tests.sh with the host gcc, never by the Saturn makefile -- that
 |   globs src/ under saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: cdda_classify.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "cdda_classify.h"

static int g_fail = 0;

static const char *action_name(cdda_action a)
{
    switch (a) {
    case CDDA_FORGET:  return "CDDA_FORGET";
    case CDDA_RESUME:  return "CDDA_RESUME";
    case CDDA_RESTART: return "CDDA_RESTART";
    default:           return "?";
    }
}

typedef struct {
    const char *name;
    int was_playing;
    int loop;
    int observed;
    uint32_t fad;
    uint32_t start;
    uint32_t end;
    cdda_action expected;
} test_case;

/* A representative track spans frames 1000..2000. */
static const test_case CASES[] = {
    { "interrupted mid-track, one-shot -> RESUME",
      1, 0, 0, 1500, 1000, 2000, CDDA_RESUME },

    { "animation case: head below start, not playing -> RESTART",
      0, 0, 0, 500, 1000, 2000, CDDA_RESTART },

    { "one-shot ran to completion, observed -> FORGET",
      0, 0, 1, 2500, 1000, 2000, CDDA_FORGET },

    /* Regression for defect 1: an infinite-repeat CDC_CdPlay re-seeks to its
       own start once it reaches its end, and mid-re-seek looks identical to
       a finished one-shot on was_playing/fad alone -- not playing, head at
       or past end. loop != 0 must still classify this as CDDA_RESTART, never
       CDDA_FORGET, or a looping track goes silent forever the first time a
       read lands in that window. */
    { "looping track sampled mid-wrap -> RESTART (defect 1 regression)",
      0, 1, 1, 2000, 1000, 2000, CDDA_RESTART },

    /* Regression for defect 2: play a high-numbered track, then a
       low-numbered one, then read immediately -- the head is still parked
       high from the previous track, at or past this track's end, even
       though this track never played. observed == 0 must keep this out of
       CDDA_FORGET. */
    { "backwards track jump -> RESTART (defect 2 regression)",
      0, 0, 0, 2000, 1000, 2000, CDDA_RESTART },

    { "looping track interrupted mid-track -> RESTART",
      1, 1, 0, 1500, 1000, 2000, CDDA_RESTART },

    { "unreadable TOC -> RESTART",
      0, 0, 1, 1500, 0, 0, CDDA_RESTART },

    { "boundary: fad == end exactly, observed one-shot -> FORGET",
      0, 0, 1, 2000, 1000, 2000, CDDA_FORGET },

    { "boundary: fad == start exactly, playing -> RESUME",
      1, 0, 0, 1000, 1000, 2000, CDDA_RESUME },

    { "end == 0 with a plausible-looking fad -> RESTART",
      0, 0, 1, 1500, 1000, 0, CDDA_RESTART },

    /* Regression for the start != 0 term itself: end != 0 and fad >= end are
       both already satisfied here, so the end-side checks alone would let
       this row wrongly return CDDA_FORGET. Only the start != 0 term blocks
       it -- deleting that term from cdda_classify.c turns this row green
       for the wrong reason and the whole suite still passes. */
    { "start unreadable, end readable, observed, fad past end -> RESTART (start != 0 term regression)",
      0, 0, 1, 2500, 0, 2000, CDDA_RESTART },

    /* Pins fad < end, not fad <= end, in the resume rule: a resumed range
       has length end - fad, and fad == end would issue CDC_CdPlay with a
       zero-length EFAS if that comparison were ever relaxed to <=. */
    { "boundary: fad == end exactly, playing, no loop -> RESTART (fad < end pin)",
      1, 0, 0, 2000, 1000, 2000, CDDA_RESTART },
};

static void run_cases(void)
{
    size_t i;

    for (i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        const test_case *c = &CASES[i];
        cdda_action got = cdda_classify(c->was_playing, c->loop, c->observed,
                                         c->fad, c->start, c->end);

        if (got != c->expected) {
            g_fail++;
            printf("FAIL %s\n  actual   = %s\n  expected = %s\n",
                   c->name, action_name(got), action_name(c->expected));
        }
    }
}

int main(void)
{
    run_cases();

    if (g_fail != 0) {
        printf("%d cdda_classify check(s) failed\n", g_fail);
        return 1;
    }

    printf("cdda_classify: all checks passed\n");
    return 0;
}
