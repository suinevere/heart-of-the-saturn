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

    return ((steps - step) * FADECALC_LEVEL_NORMAL) / steps;
}
