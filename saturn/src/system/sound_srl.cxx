/*----------------------
 | sound_srl.cxx
 | Description: The two sound.h entry points the engine calls unconditionally,
 |   stubbed. src/sound.c is built on SDL_mixer's Mix_Chunk and is filtered out
 |   of the SH-2 build by name in saturn/makefile, which leaves play_sample
 |   (decode.c:1811, on every SFX opcode) and sound_flush_cache (main.c's
 |   load_room, on every room load) undefined at link -- and there is no seam
 |   to route them through yet, because SFX are a later sub-project alongside
 |   CD-DA. Silence is the correct behaviour in the meantime: the engine never
 |   reads back from either call, so nothing downstream changes.
 |
 |   sound_init and sound_done are not stubbed here, because nothing outside
 |   sound.c and the host platform backend calls them.
 |
 |   Plain SRL-free C++, so it costs nothing but the two empty bodies.
 | Author: suinevere
 | Dependencies: sound.h
 ----------------------*/

// sound.h carries no extern "C" guard of its own. Included plain from a .cxx
// its declarations pick up C++ linkage, which then conflicts with the
// extern "C" definitions below and with the C objects that call them.
extern "C" {
#include "sound.h"
}

extern "C" {

/*----------------------
 | play_sample
 | Description: No-op. decode.c calls this on every sound opcode in the
 |   script; until the SCSP backend exists there is nowhere to send the sample
 |   and no cache to populate. Takes the arguments and discards them rather
 |   than making decode.c branch.
 | Author: suinevere
 | Globals: N/A
 | Params: index -- sample identifier; volume -- 0..0xff; channel -- 0..3
 | Returns: N/A
 ----------------------*/
void play_sample(int index, int volume, int channel)
{
	(void)index;
	(void)volume;
	(void)channel;
}

/*----------------------
 | sound_flush_cache
 | Description: No-op. It exists to release converted samples when a room
 |   unloads; play_sample above converts and caches nothing, so there is
 |   nothing to release.
 | Author: suinevere
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sound_flush_cache()
{
}

}
