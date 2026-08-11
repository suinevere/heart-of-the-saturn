/*----------------------
 | fadecalc.c
 | Description: Implementation of the palette-fade arithmetic declared in
 |   fadecalc.h. Kept free of every header but its own so it compiles
 |   unmodified into the engine, into both video backends, and into the host
 |   test runner in saturn/tests/run_tests.sh.
 | Author: suinevere
 | Dependencies: fadecalc.h
 ----------------------*/
#include "fadecalc.h"

int fadecalc_scale(int value, int level)
{
    if (value <= 0)
    {
        return 0;
    }

    if (level >= FADECALC_LEVEL_NORMAL)
    {
        return value;
    }

    if (level <= 0)
    {
        return 0;
    }

    /* Rounded rather than truncated: half of 31 is 15.5, and a palette-wide
       truncation is a systematic darkening that survives the fade. The
       product peaks at 255 * 255, well inside 16-bit-safe int range. */
    return (value * level + (FADECALC_LEVEL_NORMAL / 2)) / FADECALC_LEVEL_NORMAL;
}

int fadecalc_step_level(int step, int steps)
{
    if (steps <= 0)
    {
        return 0;
    }

    if (step <= 0)
    {
        return FADECALC_LEVEL_NORMAL;
    }

    if (step >= steps)
    {
        return 0;
    }

    /* Truncated here, unlike fadecalc_scale: the ladder only has to descend
       strictly and land on 0, and rounding it would put two adjacent steps
       on the same level for some step counts -- a fade that shows one fewer
       picture than it was asked for. */
    return ((steps - step) * FADECALC_LEVEL_NORMAL) / steps;
}
