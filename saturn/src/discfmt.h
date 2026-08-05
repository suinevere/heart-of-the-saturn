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

/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link. */
#ifdef __cplusplus
extern "C" {
#endif

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

/*----------------------
 | DISCFMT_MAX_TRACKS
 | Description: Red Book's hard ceiling of 99 tracks per disc -- the array in
 |   DiscCue is sized to it so a corrupt or hostile cue sheet can only ever
 |   overrun the loop bound, never the buffer, with no malloc to fall back on.
 | Author: suinevere
 ----------------------*/
#define DISCFMT_MAX_TRACKS 99

/*----------------------
 | DiscCueTrack
 | Description: One TRACK entry from the cue sheet: its number, whether it is
 |   audio (vs. the MODE1/2352 data track), and the exact filename from its
 |   FILE line. filename is fixed-size because discfmt.c never allocates --
 |   256 comfortably covers this disc's longest name plus headroom.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int number;
    int is_audio;
    char filename[256];
} DiscCueTrack;

/*----------------------
 | DiscCue
 | Description: The full parsed cue sheet: how many tracks discfmt_cue_parse
 |   filled in and their entries, in file order. A fixed-size array sized to
 |   DISCFMT_MAX_TRACKS rather than a pointer-plus-count, again because this
 |   file never allocates and the caller owns all storage.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int count;
    DiscCueTrack tracks[DISCFMT_MAX_TRACKS];
} DiscCue;

/*----------------------
 | discfmt_cue_parse
 | Description: Parses a multi-file Sega CD cue sheet (one FILE line per
 |   TRACK, which is how this disc's 42 tracks are laid out) into a DiscCue.
 |   Returns 1 on success, 0 on failure, matching every other function in
 |   this file.
 |
 |   single_file is a separate out-parameter rather than a third return
 |   value, because the convention here is strictly 1/0 with no negative
 |   codes -- mixing in a third code would make every existing caller's
 |   "if (!rc)" check ambiguous between "malformed" and "unsupported layout"
 |   without forcing them to look. *single_file is set to 1 only when the
 |   failure is specifically a second TRACK with no intervening FILE line --
 |   a single-file image, which this disc is not and which this code does
 |   not support -- and left 0 for every other kind of malformed input,
 |   including when single_file itself is NULL. Silently accepting a
 |   single-file cue would produce 42 tracks that all point at the same file
 |   at offset zero, which reads as data corruption everywhere at once
 |   rather than a load failure at the one place that caused it.
 | Author: suinevere
 | Params: single_file may be NULL if the caller does not need to
 |   distinguish the two failure kinds.
 ----------------------*/
int discfmt_cue_parse(const char *text, size_t len, DiscCue *out, int *single_file);

/*----------------------
 | discfmt_iso_root
 | Description: Reads the root directory's LBA and length out of an
 |   already-loaded PVD sector's 2048-byte user area. The record lives at a
 |   fixed offset (156) in every ISO9660 PVD, so this is a straight field
 |   read, not a search -- discfmt_iso_find is for records found underneath
 |   the root once its LBA and length are known.
 |
 |   Validates that pvd_user is actually a Primary Volume Descriptor before
 |   trusting offset 156: byte 0 must be the PVD type code (1) and bytes
 |   1..5 must read "CD001". Returns 0 if either check fails. Without this,
 |   a wrong sector or a garbage buffer that happens to satisfy the record's
 |   own length check would still return success with a meaningless LBA and
 |   length -- and Tasks 4 and 7 both trust this return value to locate
 |   every file on the disc, so a silent bogus success here surfaces later
 |   as "the whole disc is corrupt" rather than "we read the wrong sector".
 | Author: suinevere
 ----------------------*/
int discfmt_iso_root(const uint8_t *pvd_user, uint32_t *lba, uint32_t *len);

/*----------------------
 | discfmt_iso_find
 | Description: Walks an ISO9660 directory's raw bytes (already loaded by
 |   the caller, dir_len of them) looking for name, using
 |   discfmt_iso_name_eq for the comparison so the ';1' suffix and case
 |   rules stay in exactly one place.
 |
 |   A record length of 0 means "the rest of this 2048-byte block is unused
 |   padding," not "end of directory" -- ISO9660 directory records never
 |   straddle a block boundary, so a short tail is zero-padded and the next
 |   record starts at the next block. This disc's root directory is two
 |   blocks (4096 bytes), so a walk that treats a zero byte as EOF only ever
 |   sees the first block and silently loses every file in the second. The
 |   walk also never reads a record, or its declared name, past dir_len or
 |   past the record's own declared length, so a truncated or corrupt
 |   directory buffer fails closed instead of reading adjacent memory.
 | Author: suinevere
 ----------------------*/
int discfmt_iso_find(const uint8_t *dir, uint32_t dir_len, const char *name, uint32_t *lba, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif /* DISCFMT_H */
