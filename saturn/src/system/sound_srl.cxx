/*----------------------
 | sound_srl.cxx
 | Description: Saturn implementation of sound.h's two engine-facing entry
 |   points, over SRL::Sound::Pcm. Sibling of src/sound.c, which is built on
 |   SDL_mixer's Mix_Chunk and stays filtered out of the SH-2 build by name in
 |   saturn/makefile.
 |
 |   The port of sound.c is unusually thin, and the reason is worth stating:
 |   the SCSP plays 8-bit signed mono natively and reaches 8 kHz by hardware
 |   pitch, so the host's entire SDL_BuildAudioCVT path -- 8 kHz mono 8-bit up
 |   to 44.1 kHz stereo 16-bit, an eleven-fold expansion -- has no counterpart
 |   here. What is left of the host's work is one byte-for-byte decode, and
 |   that lives in sfxconv.c where a host test can pin it. Sound RAM also
 |   offers exactly four PCM channels against the engine's four, so `channel`
 |   maps straight through with no allocation policy.
 |
 |   Four things this file does that the host does not:
 |
 |   It owns its four PCM structs (g_pcm below) rather than going through
 |   SRL::Sound::Pcm::IPcmFile::PlayOnChannel. PlayOnChannel rewrites only
 |   mode, pitch, level and pan in Pcm::Channels[channel] and never
 |   PCM.channel, the SCSP slot number SRL's static initialiser sets once,
 |   and nothing re-initialises that field after slPCMOff has touched it.
 |   Filling a PCM struct in full on every play and calling slPCMOn directly
 |   removes the dependency on SRL's array entirely.
 |
 |   It stops a channel before playing on it, guarded by slPCMStat so
 |   slPCMOff is never called on a channel that is not playing. A script
 |   firing two sounds on one channel expects the second, which is why this
 |   file interrupts rather than refuses -- the host's Mix_PlayChannelTimed
 |   does the same.
 |
 |   It stops all four channels, same guard, before freeing the cache.
 |   slPCMOn streams from the buffer for the duration of playback, and the
 |   LWRAM allocator writes its own bookkeeping into freed blocks, so freeing
 |   underneath a live stream would be audible. main.c calls sound_flush_cache
 |   at the end of load_room, after the read that overwrites the map -- which
 |   is safe only because the cache holds converted copies rather than
 |   pointers into the map. A design that played in place would have had a
 |   live defect at that call site.
 |
 |   It honours volume on every play, where the host ignores it on cached
 |   replays. sound.c bakes volume into the Mix_Chunk when a sample is first
 |   converted and its cached path replays that chunk without consulting the
 |   volume argument again; that is an artifact of how Mix_Chunk carries its
 |   own volume, not a design decision, so this file applies volume fresh on
 |   every call instead of inheriting it -- ignoring a volume the script
 |   explicitly asked for would be an audible defect.
 |
 |   It converts that volume rather than halving it. The host's `volume >> 1`
 |   is linear amplitude on SDL_mixer's scale; slPCMOn's level is attenuation
 |   in dB steps, so copying the host's arithmetic made every effect
 |   inaudible. See sfx_level_for_volume.
 |
 |   Where the memory comes from: saturn_lwram_alloc, never malloc.
 |   disc_srl.cxx measured the HWRAM heap at 62,528 bytes and
 |   saturn_compat.cxx's malloc deliberately refuses to fall back to LWRAM.
 |   This spends the LWRAM pool's 114,688-byte remainder that comment set aside
 |   -- 81,916 of it worst case, after disc_srl.cxx's bounce buffer -- and does
 |   so deliberately: a bounded, per-room, wholly-freed cache is a better claim
 |   on that remainder than an unbounded heap fallback would have been.
 |
 |   Allocation failure is not licensed to fail silently forever: of Task 2's
 |   seven valid room attempts -- the eighth, room 9, is not a room, since
 |   there is no ROOMS9.BIN behind the index -- only two produced any probe
 |   data at all, and even those two figures are lower bounds rather than
 |   maxima. With no measured ceiling anywhere, the first allocation failure
 |   paints a one-line warning (see g_warned below) so the first room to
 |   actually exhaust the pool leaves something to diagnose from rather than a
 |   silent missing sound effect.
 |
 |   sound_init and sound_done are still not defined here. Nothing on Saturn
 |   calls either. sound_init would zero a table the C runtime already zeroes,
 |   and sound_done would free a cache at a shutdown that never happens --
 |   exit() parks in a Synchronize() loop without unwinding.
 |
 |   Design: docs/superpowers/specs/2026-08-07-hota-saturn-sfx-design.md
 | Author: suinevere
 | Dependencies: srl.hpp, sound.h, sfxconv.h, saturn_compat.h
 ----------------------*/

