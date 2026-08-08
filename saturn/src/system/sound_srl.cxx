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
 |   Three things this file does that the host does not:
 |
 |   It owns its four PCM structs (g_pcm below) rather than going through
 |   SRL::Sound::Pcm::IPcmFile::PlayOnChannel. PlayOnChannel rewrites only
 |   mode, pitch, level and pan in Pcm::Channels[channel] and never
 |   PCM.channel, the SCSP slot number SRL's static initialiser sets once;
 |   nothing re-initialises that field after slPCMOff has touched it, and
 |   PlayOnChannel also discards slPCMOn's int8_t status, making failure
 |   unobservable. Filling a PCM struct in full on every play and calling
 |   slPCMOn directly, keeping its return in g_lastRc, fixes both.
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
 | SFX diagnostic counters
 | Description: THROWAWAY instrumentation for the gameplay-silence bug. Not a
 |   fix -- counts calls, failure paths, and slPCMOn's refused-vs-played
 |   outcome so a hardware run can tell H1/H2/H3 and "never called" apart. A
 |   later commit reverts this whole block.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static int g_nCalls;
static int g_nLocateFail;
static int g_nAllocFail;
static int g_nPlayRefused;
static int g_nPlayed;
static int g_lastIndex = -1;
static int g_lastChannel = -1;
static int g_lastLen = -1;
static int g_nPaints;
static int8_t g_lastRc = 0;

/*----------------------
 | diag_paint
 | Description: THROWAWAY instrumentation. Paints the counters, the last
 |   call's index/channel/length plus current LWRAM headroom, and each
 |   channel's free/busy state onto the debug text layer -- the third line
 |   makes H3's "refused forever" hypothesis directly observable instead of
 |   inferred from g_nPlayRefused alone, and g_nPaints tells a frozen display
 |   apart from one that was never repainted since the cinematic.
 |
 |   snprintfEx (srl_string.hpp) supports only %c %s %0Nd %d %x %u %f -- no
 |   width or flag syntax -- so every specifier below is bare %d, padded with
 |   trailing spaces instead, so a shrinking number can't leave stale digits.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_nCalls, g_nLocateFail, g_nAllocFail, g_nPlayRefused, g_nPlayed,
 |   g_lastIndex, g_lastChannel, g_lastLen, g_nPaints, g_lastRc, g_pcm
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void diag_paint()
{
	g_nPaints++;

	SRL::Debug::Print(1, 24, "SFX c%d L%d A%d R%d P%d rc%d sub%d   ",
	                  g_nCalls, g_nLocateFail, g_nAllocFail,
	                  g_nPlayRefused, g_nPlayed, (int)g_lastRc,
	                  SFX_TONE_SUBSTITUTE);
	SRL::Debug::Print(1, 25, "SFX i%d ch%d ln%d lw%d        ",
	                  g_lastIndex, g_lastChannel, g_lastLen,
	                  (int)SRL::Memory::LowWorkRam::GetFreeSpace());
	SRL::Debug::Print(1, 26, "SFX free %d%d%d%d p%d        ",
	                  slPCMStat(&g_pcm[0]) ? 0 : 1,
	                  slPCMStat(&g_pcm[1]) ? 0 : 1,
	                  slPCMStat(&g_pcm[2]) ? 0 : 1,
	                  slPCMStat(&g_pcm[3]) ? 0 : 1,
	                  g_nPaints);
}

/*----------------------
 | SFX_SMOKE_BYTES
 | Description: THROWAWAY. Length of each smoke-test tone, one second at
 |   8 kHz, long enough that a listener cannot miss it and short enough that
 |   both tones fit the HWRAM heap's 62,528 bytes with room to spare.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#define SFX_SMOKE_BYTES 8000

/*----------------------
 | SFX_TONE_SUBSTITUTE
 | Description: THROWAWAY bisection switch. With this at 1, play_sample runs
 |   every one of its normal steps -- locate, allocate, decode, cache, stop
 |   guard, struct fill, slPCMOn -- and then, immediately before the call,
 |   overwrites the cached bytes with the square wave the boot smoke test
 |   proved audible and forces full volume. Only the sample content and the
 |   level change; the call site, the timing, the channel and the buffer are
 |   the engine's own.
 |
 |   Beeps during gameplay mean the call path is sound and the defect is in
 |   the bytes -- sfxconv's location or decode, or what the emulated 68000 map
 |   actually holds at that moment. Continued silence means the bytes are
 |   irrelevant and the defect is in calling slPCMOn from inside the game
 |   loop. Set to 0 to restore real samples.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#define SFX_TONE_SUBSTITUTE 1

/*----------------------
 | smoke_fill
 | Description: THROWAWAY. Writes a full-scale 8-bit signed square wave, so
 |   the tone does not depend on sfxconv, the disc, or the script VM.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- destination; bytes -- length; period -- samples per half cycle
 | Returns: N/A
 ----------------------*/
