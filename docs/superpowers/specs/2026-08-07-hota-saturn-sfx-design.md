# Heart of The Alien → Sega Saturn — Sound Effects Design Spec

**Date:** 2026-08-07
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)

## Goal

Give the game its sound effects.

`play_sample` and `sound_flush_cache` are no-ops in `saturn/src/system/sound_srl.cxx`.
They are the last stubbed seam in the port: CD-DA music, video, disc reads and input all
have real backends behind them, and the file's own banner names SFX as a later
sub-project. This is that sub-project.

Done means: the SFX opcode (`decode.c:1813`, opcode `0x18`) produces the same sound on
Saturn that it produces on the host — the right sample, on the channel the script asked
for, at the volume the script asked for, and silence on `index == 0`.

Host parity is the standard, not "sounds plausible". Where the host's behaviour is
arguably wrong (see [The `0x80` decode edge](#the-0x80-decode-edge)) this spec
reproduces it and says so, rather than quietly improving it.

## Scope

**The two `sound.h` entry points the engine actually calls, and nothing else.**

- `play_sample(index, volume, channel)` — called from `decode.c:1813` on every SFX
  opcode.
- `sound_flush_cache()` — called from `main.c:126`, at the end of `load_room`.

Explicitly not in scope, and each is argued in [Out of scope](#out-of-scope):
`sound_init`/`sound_done`, music volume or ducking, panning, reverb, and any change to
`src/sound.c` or the host build.

## What is already solved

**SRL's PCM surface is an unusually good fit, and this is the reason the sub-project is
small.**

- `SRL::Sound::Pcm` exposes exactly **four** channels (`srl_sound.hpp:395`). The engine
  has exactly four. No allocation policy is needed — `channel` maps straight through.
- The SCSP plays **8-bit signed mono natively** (`_PCM8Bit | _Mono`). The source format
  after decoding is 8-bit signed mono. **No resampling and no format conversion.** The
  host's whole `SDL_BuildAudioCVT`/`SDL_ConvertAudio` path — 8 kHz mono 8-bit up to
  44.1 kHz stereo 16-bit, an ~11× expansion — has no counterpart here.
- The 8 kHz rate is reached by **hardware pitch**, not by resampling.
  `PCM_CALC_OCT(8000)` = `LogTable[44100/8001]` = `LogTable[5]` = 3; shift = 44100 >> 3
  = 5512; fns = ((8000 − 5512) << 10) / 5512 = 462. `IPcmFile::PlayOnChannel` does this
  arithmetic itself from a `sampleRate` field.
- The sound driver is **already up**. `SRL::Core::Initialize` calls
  `SRL::Sound::Hardware::Initialize()` (`srl_core.hpp:108`), and the makefile sets
  `SRL_USE_SGL_SOUND_DRIVER = 1` (`saturn/makefile:29`). Nothing new has to be
  initialised, and there is no per-frame tick to add — the SGL driver services its own
  stream.
- `slPCMOn(PCM *, void *, uint32_t)` streams **from work RAM**, into a 32 KB window at
  `SoundRAM + 0x78000` (`modules/sgl/SRC/workarea.c:30`). Sample length is not bounded
  by that window; the driver refills it.
- `Pcm::IsChannelFree(ch)` and `Pcm::StopSound(ch)` are public statics
  (`srl_sound.hpp:873`, `:882`).

**The flush ordering is already safe.** `main.c:126` calls `sound_flush_cache()` *after*
`disc_read_file` has overwritten the map with the new room. Because the cache holds
converted copies in their own allocations rather than pointers into the map, a sample
still streaming across that read is reading memory nobody touched. The flush then stops
the channels and frees it. Had the design played in place from the map, this ordering
would have been a live defect.

## Two frictions

**Both concrete `IPcmFile` subclasses load from a `Cd::File`.** `RawPcm`
(`srl_sound.hpp:504`) and `WaveSound` (`:551`) both take a file and read it. Our samples
are already in RAM, inside the emulated 68000 map. `IPcmFile`'s members are `protected`
and its `PlayOnChannel` is public and inherited, so a third subclass that adopts a
pointer instead of reading a file is a handful of lines. That is the intended extension
point, not a workaround.

**`slPCMOn` will not play anything shorter than 0x900 bytes.** `RawPcm`'s constructor
clamps to `Max(size, 0x900)` and zero-fills the tail with the comment "slPCMOn won't
play samples shorter than 0x900" (`srl_sound.hpp:522-531`). 0x900 = 2,304 bytes = 0.288 s
at 8 kHz. Every sample shorter than that must be copied into a zero-padded buffer. This
is the single largest reason the design cannot play samples in place out of the map.

## Architecture

Two files, split on the line this repo has already drawn twice — once for `discsec.c`
(sector arithmetic) and once for `cdda_classify.c` (the resume/restart decision). Pure
logic that can fail plausibly goes into a C file with host unit tests; the SRL calls go
into the `.cxx`.

```
saturn/src/sfxconv.c     pure, testable: decode table, padded size, table walk + bounds
saturn/src/sfxconv.h
saturn/src/system/sound_srl.cxx   SRL glue: the cache, MemPcm, play_sample, flush
```

`sfxconv.c` includes `vm.h` and nothing else. It has no SRL dependency, compiles under
`gcc -std=c99 -Wall -Wextra -Werror` on the host, and is exercised by
`saturn/tests/test_sfxconv.c` the same way `test_discsec.c` exercises `discsec.c`.

The split earns its keep because the failure mode is exactly the one `run_tests.sh`'s
header describes: a wrong decode table or an off-by-eight in the header walk produces
*sound*, just the wrong sound, and diagnosing that by ear on an emulator costs a round
trip per attempt.

## Components

### `sfxconv.c` — the pure half

**`sfxconv_locate(int index, int *out_offset, int *out_length)`**

Walks the same three indirections the host walks, with bounds checks the host does not
have:

```
sample_ptr = get_long(0xf90c)
ptr        = get_long(sample_ptr + index * 4)
length     = get_long(ptr)
data       = ptr + 8            /* 4-byte length, 4 bytes of unknown flags */
```

Returns zero and leaves `*out_*` untouched unless every one of these holds:
`sample_ptr`, `sample_ptr + index*4 + 4`, `ptr + 8` and `ptr + 8 + length` all fall
inside `[0, MEMORY_SIZE)`, and `length > 0`.

The host has no such guard. It gets away with it because its map is a 512 KB file-scope
`static` with the rest of `.bss` around it, so a wild pointer reads garbage and keeps
running. On Saturn the map is a 512 KB `saturn_lwram_alloc` block with the allocator's
own structures next to it, and `get_byte` is an unchecked `memory[offset]`. A garbage
table entry — from an unloaded room, a truncated read, or a script index the data does
not cover — would walk off it. Everything past the intro is unexercised (see the
2026-08-06 handoff), so this is exactly where a first-contact bug would land.

**`sfxconv_decode_byte(unsigned char u)`** — a 256-entry lookup table, not a branch.

The host's two-branch form is reproduced exactly, including its overflow:

| input `u` | host result | note |
|---|---|---|
| `0x00`–`0x7F` | `0`…`+127` | `s = u` |
| `0x80` | `−128` | `s = u` overflows `signed char`; see below |
| `0x81`–`0xFF` | `−1`…`−127` | `s = 0 - (u & 0x7f)` |

A table is used because it is one indexed load per byte instead of two compares on a
per-byte loop over the whole sample, and because the table *is* the unit test fixture —
all 256 entries get pinned, so the mapping cannot drift silently.

**`sfxconv_padded_size(int length)`** — `length < 0x900 ? 0x900 : length`. One line,
tested, and named so the 0x900 constant appears once.

**`sfxconv_decode_into(int offset, int length, signed char *dst, int dst_size)`** —
decodes `length` bytes from the map at `offset` through the table into `dst`, then zeros
`dst[length .. dst_size)`.

This exists so that `vm.h` never appears in `sound_srl.cxx`. If the decode loop lived in
the `.cxx`, that file would need `get_byte`, and `vm.h` carries no `extern "C"` guard of
its own — every engine header the Saturn backends include (`disc.h`, `discsec.h`,
`cdtoc.h`, …) has one, and `vm.h` does not, so including it from C++ would declare
`get_byte` with C++ linkage and fail the link. Wrapping it at the call site would work
and would also be the first place in the port where a `.cxx` reaches into the map. The
boundary is better drawn here: `sfxconv.c` owns every map access, and the whole
conversion — table, loop and padding together — becomes host-testable rather than just
the table.

### `sound_srl.cxx` — the SRL half

**The cache.** Two parallel 256-entry static arrays:

```c
static signed char *g_sampleData[256];   /* LWRAM blocks, NULL when uncached */
static unsigned long g_sampleSize[256];  /* padded size, what slPCMOn is given */
```

2,048 bytes of `.bss`, which is HWRAM. That is stated plainly here because
`disc_srl.cxx`'s banner tracks this project's `.bss` to the byte and the next person
measuring it should find the 2,048 accounted for rather than have to chase it. 256
entries rather than 255 because `next_pc()` yields a byte and the guard costs nothing.

**`MemPcm`, a file-local `IPcmFile` subclass.** Adopts a pointer, a size, `Mono`,
`Pcm8Bit` and `sampleRate = 8000`. Non-owning: the cache owns the memory, `MemPcm` is a
parameter bundle constructed on the stack per play and discarded. `IPcmFile`'s
destructor is empty and non-virtual, which is correct for a non-owning subclass and
would be a bug in an owning one.

Discarding it immediately is safe even though playback is asynchronous:
`PlayOnChannel` copies mode, pitch, level and pan into `Pcm::Channels[channel]` and
passes the data pointer and size to `slPCMOn` by value, so nothing the driver goes on to
use lives in the `MemPcm` itself. The *buffer* must outlive playback; the handle need
not.

**`play_sample(index, volume, channel)`**

1. `channel` outside `0..3` → return. The host would index `Mix_HaltChannel` out of
   range; we will not.
2. `index == 0` → `Pcm::StopSound(0..3)`, return. This is the host's
   `stop_all_channels()`.
3. `index -= 1`; `index` outside `0..255` → return.
4. Cache miss → `sfxconv_locate`; on failure return. Otherwise
   `saturn_lwram_alloc(sfxconv_padded_size(length))`; on failure return (the host's
   `malloc` failure path also just returns). `sfxconv_decode_into` fills and pads it;
   store both arrays.
5. `Pcm::StopSound(channel)` unconditionally, then `MemPcm(...).PlayOnChannel(channel,
   volume >> 1)`.

Step 5 is the one place SRL's default behaviour is wrong for us. `PlayOnChannel` opens
with `if (!slPCMStat(&Channels[channel]))` and **refuses** on a busy channel; the host's
`Mix_PlayChannelTimed` **interrupts** it. A script that fires two sounds on one channel
expects the second. Stopping first restores that, and is a no-op when the channel is
already free.

`volume >> 1` maps the engine's `0..0xff` onto SRL's `0..127` exactly — the same shift
the host applies for SDL_mixer's identical range.

**`sound_flush_cache()`** — `StopSound` on all four channels **first**, then
`saturn_lwram_free` every non-NULL entry and NULL it.

The ordering is not decorative. Freeing a block that `slPCMOn` is still streaming from
hands the SGL driver a pointer into a freed allocation, and the LWRAM allocator writes
its own bookkeeping into freed blocks. The result would be a burst of noise at best.

## Decisions worth naming

### LWRAM, not `malloc`

`saturn_compat.cxx`'s `malloc` is the HWRAM arena and **deliberately refuses to fall
back to LWRAM**; `disc_srl.cxx` measured that heap at **62,528 bytes** total
(`__heap_start 0x060b0bc0 .. __heap_end 0x060c0000`). The cache cannot live there.

It goes to `saturn_lwram_alloc`, which spends the LWRAM pool's **114,688-byte
remainder** — the same remainder `saturn_compat.cxx:194` says a `malloc` fallback "would
spend and fragment". This sub-project spends it on purpose. That is a change of stance
and is recorded as one: the remainder existed to be spent by something, and a bounded,
per-room, wholly-freed sample cache is a better claim on it than an unbounded general
heap fallback would have been.

The bounce buffer in `disc_srl.cxx` takes 32,772 of that remainder on the first
misaligned read and never gives it back, so the **worst-case budget is 81,916 bytes**.

### The `0x80` decode edge

`0x80` is sign-magnitude negative zero. The correct decode is `0`. The host produces
`−128` — a full-scale negative spike — because `s = u` assigns 128 to a `signed char`.

**This spec reproduces the host.** Parity is the stated goal, and the host is the only
reference implementation anyone here can run. But the decision is isolated to one
entry of one table, the unit test pins that entry explicitly with a comment naming it,
and changing it later is a one-character edit rather than an archaeology exercise.

### Padding holds the channel

A 500-byte sample padded to 2,304 occupies its channel for 0.288 s where the host would
hold it for 0.063 s. Inaudible — the padding is silence — and harmless, because
`play_sample` force-stops the channel before every play. Recorded because "the channel
is busy longer than on the host" is a true statement that will look like a bug to
whoever notices it first.

### `sfxconv.c` sees the map through `vm.h`

`sfxconv_locate` calls `get_long`, not a raw pointer walk. `get_long` assembles its
result byte by byte (`vm.c:130`), which is why the map has been endian-clean since the
port began. Reaching around it with a cast would reintroduce exactly the class of defect
the 2026-08-06 handoff documents at length.

## Risks

**1. `slPCMOn` may not be able to stream from LWRAM.** Unresolved, and the largest
risk in the sub-project. SGL streams into `SoundRAM + 0x78000`; whether it moves those
bytes with the CPU or with SCU DMA is not visible from the headers, and the Saturn's bus
rules constrain what SCU DMA may use as a source when the destination is the B-bus,
where sound RAM lives. `LIBSGL.A` is a binary and the SRL sample plays only from
`autonew` allocations, so nothing in the tree answers it.

*This is settled by a spike before any other work begins*, not by reasoning. The project
has now paid twice for theorising ahead of instrumenting — see the 2026-08-06 handoff's
"Instrument before theorising", which cost two sessions.

*Fallback if the answer is no:* the HWRAM heap, 62,528 bytes, with a hard cap and
drop-on-full. Tighter, and it competes with everything else on that heap, but it is a
real fallback rather than a dead end. A second fallback — a bounce through a small HWRAM
staging buffer — does not exist, because streaming needs the whole buffer live for the
duration.

**2. Sample sizes are unmeasured.** The 81,916-byte budget is compared against nothing.
A room whose sample set exceeds it gets silent drops with no diagnostic. The host build
is a working oracle for this — `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./saturn/src/alien.exe
--debug --room N` runs headless and prints opcode traces (verified 2026-08-07 on room 7)
— so a probe printing every `sample length` across the rooms turns this into a number
before the design depends on a guess.

**3. Cache exhaustion is silent.** By design, matching the host, whose `malloc` failure
path prints to `stderr` and returns. On Saturn `printf` goes to SRL's debug text layer
and would paint over the game, so even that is not available. If risk 2's measurement
shows the budget is tight, a one-shot warning on the first failure — the pattern
`disc_srl.cxx` already uses for the small-`cdbuf` case — is the answer, and the spike
should look for the room that needs it.

**4. Streaming starves during long disc reads.** The SGL driver services its stream from
the sound interrupt, so a blocking read that never yields would stall it. `disc_read_file`
now yields a frame per iteration (commit `84e210a`), which is why this is a risk to
verify rather than a known defect. The audible symptom would be a sample stuttering or
cutting off across a room load.

**5. `length` is assumed to be a byte count.** The host reads `get_long(ptr)` and uses it
directly as bytes, with `ptr + 8` as the data start and the four bytes between called
"some unknown flags". If it were a word count every sample would play at half length.
The host is the only evidence either way. A sample that sounds truncated by exactly half
is the tell.

## What stays at zero

- **No per-frame tick.** The SGL driver services its own stream; nothing is added to
  `platform_frame`.
- **No change to `platform.h`, `disc.h`, or any engine `.c` outside the new `sfxconv.c`.**
  `decode.c` and `main.c` already call the two functions; they do not learn that the
  calls now do something.
- **No new SRL initialisation.** `Core::Initialize` already brings the sound driver up.

## Testing

**Host unit tests** — `saturn/tests/test_sfxconv.c`, added to `run_tests.sh` in the
established form:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src \
    -o run_tests_sfxconv test_sfxconv.c ../src/sfxconv.c ../src/vm.c
./run_tests_sfxconv
```

Cases:

1. All 256 decode-table entries, pinned against the host's two-branch form. `0x80 →
   −128` carries its own comment naming it as negative zero and as deliberate.
2. `sfxconv_padded_size`: below, at, and above 0x900.
3. `sfxconv_locate` on a hand-built map: a well-formed entry returns the right offset
   and length; a `sample_ptr` past `MEMORY_SIZE`, a `ptr` past it, a `ptr + 8 + length`
   that straddles the end, and `length == 0` each return failure and leave the outputs
   untouched.
4. `sfxconv_decode_into`: a short sample decodes correctly and its padding is zeroed all
   the way to `dst_size`; a sample already at or above 0x900 is decoded with no padding
   written past `length`.

`test_vm_memory.c` already links `vm.c` on the host, so building the map for case 3
needs no new scaffolding.

**Host oracle probe** — a temporary `LOG` of `index`, `length` and `sfxconv_padded_size`
in the host's `play_sample`, run across several rooms, to produce the per-room totals
risk 2 needs. Removed before the sub-project lands, the way the CD-DA timing probe was
(`d47ffe6`).

**Emulator** — Suinevere runs it. See [Acceptance](#acceptance).

## Build and test

- `sh saturn/tests/run_tests.sh` — all suites, including the new one.
- `make -C saturn/src` — the host build. `src/sound.c` stays filtered out of the SH-2
  build by name (`saturn/makefile:67`) and is not edited, so the host's SFX path carries
  no regression risk.
- `./compile.bat` — the Saturn disc.

## Out of scope

- **`sound_init` / `sound_done`.** Nothing on Saturn calls either. `sound_init` would
  zero a table the C runtime already zeroes, and `sound_done` would free a cache at a
  shutdown that never happens — `exit()` on Saturn parks in a `Synchronize()` loop
  without unwinding (`saturn_compat.h`). Adding them means adding code that provably
  never runs. `sound_srl.cxx`'s banner already argues their absence; that argument
  stands and gets extended, not reversed.
- **Panning.** The engine has no pan concept. Centred.
- **Music ducking under SFX, and SFX volume as a separate mix.** No engine concept
  drives either, the same reason the CD-DA spec gave for deferring music volume.
- **`src/sound.c` and the host backend.** Untouched.
- **A backup-RAM save backend**, which `saturn_filestub.c` still stubs. Unrelated.

## Acceptance

1. `sh saturn/tests/run_tests.sh` passes, including `test_sfxconv`.
2. `make -C saturn/src` still builds. `src/sound.c` is byte-identical.
3. **The spike answers risk 1 before anything else lands**, and its answer is recorded
   in this document — including the case where the answer is "no" and the design moves
   to the HWRAM fallback.
4. Measured sample sizes from the host oracle are recorded here as a per-room worst
   case against the 81,916-byte budget.
5. `./compile.bat` produces a disc on which a scripted sound effect is audible, on the
   channel the script asked for, at a volume that tracks the script's. Confirmed by
   Suinevere on the emulator.
6. A room transition with a sample playing does not produce noise, a stall, or a hang —
   the flush-ordering and streaming-starvation cases (risk 4).
7. Music and SFX coexist: a sample plays over CD-DA without either dropping.