#include <srl.hpp>

#include "sound.h"
#include "sfxconv.h"
#include "saturn_compat.h"
#include "disc.h"

/*----------------------
 | SFX_CHANNELS
 | Description: PCM channels the SCSP offers and the engine uses. Both are
 |   four; SRL fixes its side at four in srl_sound.hpp:395 and sound.h's
 |   play_sample documents the engine's channel argument as 0..3.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#define SFX_CHANNELS 4

/*----------------------
 | SFX_CACHE_SLOTS
 | Description: Cache entries, one per sample index. The script's operand comes
 |   from next_pc(), a single byte, so the maximum operand is 255 and
 |   play_sample's decrement makes the maximum index 254, needing 255 entries;
 |   256 is used because it removes the need to reason about the bound twice.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#define SFX_CACHE_SLOTS 256

/*----------------------
 | SFX_PITCH_8KHZ
 | Description: slPCMOn's pitch word for an 8 kHz sample, precomputed because
 |   SRL's PCM_CALC_* macros dereference Pcm::LogTable, a private static that
 |   is unreachable outside SRL::Sound::Pcm. Derivation, following SGL's own
 |   formula: octave = LogTable[44100 / (8000 + 1)] = LogTable[5] = 3;
 |   shift = 44100 >> 3 = 5512; fns = ((8000 - 5512) << 10) / 5512 = 462;
 |   pitch = ((-3 & 0xF) << 11) | 462 = (0xD << 11) | 0x1CE = 0x69CE.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#define SFX_PITCH_8KHZ 0x69CE

/*----------------------
 | g_sampleData / g_sampleSize
 | Description: The converted-sample cache, parallel arrays indexed by
 |   zero-based sample index. g_sampleData holds LWRAM blocks and is NULL where
 |   a sample has not been played since the last flush; g_sampleSize holds the
 |   padded byte count handed to slPCMOn, which is not the sample's own length
 |   whenever that was below SFXCONV_MIN_PLAYABLE.
 |
 |   2,048 bytes of .bss, which is HWRAM. Recorded here because disc_srl.cxx
 |   tracks this project's .bss to the byte; g_warned below adds the other 4
 |   bytes of this file's .bss contribution.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static signed char *g_sampleData[SFX_CACHE_SLOTS];
static unsigned long g_sampleSize[SFX_CACHE_SLOTS];

/*----------------------
 | g_warned
 | Description: One-shot latch on the LWRAM-exhaustion warning in play_sample.
 |   Reset in sound_flush_cache so a later room can warn again -- flushing
 |   frees the whole cache back, not the LWRAM pool itself, so a fresh
 |   exhaustion there is new information. Adds 4 bytes of .bss beyond
 |   g_sampleData / g_sampleSize's 2,048.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static int g_warned;

/*----------------------
 | g_pcm
 | Description: One PCM struct per engine channel, owned and fully
 |   re-initialised on every play_sample call rather than left for SRL's
 |   Pcm::Channels[] to half-update. PlayOnChannel only ever rewrote mode,
 |   pitch, level and pan there, never PCM.channel (the SCSP slot number,
 |   set once by SRL's static initialiser), so once slPCMOff had touched a
 |   channel nothing put that field back. Filling all nine members on every
 |   play removes that dependency on SRL's own array entirely.
 |
 |   File-scope initialised, not left zero, because .channel is what
 |   slPCMStat and slPCMOff key off of -- the PCM struct carries no separate
 |   "is playing" flag -- and a zero-initialised array reports channel 1, 2
 |   and 3 as slot 0 until their first play writes .channel. The guard in
 |   play_sample and the stop loops below all run before that write, so on
 |   first use they would query and potentially cut off slot 0. Values are
 |   0, 2, 4, 6: SCSP slot numbers, spaced two apart so a stereo pair would
 |   fit each engine channel, matching srl_sound.hpp:395-401's own layout.
 |   play_sample's per-play write of every field afterward is kept, not
 |   removed -- this initialiser only has to be correct once, at startup.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static PCM g_pcm[SFX_CHANNELS] = {
	{ _Mono | _PCM8Bit, 0, 127, 0, SFX_PITCH_8KHZ, 0, 0, 0, 0 },
	{ _Mono | _PCM8Bit, 2, 127, 0, SFX_PITCH_8KHZ, 0, 0, 0, 0 },
	{ _Mono | _PCM8Bit, 4, 127, 0, SFX_PITCH_8KHZ, 0, 0, 0, 0 },
	{ _Mono | _PCM8Bit, 6, 127, 0, SFX_PITCH_8KHZ, 0, 0, 0, 0 }
};

/*----------------------
 | sfx_level_for_volume
 | Description: Maps the engine's 0..255 volume onto slPCMOn's 0..127 level.
 |   Not a halving, which is what src/sound.c does and what this file did
 |   until hardware said otherwise: SDL_mixer's volume is a linear amplitude
 |   multiplier over 0..128, so the host's `volume >> 1` is about -6 dB, but
 |   SGL's level reaches the SCSP's total-level field, which is attenuation in
 |   fixed dB steps. Halving the number there is roughly -48 dB, and the
 |   engine's quieter volume of 50 landed near -72 dB. Both were inaudible on
 |   hardware, in every room, in every build -- the defect that made sound
 |   effects look like they had never worked.
 |
 |   Taking the SCSP's 96 dB over 256 steps and SGL's 0..127 as every second
 |   step gives 0.75 dB per level, so reproducing the host's linear gain
 |   v/255 means level = 127 + 20*log10(v/255)/0.75, which reduces to
 |   63 + 8*log2(v). That is computed here without floating point: e is the
 |   integer part of log2, and the remainder term interpolates the mantissa
 |   linearly, worth at most about half a dB of error.
 |
 |   The 0.75 dB step is inferred from the hardware rather than read off a
 |   datasheet, so the shape is certain -- 63 was inaudible where 127 was not,
 |   which no linear scale explains -- and the exact slope is not.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: volume -- engine volume, 0..255
 | Returns: level for PCM.level, 0..127
 ----------------------*/
