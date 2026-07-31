/*----------------------
 | discfmt.c
 | Description: Implementation of the pure Sega CD disc-format arithmetic
 |   declared in discfmt.h. Kept to <stdint.h>, <stddef.h> and <string.h> so it
 |   compiles unmodified into the engine, into tools/extract_disc, and into the
 |   host test runner in saturn/tests/run_tests.sh.
 | Author: suinevere
 | Dependencies: discfmt.h
 ----------------------*/
#include "discfmt.h"
#include <string.h>

#define DISCFMT_RAW_SECTOR   2352
#define DISCFMT_USER_SECTOR  2048
#define DISCFMT_SYNC_HEADER  16

#define DISCFMT_MUSIC_FIRST_TRACK 2   /* TRACK 01 is data */
#define DISCFMT_MUSIC_MAX_INDEX   40  /* 41 audio tracks, 02..42 */

uint32_t discfmt_mode1_user_offset(uint32_t lba)
{
    return lba * (uint32_t)DISCFMT_RAW_SECTOR + DISCFMT_SYNC_HEADER;
}

uint32_t discfmt_sector_span(uint32_t size)
{
    return (size + (DISCFMT_USER_SECTOR - 1u)) / DISCFMT_USER_SECTOR;
}

/*----------------------
 | discfmt_ascii_upper
 | Description: Explicit A-Z fold instead of tolower() -- tolower is locale
 |   dependent and would drag in <ctype.h>, and this file must stay free of
 |   anything beyond stdint.h/stddef.h/string.h to remain host-testable.
 | Author: suinevere
 ----------------------*/
static char discfmt_ascii_upper(char c)
{
    if (c >= 'a' && c <= 'z')
    {
        return (char)(c - 'a' + 'A');
    }

    return c;
}

int discfmt_iso_name_eq(const char *iso_name, uint8_t iso_len, const char *want)
{
    uint8_t i;
    size_t want_len = strlen(want);

    for (i = 0; i < iso_len; i++)
    {
        if (iso_name[i] == ';')
        {
            break;
        }

        if (i >= want_len)
        {
            /* iso_name is longer than want at the point want ran out --
               a shared prefix, not a match (e.g. ROOMS11.BIN vs ROOMS1.BIN). */
            return 0;
        }

        if (discfmt_ascii_upper(iso_name[i]) != discfmt_ascii_upper(want[i]))
        {
            return 0;
        }
    }

    /* Both sides must end together: if want has bytes left over, iso_name's
       name (up to ';' or iso_len) was a strict prefix of it and no match. */
    return i == want_len;
}

int discfmt_cue_track_for_music(int engine_index)
{
    if (engine_index < 0 || engine_index > DISCFMT_MUSIC_MAX_INDEX)
    {
        return 0;
    }

    return engine_index + DISCFMT_MUSIC_FIRST_TRACK;
}
