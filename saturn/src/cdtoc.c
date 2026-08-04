/*----------------------
 | cdtoc.c
 | Description: Implementation of cdtoc.h. Every function takes the TOC buffer
 |   rather than owning one, so none of this needs a disc, an emulator or SRL
 |   to test -- see saturn/tests/test_cdtoc.c.
 | Author: suinevere
 | Dependencies: cdtoc.h
 ----------------------*/
#include "cdtoc.h"

/*----------------------
 | cdtoc_ctrl
 | Description: The control nibble of a TOC longword -- the high 4 bits of the
 |   ctrladr byte, which is itself the top byte.
 | Author: suinevere
 | Globals: N/A
 | Params: word -- one TOC longword
 | Returns: the nibble, 0..15
 ----------------------*/
static int cdtoc_ctrl(uint32_t word)
{
    return (int)((word >> 28) & 0xfu);
}

/*----------------------
 | cdtoc_fad
 | Description: The frame address packed into the low 24 bits of a TOC
 |   longword.
 | Author: suinevere
 | Globals: N/A
 | Params: word -- one TOC longword
 | Returns: the frame address
 ----------------------*/
static uint32_t cdtoc_fad(uint32_t word)
{
    return word & 0x00ffffffu;
}

/*----------------------
 | cdtoc_absent
 | Description: Whether a TOC entry describes nothing. The BIOS writes
 |   0xFFFFFFFF for tracks the disc does not have, and control 0x0f is the
 |   marker; treating such an entry as real yields a frame address of
 |   0xffffff, which is a seek off the end of the disc.
 | Author: suinevere
 | Globals: N/A
 | Params: word -- one TOC longword
 | Returns: 1 if the entry is absent, else 0
 ----------------------*/
static int cdtoc_absent(uint32_t word)
{
    return cdtoc_ctrl(word) == 0xf;
}

/*----------------------
 | cdtoc_record_track_no
 | Description: The track number carried in a first-track or last-track
 |   record, which store it in bits 16..23 rather than as a frame address.
 |   Returns 0 when the value is outside 1..99, which is what a TOC read
 |   before the drive was ready looks like.
 | Author: suinevere
 | Globals: N/A
 | Params: toc -- the TOC buffer; word -- CDTOC_FIRST_WORD or CDTOC_LAST_WORD
 | Returns: the track number, or 0 if unreadable
 ----------------------*/
static int cdtoc_record_track_no(const uint32_t *toc, int word)
{
    int n = (int)((toc[word] >> 16) & 0xffu);

    return (n >= 1 && n <= CDTOC_MAX_TRACK) ? n : 0;
}

int cdtoc_is_audio(const uint32_t *toc, int track)
{
    int ctrl;

    if (toc == 0 || track < 1 || track > CDTOC_MAX_TRACK)
    {
        return 0;
    }

    ctrl = cdtoc_ctrl(toc[track - 1]);

    return (ctrl != 0xf) && ((ctrl & 0x4) == 0);
}

uint32_t cdtoc_track_start(const uint32_t *toc, int track)
{
    if (toc == 0 || track < 1 || track > CDTOC_MAX_TRACK)
    {
        return 0;
    }

    if (cdtoc_absent(toc[track - 1]))
    {
        return 0;
    }

    return cdtoc_fad(toc[track - 1]);
}

uint32_t cdtoc_track_end(const uint32_t *toc, int track)
{
    int last;

    if (toc == 0 || track < 1 || track > CDTOC_MAX_TRACK)
    {
        return 0;
    }

    if (cdtoc_absent(toc[track - 1]))
    {
        return 0;
    }

    last = cdtoc_record_track_no(toc, CDTOC_LAST_WORD);

    if (last == 0)
    {
        return 0;
    }

    if (track >= last || cdtoc_absent(toc[track]))
    {
        return cdtoc_fad(toc[CDTOC_LEADOUT_WORD]);
    }

    return cdtoc_fad(toc[track]);
}

int cdtoc_max_audio_track(const uint32_t *toc)
{
    int first;
    int last;
    int track;
    int best = 0;

    if (toc == 0)
    {
        return 0;
    }

    first = cdtoc_record_track_no(toc, CDTOC_FIRST_WORD);
    last = cdtoc_record_track_no(toc, CDTOC_LAST_WORD);

    if (first == 0 || last == 0 || first > last)
    {
        return 0;
    }

    for (track = first; track <= last; track++)
    {
        if (cdtoc_is_audio(toc, track))
        {
            best = track;
        }
    }

    return best;
}