static int sfx_level_for_volume(int volume)
{
	int e;
	int level;

	if (volume <= 0)
	{
		return 0;
	}

	if (volume > 255)
	{
		volume = 255;
	}

	e = 0;
	while ((volume >> (e + 1)) != 0)
	{
		e++;
	}

	level = 63 + 8 * e + ((volume - (1 << e)) * 8) / (1 << e);

	if (level > 127)
	{
		level = 127;
	}

	return level;
}

/*----------------------
 | SFX_DISC_SLOT_BYTES / SFX_DISC_CHANNEL
 | Description: The single resident buffer the disc effect tracks are played
 |   from, and the channel they play on. 51,200 bytes holds the largest of
 |   them, SFX26 at 49,753; the others are 11 KB to 30 KB and share the slot.
 |
 |   Cost is real and worth stating: the buffer is taken once and never
 |   returned, so the per-room sample cache below is working against 30,716
 |   bytes rather than 81,916. The worst room measured needs 20,804, so it
 |   fits, but the margin is now thin enough that a heavier room is the first
 |   thing to suspect if the exhaustion warning ever fires.
 |
 |   Channel 3 is the highest the engine uses, so an effect track collides
 |   with a script sample only when all four are busy at once -- and the
 |   engine's own play_sample would have interrupted that channel anyway.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#define SFX_DISC_SLOT_BYTES 51200
#define SFX_DISC_CHANNEL 3

/*----------------------
 | g_discPcm / g_discPcmSize / g_discPcmCue
 | Description: The resident effect-track slot: its LWRAM block, the bytes
 |   currently in it, and which cue those bytes came from. One slot rather
 |   than a cache per cue because the seven of them total 183 KB and LWRAM
 |   cannot hold that, and one is enough in practice -- a death repeats the
 |   same sound, so only the first play of each pays a read.
 | Author: suinevere
 ----------------------*/
static signed char *g_discPcm;
static unsigned long g_discPcmSize;
static int g_discPcmCue = -1;

