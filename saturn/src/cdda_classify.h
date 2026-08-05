/*----------------------
 | cdda_classify.h
 | Description: Pure decision logic for what disc_read_file's suspend/restore
 |   bracket should do with CD-DA after a read: forget the track, resume it
 |   from where the read interrupted it, or restart it. Split out of
 |   disc_srl.cxx -- which owns CDC_GetCurStat/CDC_CdPlay and cannot be
 |   host-compiled -- so this arithmetic, the entire risk surface of the read
 |   bracket, gets the same host-compiled test coverage cdtoc.c has instead of
 |   being provable only by listening to a Saturn emulator.
 |
 |   Deliberately free of SRL, stdio and every engine header, for the same
 |   reason discfmt.h and cdtoc.h are: compiled into the engine and by
 |   saturn/tests/run_tests.sh with the host gcc.
 |
 |   Design: docs/superpowers/specs/2026-08-04-hota-saturn-cdda-design.md
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef CDDA_CLASSIFY_H
#define CDDA_CLASSIFY_H

#include <stdint.h>

/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link, the way the six seam headers did before 7f66fe3. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | cdda_action
 | Description: What cdda_restore should do with the music after a file read.
 |   CDDA_FORGET drops it -- the track finished on its own. CDDA_RESUME
 |   continues it from the frame the read interrupted. CDDA_RESTART starts it
 |   over, and is also the fallback for anything the other two cannot be sure
 |   about, because restarting is a far better failure than silence.
 | Author: suinevere
 ----------------------*/
typedef enum
{
    CDDA_FORGET  = 0,
    CDDA_RESUME  = 1,
    CDDA_RESTART = 2
} cdda_action;

/*----------------------
 | cdda_classify
 | Description: Chooses one of the three cdda_action outcomes from what the
 |   drive was doing and where its head was when a file read pre-empted it.
 |
 |   was_playing and observed answer different questions and both are needed.
 |   was_playing is this instant's status: was the drive in CDC_ST_PLAY the
 |   moment the read took over. observed is history: has this track actually
 |   been seen playing, at any earlier suspend, since it was last commanded. A
 |   track can only have run to completion if it was observed running at some
 |   point, so a track that was only ever commanded -- never observed -- is
 |   not condemned as finished by a stale head position left over from
 |   whatever played before it. That is exactly what a backwards track jump
 |   (play a high-numbered track, then a low-numbered one, then read
 |   immediately) would otherwise look like: not playing, head at or past the
 |   new track's end, purely because the head never moved down to it yet.
 |
 |   loop == 0 gates the forget rule for a second, independent reason: a
 |   looping track has no completion state to detect. An infinite-repeat play
 |   re-seeks to its own start once it reaches its end, and during that
 |   re-seek the block is briefly not in CDC_ST_PLAY with the head sitting at
 |   or just past end -- indistinguishable, on was_playing and fad alone, from
 |   a one-shot that just finished. Excluding loop != 0 from the forget rule
 |   is what tells them apart: what looks like "finished" for a looping track
 |   classifies as CDDA_RESTART instead, which is audible, not silent forever.
 |
 |   start != 0 and end != 0 are both required explicitly, everywhere either
 |   bound is compared. A 0 from cdtoc_track_start/cdtoc_track_end means
 |   "unknown", never a real frame address (see cdtoc.h) -- checking both here
 |   rather than leaning on a cross-function invariant inside cdtoc_track_end
 |   keeps this correct even if a future cdtoc change loosens that invariant.
 |
 |   CDDA_RESTART is the fallback for every case not explicitly forgiven or
 |   resumed, including an unreadable TOC: restarting is a far better failure
 |   than silence.
 | Author: suinevere
 | Params: was_playing -- nonzero if CDC_GetCurStat reported CDC_ST_PLAY at
 |   suspend; loop -- nonzero if the track was commanded to repeat; observed
 |   -- nonzero if this track has been seen actually playing since it was
 |   commanded; fad -- head position at suspend; start, end -- the cue
 |   track's bounds from cdtoc_track_start/cdtoc_track_end, 0 if unknown
 | Returns: the action cdda_restore should take
 ----------------------*/
cdda_action cdda_classify(int was_playing, int loop, int observed,
                          uint32_t fad, uint32_t start, uint32_t end);

#ifdef __cplusplus
}
#endif

#endif /* CDDA_CLASSIFY_H */
