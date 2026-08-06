/*----------------------
 | discsec.h
 | Description: Splits a file's byte count into the whole sectors a disc read
 |   can transfer straight into the caller's destination and the leftover tail
 |   bytes that cannot.
 |
 |   This is four lines of arithmetic and it still earns its own file, for a
 |   reason that is not obvious: SRL already reports Size.Sectors and
 |   Size.LastSectorSize (srl_cd.hpp:197), so the split could be read straight
 |   off the file object. It must not be. disc_read_file_body bounds-checks
 |   Size.Bytes against max_size and nothing else; if it then derived the read
 |   length from a different pair of numbers, a GFS_GetFileSize that reported
 |   Sectors and LastSectorSize inconsistent with Bytes would write past the
 |   bound that was just checked. Deriving the split from Size.Bytes -- the one
 |   number max_size was compared against -- closes that, and cross-checking
 |   the result against SRL's own pair turns any disagreement into a refusal
 |   instead of an overrun. That cross-check is the second reason the
 |   arithmetic is worth isolating: it is the thing a host test can pin, and a
 |   listening test cannot.
 |
 |   Deliberately free of SRL, stdio and every engine header, for the same
 |   reason cdtoc.h and cdda_classify.h are: it is compiled into the engine and
 |   by saturn/tests/run_tests.sh with the host gcc, and a wrong split fails
 |   plausibly -- 966 bytes of mastering filler land in the emulated 68000 map
 |   past the end of a room -- rather than erroring.
 |
 |   Design: docs/superpowers/specs/2026-08-06-hota-saturn-disc-read-speed-design.md
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef DISCSEC_H
#define DISCSEC_H

#include <stdint.h>

/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link, the way the six seam headers did before 7f66fe3. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | DISC_MAX_SECTOR_BYTES
 | Description: The largest sector this port's bounce buffer is built for, and
 |   the size of a Mode 1 data sector. The sector size is a runtime value read
 |   off the mounted disc and it indexes a fixed-size buffer, so a backend must
 |   refuse anything larger rather than assume: a Mode 2 disc reporting 2,336
 |   would otherwise memcpy 2,336 bytes into 2,048. The mounted disc is Mode 1
 |   and always will be, which is exactly why this ceiling has to be written
 |   down rather than assumed.
 | Author: suinevere
 ----------------------*/
#define DISC_MAX_SECTOR_BYTES 2048

/*----------------------
 | discsec_whole_sectors
 | Description: How many complete sectors a file of this many bytes occupies.
 |   These are the sectors a read can transfer straight into the caller's
 |   destination, because every byte of them belongs to the file.
 | Author: suinevere
 | Globals: N/A
 | Params: bytes -- the file's size, from the same field max_size was checked
 |   against; sector_size -- the mounted disc's sector size in bytes
 | Returns: the count, or 0 for a non-positive sector_size or a negative byte
 |   count -- a caller that gets 0 from this and from discsec_tail_bytes reads
 |   nothing and fails its own length check rather than proceeding on a guess
 ----------------------*/
int32_t discsec_whole_sectors(int32_t bytes, int32_t sector_size);

/*----------------------
 | discsec_tail_bytes
 | Description: How many bytes of the file live in its final, partial sector.
 |   These are the bytes that cannot be transferred straight to the caller: the
 |   rest of that sector on the disc is whatever the mastering tool put there,
 |   and writing it out would overshoot the destination bound. 0 means the file
 |   is an exact multiple of the sector size, which 18 of the 19 manifest blobs
 |   are -- ROOMS7.BIN is the exception.
 | Author: suinevere
 | Globals: N/A
 | Params: bytes -- the file's size, from the same field max_size was checked
 |   against; sector_size -- the mounted disc's sector size in bytes
 | Returns: the remainder, 0 .. sector_size - 1, or 0 for a non-positive
 |   sector_size or a negative byte count
 ----------------------*/
int32_t discsec_tail_bytes(int32_t bytes, int32_t sector_size);

#ifdef __cplusplus
}
#endif

#endif /* DISCSEC_H */
