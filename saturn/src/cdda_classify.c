/*----------------------
 | cdda_classify.c
 | Description: Implementation of cdda_classify.h. Pure arithmetic over
 |   caller-supplied values, so none of this needs a disc, an emulator or SRL
 |   to test -- see saturn/tests/test_cdda_classify.c.
 | Author: suinevere
 | Dependencies: cdda_classify.h
 ----------------------*/
#include "cdda_classify.h"

cdda_action cdda_classify(int was_playing, int loop, int observed,
                          uint32_t fad, uint32_t start, uint32_t end)
{
    if (!was_playing && loop == 0 && observed &&
        start != 0 && end != 0 && fad >= end)
    {
        return CDDA_FORGET;
    }

    if (was_playing && loop == 0 && start != 0 && end != 0 &&
        fad >= start && fad < end)
    {
        return CDDA_RESUME;
    }

    return CDDA_RESTART;
}
