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

static const test_case CASES[] = {
    { "interrupted mid-track, one-shot -> RESUME",
      1, 0, 0, 1500, 1000, 2000, CDDA_RESUME },

    { "animation case: head below start, not playing -> RESTART",
      0, 0, 0, 500, 1000, 2000, CDDA_RESTART },

    { "one-shot ran to completion, observed -> FORGET",
      0, 0, 1, 2500, 1000, 2000, CDDA_FORGET },

    { "looping track sampled mid-wrap -> RESTART (defect 1 regression)",
      0, 1, 1, 2000, 1000, 2000, CDDA_RESTART },

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

    { "start unreadable, end readable, observed, fad past end -> RESTART (start != 0 term regression)",
      0, 0, 1, 2500, 0, 2000, CDDA_RESTART },

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
