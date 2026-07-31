/*----------------------
 | discfmt.h
 | Description: Pure Sega CD disc-format logic: MODE1/2352 sector arithmetic,
 |   ISO9660 record matching, cue parsing and the music track mapping.
 |
 |   Deliberately free of <stdio.h>, SDL and every engine header. This file is
 |   compiled three times -- into the engine, into tools/extract_disc, and by
 |   saturn/tests/run_tests.sh with the host gcc -- and keeping it to <stdint.h>
 |   is what makes the arithmetic testable without a disc. That matters because
 |   all of it fails plausibly rather than loudly: a wrong sector offset yields
 |   data that is correct for its first 2048 bytes and garbage after, which
 |   presents as a decoder bug rather than a disc bug.
 |
 |   Everything needing a FILE * lives in src/host/disc_cue.c.
 |
 |   Design: docs/superpowers/specs/2026-07-31-hota-bincue-disc-backend-design.md
 | Author: suinevere
 | Dependencies: stdint.h, stddef.h
 ----------------------*/
#ifndef DISCFMT_H
#define DISCFMT_H

#include <stdint.h>
#include <stddef.h>

/*----------------------
 | discfmt_mode1_user_offset
 | Description: Byte offset of a sector's 2048 bytes of user data within a
 |   MODE1/2352 track file.
 |
 |   A raw sector is 12 bytes of sync, a 4-byte header, 2048 bytes of payload,
 |   then 288 bytes of EDC/ECC. The old cd_iso.c read from a 2048-byte-per-
 |   sector .iso where payload was contiguous; here it is not, so a read that
 |   spans sectors is a per-sector loop and never one fread. Treating it as
 |   contiguous corrupts everything past the first 2048 bytes.
 | Author: suinevere
 ----------------------*/
uint32_t discfmt_mode1_user_offset(uint32_t lba);

/*----------------------
 | discfmt_sector_span
 | Description: Number of whole MODE1 sectors needed to hold size bytes.
 |   Files on this disc are not sector-aligned, so a straight division would
 |   silently truncate the last partial sector of every such file.
 | Author: suinevere
 ----------------------*/
uint32_t discfmt_sector_span(uint32_t size);

/*----------------------
 | discfmt_iso_name_eq
 | Description: Compares an ISO9660 directory-record filename against a bare
 |   want name, case-insensitively, stopping at the ';' version suffix and
 |   honoring iso_len rather than a NUL -- ISO9660 names are not NUL-terminated
 |   inside the record. Both sides must end together, so a shared prefix (e.g.
 |   ROOMS1.BIN against ROOMS11.BIN) does not match.
 | Author: suinevere
 ----------------------*/
int discfmt_iso_name_eq(const char *iso_name, uint8_t iso_len, const char *want);

/*----------------------
 | discfmt_cue_track_for_music
 | Description: Maps the engine's music index onto a cue track number.
 |
 |   cue_track = engine_index + 2. The disc carries 41 audio tracks, TRACK 02
 |   through TRACK 42, and the engine indexes music 0..40.
 |
 |   The +2 is the whole reason this is a function. The deleted music.c built
 |   its filename as "%02d" of track + 1, but that numbered the mp3 RIP, which
 |   numbered the 41 audio tracks 01..41 -- audio track 1 being disc track 2.
 |   Carrying that +1 over as a cue-track formula aims engine index 0 at
 |   TRACK 01, the DATA track. On hardware that is noise or a hang; in an
 |   emulator it is often just silence, so it survives casual testing.
 | Author: suinevere
 | Params: engine_index -- 0..40. Returns 0 (an invalid track) if out of range.
 ----------------------*/
int discfmt_cue_track_for_music(int engine_index);

#endif /* DISCFMT_H */
