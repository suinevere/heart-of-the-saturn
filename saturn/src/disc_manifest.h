/*----------------------
 | disc_manifest.h
 | Description: The 19 blobs on the disc, and the two fixed offsets in the
 |   emulated 68000 map that they are loaded to. Split out of disc_cue.c on
 |   2026-08-04 so that the sizes have exactly one home: the map's size bound
 |   is a function of these numbers, and a bound written as a literal in a test
 |   is a claim nobody checks, not a fact. Header-only and free of stdio and
 |   SDL so that the host unit tests can derive the bound without linking the
 |   disc layer.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef __DISC_MANIFEST_INCLUDED__
#define __DISC_MANIFEST_INCLUDED__

/*----------------------
 | DISC_MANIFEST_LIST
 | Description: Every {name, lba, size} on the disc, as an X-macro because the
 |   two consumers need the same facts in different shapes -- disc_cue.c builds
 |   a struct array to check a live directory read against, test_vm_memory.c
 |   folds the sizes into a maximum. Duplicating the table for the second
 |   consumer would let the two drift, and the whole point of the test is that
 |   the map is big enough for these exact files.
 | Author: suinevere
 ----------------------*/
#define DISC_MANIFEST_LIST(X)        \
	X("END1.BIN",     2593, 432128)  \
	X("END2.BIN",     2804, 432128)  \
	X("END3.BIN",     3015, 432128)  \
	X("END4.BIN",     3226, 432128)  \
	X("GAME2.BIN",    1082, 409600)  \
	X("INTRO1.BIN",    238, 432128)  \
	X("INTRO2.BIN",    449, 432128)  \
	X("INTRO3.BIN",    660, 432128)  \
	X("INTRO4.BIN",    871, 432128)  \
	X("MAKE2MB.BIN",  3650, 436224)  \
	X("MID2.BIN",     3863, 432128)  \
	X("ROOMS1.BIN",   4501, 370688)  \
	X("ROOMS2.BIN",   1282, 370688)  \
	X("ROOMS3.BIN",   1463, 370688)  \
	X("ROOMS4.BIN",   1644, 370688)  \
	X("ROOMS5.BIN",   1825, 370688)  \
	X("ROOMS6.BIN",   4139, 370688)  \
	X("ROOMS7.BIN",   2006, 160826)  \
	X("ROOMS8.BIN",   4320, 370688)

/*----------------------
 | ANIMATION_LOAD_BASE
 | Description: Offset in the emulated 68000 map that play_animation reads
 |   animation files to. Named here rather than left a literal at the load site
 |   so the map-size test bounds the same number the loader uses. The loader
 |   subtracts the caller's fileoffset from it, so this is the highest base any
 |   animation is ever read at, never the lowest.
 | Author: suinevere
 ----------------------*/
#define ANIMATION_LOAD_BASE 0x809a

/*----------------------
 | ROOMS_LOAD_BASE
 | Description: Offset in the emulated 68000 map that load_room_file reads
 |   ROOMSn.BIN to. Named for the same reason as ANIMATION_LOAD_BASE. Higher
 |   than the animation base but the room files are smaller, so it is not what
 |   bounds the map.
 | Author: suinevere
 ----------------------*/
#define ROOMS_LOAD_BASE 0xf900

#endif