extern "C" {

/*----------------------
 | sfx_play_disc_track
 | Description: Plays one of the game's short effect tracks from a converted
 |   PCM file instead of from CD-DA. Loads it on first use and keeps it, so a
 |   repeated sound costs nothing after the first.
 |
 |   This exists because the CD-DA path cannot deliver these at all. They sit
 |   a third of the way across the disc, need one to three seconds of seek
 |   from wherever the room music is, and are wanted at the instant of an
 |   event that is over in about that long -- and any file read in between
 |   sends the drive away again. Making the engine wait for the drive was
 |   tried and it desynchronised the cutscenes, because the same wait covers
 |   both. From memory there is no seek, nothing to synchronise, and nothing
 |   a room load can take away.
 |
 |   The files are 8 kHz 8-bit signed mono with their Red Book padding
 |   already trimmed (saturn/tools/mkpcmsfx.py), which is exactly what
 |   sfxconv.c produces for script samples -- so this shares the pitch word
 |   and the PCM struct handling below and needs no decoder of its own.
 | Author: suinevere
 | Dependencies: srl.hpp, disc.h, saturn_compat.h
 | Globals: g_discPcm, g_discPcmSize, g_discPcmCue, g_pcm
 | Params: cue -- cue track number of the effect
 | Returns: 1 if the sound was started, 0 if it could not be
 ----------------------*/
int sfx_play_disc_track(int cue)
{
	char name[16];

	if (cue != g_discPcmCue)
	{
		if (g_discPcm == 0)
		{
			g_discPcm = (signed char *)saturn_lwram_alloc(SFX_DISC_SLOT_BYTES);

			if (g_discPcm == 0)
			{
				return 0;
			}
		}

		snprintf(name, sizeof(name), "SFX%02d.PCM", cue);

		SRL::Cd::File probe(name);

		if (!probe.Exists())
		{
			printf("sfx: %s not on disc\n", name);
			return 0;
		}

		g_discPcmSize = (unsigned long)probe.Size.Bytes;

		if (g_discPcmSize > SFX_DISC_SLOT_BYTES)
		{
			g_discPcmSize = SFX_DISC_SLOT_BYTES;
		}

		if (disc_read_file(name, g_discPcm, SFX_DISC_SLOT_BYTES) < 0)
		{
			g_discPcmCue = -1;
			return 0;
		}

		g_discPcmCue = cue;
	}

	if (slPCMStat(&g_pcm[SFX_DISC_CHANNEL]))
	{
		slPCMOff(&g_pcm[SFX_DISC_CHANNEL]);
	}

	g_pcm[SFX_DISC_CHANNEL].mode      = _Mono | _PCM8Bit;
	g_pcm[SFX_DISC_CHANNEL].channel   = (uint8_t)(SFX_DISC_CHANNEL * 2);
	g_pcm[SFX_DISC_CHANNEL].level     = 127;
	g_pcm[SFX_DISC_CHANNEL].pan       = 0;
	g_pcm[SFX_DISC_CHANNEL].pitch     = SFX_PITCH_8KHZ;
	g_pcm[SFX_DISC_CHANNEL].eflevelR  = 0;
	g_pcm[SFX_DISC_CHANNEL].efselectR = 0;
	g_pcm[SFX_DISC_CHANNEL].eflevelL  = 0;
	g_pcm[SFX_DISC_CHANNEL].efselectL = 0;

	slPCMOn(&g_pcm[SFX_DISC_CHANNEL], g_discPcm, g_discPcmSize);
	return 1;
}

/*----------------------
 | sfx_stop_disc_track
 | Description: Silences the effect-track channel. Called when a death
 |   sequence ends, because the sound outlasts the picture otherwise: the
 |   converted tracks run one to six seconds and a sequence is over well
 |   before the longest of them, so a splat that used to be cut off by the
 |   drive being seeked away now runs on into the next screen instead.
 |
 |   The buffer is deliberately not freed. Keeping it is the whole point of
 |   the resident slot -- the next death is the same sound and must not pay
 |   another read.
 | Author: suinevere
 | Dependencies: srl.hpp
 | Globals: g_pcm
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sfx_stop_disc_track(void)
{
	if (slPCMStat(&g_pcm[SFX_DISC_CHANNEL]))
	{
		slPCMOff(&g_pcm[SFX_DISC_CHANNEL]);
	}
}

/*----------------------
 | play_sample
 | Description: Plays a sound effect, converting and caching it on first use.
 |   Index 0 stops every channel, which is what src/sound.c's
 |   stop_all_channels does and the only way a script silences anything.
 |   Otherwise the index is one-based and is decremented into the cache.
 |
 |   sound.c has exactly one failure path -- allocation failure -- and it is
 |   not silent: it calls fprintf(stderr, ...). That is the one path with a
 |   host counterpart, and it is the one path here that also prints (see
 |   g_warned's banner for why). The other two failure paths -- a refused
 |   location and an out-of-range channel -- have no host counterpart at all,
 |   because the host has no bounds checks that can fail, and stay silent
 |   here.
 | Author: suinevere
 | Globals: g_sampleData, g_sampleSize, g_warned, g_pcm
 | Params: index -- sample identifier, 0 to stop all; volume -- 0..0xff, mapped
 |   onto SRL's 0..127 by sfx_level_for_volume; channel -- 0..3
 | Returns: N/A
 ----------------------*/
void play_sample(int index, int volume, int channel)
{
	int offset;
	int length;
	int padded;
	int i;
	signed char *buffer;

	if (index == 0)
	{
		for (i = 0; i < SFX_CHANNELS; i++)
		{
			if (slPCMStat(&g_pcm[i]))
			{
				slPCMOff(&g_pcm[i]);
			}
		}

		return;
	}

	if (channel < 0 || channel >= SFX_CHANNELS)
	{
		return;
	}

	index = index - 1;
	if (index < 0 || index >= SFX_CACHE_SLOTS)
	{
		return;
	}

	if (g_sampleData[index] == 0)
	{
		if (!sfxconv_locate(index, &offset, &length))
		{
			return;
		}

		padded = sfxconv_padded_size(length);
		buffer = (signed char *)saturn_lwram_alloc((unsigned long)padded);
		if (buffer == 0)
		{
			if (!g_warned)
			{
				g_warned = 1;
				printf("SFX: lwram full at %d", index + 1);
			}

			return;
		}

		sfxconv_decode_into(offset, length, buffer, padded);
		g_sampleData[index] = buffer;
		g_sampleSize[index] = (unsigned long)padded;
	}

	if (slPCMStat(&g_pcm[channel]))
	{
		slPCMOff(&g_pcm[channel]);
	}

	g_pcm[channel].mode      = _Mono | _PCM8Bit;
	g_pcm[channel].channel   = (uint8_t)(channel * 2);
	g_pcm[channel].level     = (uint8_t)sfx_level_for_volume(volume);
	g_pcm[channel].pan       = 0;
	g_pcm[channel].pitch     = SFX_PITCH_8KHZ;
	g_pcm[channel].eflevelR  = 0;
	g_pcm[channel].efselectR = 0;
	g_pcm[channel].eflevelL  = 0;
	g_pcm[channel].efselectL = 0;

	slPCMOn(&g_pcm[channel], g_sampleData[index], g_sampleSize[index]);
}

/*----------------------
 | sound_flush_cache
 | Description: Stops every channel, then releases every cached sample. The
 |   order is the point: slPCMOn streams from these buffers for the duration of
 |   playback and the LWRAM allocator writes its bookkeeping into freed blocks,
 |   so freeing first would hand the sound driver a pointer into a block that
 |   is being rewritten underneath it.
 |
 |   Also resets g_warned, so a room that exhausts the pool after this flush
 |   gets its own warning rather than inheriting a prior room's silence.
 | Author: suinevere
 | Globals: g_sampleData, g_sampleSize, g_warned, g_pcm
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sound_flush_cache()
{
	int i;

	for (i = 0; i < SFX_CHANNELS; i++)
	{
		if (slPCMStat(&g_pcm[i]))
		{
			slPCMOff(&g_pcm[i]);
		}
	}

	for (i = 0; i < SFX_CACHE_SLOTS; i++)
	{
		if (g_sampleData[i] != 0)
		{
			saturn_lwram_free(g_sampleData[i]);
			g_sampleData[i] = 0;
			g_sampleSize[i] = 0;
		}
	}

	g_warned = 0;
}

}
