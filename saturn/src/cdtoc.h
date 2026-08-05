/*----------------------
 | cdtoc.h
 | Description: Pure decoding of the Saturn BIOS CD table of contents: which
 |   tracks are audio, where each one starts and ends, and the highest audio
 |   track on the disc.
 |
 |   This exists because SRL::Cd::TableOfContents cannot read the TOC.
 |   srl_cd.hpp:794 declares TrackLocation : public ITrack, where ITrack holds
 |   a 4-bit bitfield and the derived class adds a 24-bit one -- a bitfield in
 |   a base subobject cannot share a storage unit with one in the derived
 |   class, so sizeof(TrackLocation) is 8, not 4. GetTable() fills that ~812
 |   byte struct with CDC_TgetToc, which writes 102 longwords (408 bytes), so
 |   Tracks[t] reads longword 2t -- the wrong track -- and everything past
 |   t = 50 is uninitialised stack. SRL::Sound::Cdda::Resume() inherits the
 |   fault through the same table. Found the expensive way in the sibling port
 |   zaturn (saturn/src/sound/music_cdda.cxx) and re-verified here.
 |
 |   Deliberately free of SRL, stdio and every engine header, for the same
 |   reason discfmt.h is: it is compiled into the engine and by
 |   saturn/tests/run_tests.sh with the host gcc, and a wrong frame address
 |   fails plausibly -- the drive plays the wrong part of the disc rather than
 |   erroring -- so it is checked in milliseconds instead of by listening.
 |
 |   The fetch itself (CDC_TgetToc) stays in saturn/src/system/disc_srl.cxx.
 |
 |   Design: docs/superpowers/specs/2026-08-04-hota-saturn-cdda-design.md
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef CDTOC_H
#define CDTOC_H

#include <stdint.h>

/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link, the way the six seam headers did before 7f66fe3. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CDTOC_WORDS / CDTOC_FIRST_WORD / CDTOC_LAST_WORD / CDTOC_LEADOUT_WORD
 | Description: Size and named indices of the BIOS TOC longword array.
 |   CDC_TgetToc writes exactly CDTOC_WORDS longwords and no more, so a buffer
 |   handed to these functions must be at least that large -- reading past it
 |   is what SRL's own table does wrong.
 | Author: suinevere
 ----------------------*/
#define CDTOC_WORDS         102
#define CDTOC_FIRST_WORD    99
#define CDTOC_LAST_WORD     100
#define CDTOC_LEADOUT_WORD  101

/*----------------------
 | CDTOC_MAX_TRACK
 | Description: Red Book's ceiling of 99 tracks per disc, which is also the
 |   number of per-track entries the BIOS TOC carries.
 | Author: suinevere
 ----------------------*/
#define CDTOC_MAX_TRACK     99

/*----------------------
 | cdtoc_is_audio
 | Description: Whether a CD track exists and carries audio rather than data.
 |   Control nibble 0x0f marks the entry absent; bit 2 set marks a data track.
 |   Track 1 on this disc is data, so this is false for it.
 | Author: suinevere
 | Params: toc -- CDTOC_WORDS longwords from CDC_TgetToc; track -- 1..99
 | Returns: 1 if the track exists and is audio, else 0
 ----------------------*/
int cdtoc_is_audio(const uint32_t *toc, int track);

/*----------------------
 | cdtoc_track_start
 | Description: Frame address of a track's first frame. Callers treat 0 as
 |   "unknown" rather than as a valid address -- a real track never starts at
 |   frame 0, since the lead-in occupies everything below 150.
 | Author: suinevere
 | Params: toc -- CDTOC_WORDS longwords from CDC_TgetToc; track -- 1..99
 | Returns: the frame address, or 0 if the track is absent or out of range
 ----------------------*/
uint32_t cdtoc_track_start(const uint32_t *toc, int track);

/*----------------------
 | cdtoc_track_end
 | Description: First frame past the end of a track -- the next track's start,
 |   or the lead-out when nothing follows. A CD track carries no length of its
 |   own, so this subtraction is the only way to bound a playback range, which
 |   is what a mid-track resume needs.
 | Author: suinevere
 | Params: toc -- CDTOC_WORDS longwords from CDC_TgetToc; track -- 1..99
 | Returns: the frame address, or 0 if the track is absent, out of range, or
 |   the TOC's last-track record is unreadable
 ----------------------*/
uint32_t cdtoc_track_end(const uint32_t *toc, int track);

/*----------------------
 | cdtoc_max_audio_track
 | Description: Highest audio track number on the disc, walking only the range
 |   the TOC says exists so absent slots are never consulted. Returns 0 for a
 |   data-only disc, which is what a HOTA_AUDIO=none build produces -- that 0
 |   is what makes asking for music on such a disc a no-op instead of an
 |   undefined CD command.
 | Author: suinevere
 | Params: toc -- CDTOC_WORDS longwords from CDC_TgetToc
 | Returns: the track number, or 0 if the disc has no audio or the TOC is
 |   unreadable
 ----------------------*/
int cdtoc_max_audio_track(const uint32_t *toc);

#ifdef __cplusplus
}
#endif

#endif /* CDTOC_H */