static void smoke_fill(signed char *buf, int bytes, int period)
{
	int i;

	for (i = 0; i < bytes; i++)
	{
		buf[i] = ((i / period) & 1) ? (signed char)-100 : (signed char)100;
	}
}

/*----------------------
 | smoke_hold
 | Description: THROWAWAY. Presents frames while a tone plays. slPCMOn
 |   streams, so the caller must keep yielding or nothing is fed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: frames -- vblanks to wait
 | Returns: N/A
 ----------------------*/
static void smoke_hold(int frames)
{
	int i;

	for (i = 0; i < frames; i++)
	{
		SRL::Core::Synchronize();
	}
}

/*----------------------
 | smoke_play
 | Description: THROWAWAY. Fills a PCM struct in full and starts one tone,
 |   returning slPCMOn's status rather than discarding it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_pcm
 | Params: channel -- 0..3; data -- sample bytes; bytes -- length
 | Returns: slPCMOn's int8_t status
 ----------------------*/
static int8_t smoke_play(int channel, signed char *data, int bytes)
{
	if (slPCMStat(&g_pcm[channel]))
	{
		slPCMOff(&g_pcm[channel]);
	}

	g_pcm[channel].mode      = _Mono | _PCM8Bit;
	g_pcm[channel].channel   = (uint8_t)(channel * 2);
	g_pcm[channel].level     = 127;
	g_pcm[channel].pan       = 0;
	g_pcm[channel].pitch     = SFX_PITCH_8KHZ;
	g_pcm[channel].eflevelR  = 0;
	g_pcm[channel].efselectR = 0;
	g_pcm[channel].eflevelL  = 0;
	g_pcm[channel].efselectL = 0;

	return slPCMOn(&g_pcm[channel], data, (unsigned long)bytes);
}

