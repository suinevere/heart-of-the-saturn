/*----------------------
 | discsec.c
 | Description: Implementation of discsec.h. Pure arithmetic over
 |   caller-supplied values, so none of this needs a disc, an emulator or SRL
 |   to test -- see saturn/tests/test_discsec.c.
 | Author: suinevere
 | Dependencies: discsec.h
 ----------------------*/
#include "discsec.h"

int32_t discsec_whole_sectors(int32_t bytes, int32_t sector_size)
{
    if (sector_size <= 0 || bytes < 0)
    {
        return 0;
    }

    return bytes / sector_size;
}

int32_t discsec_tail_bytes(int32_t bytes, int32_t sector_size)
{
    if (sector_size <= 0 || bytes < 0)
    {
        return 0;
    }

    return bytes % sector_size;
}
