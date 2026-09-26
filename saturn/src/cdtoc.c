#include "cdtoc.h"

static int cdtoc_ctrl(uint32_t word)
{
    return (int)((word >> 28) & 0xfu);
}

static uint32_t cdtoc_fad(uint32_t word)
{
    return word & 0x00ffffffu;
}

static int cdtoc_absent(uint32_t word)
{
    return cdtoc_ctrl(word) == 0xf;
}

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