extern "C" {

/*----------------------
 | sfx_smoke_test
 | Description: THROWAWAY instrumentation, called once from main() before the
 |   intro. Answers the question the plan's Task 1 was supposed to settle and
 |   never did: can slPCMOn stream from Low Work RAM? The original spike hung
 |   its tone on the first play_sample call, which only the script VM reaches,
 |   so it could never have fired during the intro it was listened for.
 |
 |   Two tones, deliberately different pitches so they are distinguishable by
 |   ear: A is 250 Hz from LWRAM, B is 500 Hz from the HWRAM heap. Three
 |   outcomes, three different causes -- both audible means PCM works and the
 |   defect is in the engine path; only B means LWRAM is an illegal DMA source
 |   for the B-bus destination and the cache must move to HWRAM; neither means
 |   PCM playback is broken below this file, at the driver or the pitch.
 |
 |   Runs before disc_open and initialize(), so it competes with nothing for
 |   either pool and depends on nothing but the sound driver Core::Initialize
 |   has already loaded.
 | Author: suinevere
 | Dependencies: srl.hpp, saturn_compat.h
 | Globals: g_pcm
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sfx_smoke_test(void)
{
	signed char *lw;
	signed char *hw;
	int8_t rcLow = 99;
	int8_t rcHigh = 99;

	SRL::Debug::Print(1, 20, "SFX SMOKE TEST            ");

	lw = (signed char *)saturn_lwram_alloc((unsigned long)SFX_SMOKE_BYTES);
	if (lw != 0)
	{
		smoke_fill(lw, SFX_SMOKE_BYTES, 32);
		rcLow = smoke_play(0, lw, SFX_SMOKE_BYTES);
	}

	SRL::Debug::Print(1, 21, "A LWRAM 250Hz ok%d rc%d    ",
	                  lw != 0 ? 1 : 0, (int)rcLow);
	smoke_hold(120);

	if (slPCMStat(&g_pcm[0]))
	{
		slPCMOff(&g_pcm[0]);
	}

	hw = (signed char *)malloc((size_t)SFX_SMOKE_BYTES);
	if (hw != 0)
	{
		smoke_fill(hw, SFX_SMOKE_BYTES, 16);
		rcHigh = smoke_play(1, hw, SFX_SMOKE_BYTES);
	}

	SRL::Debug::Print(1, 22, "B HWRAM 500Hz ok%d rc%d    ",
	                  hw != 0 ? 1 : 0, (int)rcHigh);
	smoke_hold(120);

	if (slPCMStat(&g_pcm[1]))
	{
		slPCMOff(&g_pcm[1]);
	}

	if (lw != 0)
	{
		saturn_lwram_free(lw);
	}

	if (hw != 0)
	{
		free(hw);
	}

	SRL::Debug::Print(1, 23, "SMOKE DONE A%d B%d          ",
	                  (int)rcLow, (int)rcHigh);
	smoke_hold(180);
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
 | Globals: g_sampleData, g_sampleSize, g_warned, g_pcm, g_lastRc
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

	g_nCalls++;
	g_lastChannel = channel;

	if (index == 0)
	{
		for (i = 0; i < SFX_CHANNELS; i++)
		{
			if (slPCMStat(&g_pcm[i]))
			{
				slPCMOff(&g_pcm[i]);
			}
		}

		diag_paint();
		return;
	}

	if (channel < 0 || channel >= SFX_CHANNELS)
	{
		diag_paint();
		return;
	}

	index = index - 1;
	g_lastIndex = index;
	if (index < 0 || index >= SFX_CACHE_SLOTS)
	{
		diag_paint();
		return;
	}

	if (g_sampleData[index] == 0)
	{
		if (!sfxconv_locate(index, &offset, &length))
		{
			g_nLocateFail++;
			diag_paint();
			return;
		}

		padded = sfxconv_padded_size(length);
		g_lastLen = padded;
		buffer = (signed char *)saturn_lwram_alloc((unsigned long)padded);
		if (buffer == 0)
		{
			g_nAllocFail++;

			if (!g_warned)
			{
				g_warned = 1;
				printf("SFX: lwram full at %d", index + 1);
			}

			diag_paint();
			return;
		}

		sfxconv_decode_into(offset, length, buffer, padded);
		g_sampleData[index] = buffer;
		g_sampleSize[index] = (unsigned long)padded;
	}

	g_lastLen = (int)g_sampleSize[index];

#if SFX_TONE_SUBSTITUTE
	smoke_fill(g_sampleData[index], (int)g_sampleSize[index], 32);
	volume = 254;
#endif

	if (slPCMStat(&g_pcm[channel]))
	{
		slPCMOff(&g_pcm[channel]);
	}

	g_pcm[channel].mode      = _Mono | _PCM8Bit;
	g_pcm[channel].channel   = (uint8_t)(channel * 2);
	g_pcm[channel].level     = (uint8_t)(volume >> 1);
	g_pcm[channel].pan       = 0;
	g_pcm[channel].pitch     = SFX_PITCH_8KHZ;
	g_pcm[channel].eflevelR  = 0;
	g_pcm[channel].efselectR = 0;
	g_pcm[channel].eflevelL  = 0;
	g_pcm[channel].efselectL = 0;

	g_lastRc = slPCMOn(&g_pcm[channel], g_sampleData[index], g_sampleSize[index]);

	if (g_lastRc >= 0)
	{
		g_nPlayed++;
	}
	else
	{
		g_nPlayRefused++;
	}

	diag_paint();
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
