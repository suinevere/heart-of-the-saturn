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
 |   Two things this file does that the host does not:
 |
 |   It stops a channel before playing on it. SRL's PlayOnChannel opens with
 |   `if (!slPCMStat(...))` and refuses a busy channel; the host's
 |   Mix_PlayChannelTimed interrupts it. A script firing two sounds on one
 |   channel expects the second, so the refusal is wrong for us and StopSound
 |   restores the host's behaviour. It is a no-op on a free channel.
 |
 |   It stops all four channels before freeing the cache. slPCMOn streams from
 |   the buffer for the duration of playback, and the LWRAM allocator writes
 |   its own bookkeeping into freed blocks, so freeing underneath a live stream
 |   would be audible. main.c calls sound_flush_cache at the end of load_room,
 |   after the read that overwrites the map -- which is safe only because the
 |   cache holds converted copies rather than pointers into the map. A design
 |   that played in place would have had a live defect at that call site.
 |
 |   Where the memory comes from: saturn_lwram_alloc, never malloc.
 |   disc_srl.cxx measured the HWRAM heap at 62,528 bytes and
 |   saturn_compat.cxx's malloc deliberately refuses to fall back to LWRAM.
 |   This spends the LWRAM pool's 114,688-byte remainder that comment set aside
 |   -- 81,916 of it worst case, after disc_srl.cxx's bounce buffer -- and does
 |   so deliberately: a bounded, per-room, wholly-freed cache is a better claim
 |   on that remainder than an unbounded heap fallback would have been.
 |
 |   Allocation failure is not licensed to fail silently forever: Task 2's
 |   sweep of the eight rooms could only drive an SFX opcode in three of them,
 |   and even those figures are lower bounds because their runs stalled on
 |   input gates rather than completing. With no measured ceiling anywhere,
 |   the first allocation failure paints a one-line warning (see g_warned
 |   below) so the first room to actually exhaust the pool leaves something to
 |   diagnose from rather than a silent missing sound effect.
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

/*----------------------
 | SFX_CHANNELS
 | Description: PCM channels the SCSP offers and the engine uses. Both are
 |   four; SRL fixes its side at four in srl_sound.hpp:395 and sound.h's
 |   play_sample documents the engine's channel argument as 0..3.
 | Author: suinevere
 ----------------------*/
#define SFX_CHANNELS 4

/*----------------------
 | SFX_CACHE_SLOTS
 | Description: Cache entries, one per sample index. The script's operand comes
 |   from next_pc(), a single byte, and play_sample subtracts one from it, so
 |   254 would do; 256 costs 2 KB of .bss either way once alignment is counted
 |   and removes the need to reason about the bound twice.
 | Author: suinevere
 ----------------------*/
#define SFX_CACHE_SLOTS 256

namespace
{
	/*----------------------
	 | MemPcm
	 | Description: An IPcmFile over a buffer this file already owns. SRL's two
	 |   concrete subclasses, RawPcm and WaveSound, both read from a Cd::File;
	 |   these samples are already in RAM. IPcmFile's members are protected and
	 |   PlayOnChannel is public and inherited, so this is the intended
	 |   extension point rather than a way around one.
	 |
	 |   Non-owning, and constructed on the stack per play. That is safe even
	 |   though playback is asynchronous: PlayOnChannel copies mode, pitch,
	 |   level and pan into Pcm's own channel array and passes the pointer and
	 |   size to slPCMOn by value, so nothing the driver goes on to use lives
	 |   here. The buffer must outlive playback; this handle need not. It is
	 |   also why IPcmFile's empty non-virtual destructor is correct here and
	 |   would be a bug in an owning subclass.
	 | Author: suinevere
	 ----------------------*/
	class MemPcm : public SRL::Sound::Pcm::IPcmFile
	{
	public:
		/*----------------------
		 | MemPcm::MemPcm
		 | Description: Describes a cached sample to SRL: mono, 8-bit signed,
		 |   8 kHz. The rate is the Sega CD source rate and is reached by
		 |   hardware pitch, not by resampling -- PlayOnChannel derives octave
		 |   and FNS from this field.
		 | Author: suinevere
		 | Globals: N/A
		 | Params: buffer -- decoded and padded sample bytes, owned by the
		 |   cache; bytes -- their count, from sfxconv_padded_size
		 | Returns: N/A
		 ----------------------*/
		MemPcm(signed char *buffer, unsigned long bytes)
		{
			this->data = (int8_t *)buffer;
			this->dataSize = bytes;
			this->mode = _Mono;
			this->depth = _PCM8Bit;
			this->sampleRate = 8000;
		}
	};
}

/*----------------------
 | g_sampleData / g_sampleSize
 | Description: The converted-sample cache, parallel arrays indexed by
 |   zero-based sample index. g_sampleData holds LWRAM blocks and is NULL where
 |   a sample has not been played since the last flush; g_sampleSize holds the
 |   padded byte count handed to slPCMOn, which is not the sample's own length
 |   whenever that was below SFXCONV_MIN_PLAYABLE.
 |
 |   2,048 bytes of .bss, which is HWRAM. Recorded here because disc_srl.cxx
 |   tracks this project's .bss to the byte and the next person measuring it
 |   should find these accounted for rather than have to chase them.
 | Author: suinevere
 ----------------------*/
static signed char *g_sampleData[SFX_CACHE_SLOTS];
static unsigned long g_sampleSize[SFX_CACHE_SLOTS];

/*----------------------
 | g_warned
 | Description: One-shot latch on the LWRAM-exhaustion warning in play_sample.
 |   Reset in sound_flush_cache so a later room can warn again -- flushing
 |   frees the whole pool back, so a fresh exhaustion there is new information.
 | Author: suinevere
 ----------------------*/
static int g_warned;

extern "C" {

/*----------------------
 | play_sample
 | Description: Plays a sound effect, converting and caching it on first use.
 |   Index 0 stops every channel, which is what src/sound.c's
 |   stop_all_channels does and the only way a script silences anything.
 |   Otherwise the index is one-based and is decremented into the cache.
 |
 |   Failure is silent at every step except one: a refused location and an
 |   out-of-range channel say nothing, matching src/sound.c's one silent
 |   failure path, but the first LWRAM allocation failure since the last flush
 |   paints a one-line warning -- see g_warned's banner for why.
 | Author: suinevere
 | Globals: g_sampleData, g_sampleSize, g_warned
 | Params: index -- sample identifier, 0 to stop all; volume -- 0..0xff, halved
 |   onto SRL's 0..127; channel -- 0..3
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
			SRL::Sound::Pcm::StopSound(i);
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
				printf("SFX: lwram full at %d", index);
			}

			return;
		}

		sfxconv_decode_into(offset, length, buffer, padded);
		g_sampleData[index] = buffer;
		g_sampleSize[index] = (unsigned long)padded;
	}

	SRL::Sound::Pcm::StopSound(channel);
	MemPcm(g_sampleData[index], g_sampleSize[index]).PlayOnChannel(
		(uint8_t)channel, (uint8_t)(volume >> 1));
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
 | Globals: g_sampleData, g_sampleSize, g_warned
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sound_flush_cache()
{
	int i;

	for (i = 0; i < SFX_CHANNELS; i++)
	{
		SRL::Sound::Pcm::StopSound(i);
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
