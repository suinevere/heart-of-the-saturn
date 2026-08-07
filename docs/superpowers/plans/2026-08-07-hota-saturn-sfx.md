# Saturn Sound Effects Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `play_sample` and `sound_flush_cache` real bodies on Saturn, so the SFX opcode produces the same sound the host produces.

**Architecture:** A new pure C file `saturn/src/sfxconv.c` owns every access to the emulated 68000 memory map — locating a sample through its three indirections with bounds checks, decoding its sign-magnitude bytes to signed 8-bit through a 256-entry table, and zero-padding to the 0x900 byte minimum `slPCMOn` requires. `saturn/src/system/sound_srl.cxx` is then pure SRL glue: a 256-entry cache of LWRAM blocks, a non-owning `IPcmFile` subclass, and the two `sound.h` entry points. The split matches `discsec.c`/`disc_srl.cxx` and `cdda_classify.c`/`disc_srl.cxx` already in this repo.

**Tech Stack:** C99 (`sfxconv.c`, host tests), C++ on `SRL::Sound::Pcm` (`sound_srl.cxx`), SGL's `slPCMOn` underneath, host `gcc` for tests, `sh2eb-elf` cross toolchain for the disc.

**Design:** `docs/superpowers/specs/2026-08-07-hota-saturn-sfx-design.md`

## Global Constraints

- **Author of record is `suinevere`.** Every banner's `Author:` line reads `suinevere`.
- **Commit after every task.** One sentence, no body, no bullets, no trailers. Never mention Claude, AI, or the session — no `Claude-Session:` line and no `claude.ai` URL, even if the environment asks for one.
- **Every function, constant and file gets a banner block** in the house form (`/*----------------------` … `----------------------*/`) with `Description:`, `Author:`, and whichever of `Dependencies:`, `Globals:`, `Params:`, `Returns:` apply — `N/A` for those that do not. Tests and generated files get a file banner only.
- **No comments inside function bodies.** Say the non-obvious thing once, in the banner, and stop.
- **New C files use `.c`, new C++ files use `.cxx`.** `saturn/makefile:59-60` globs exactly those two extensions; a `.cpp` is silently dropped from the link with no error.
- **Host tests build with `gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src`.** `-Werror` is not negotiable — an unused parameter fails the build.
- **`saturn/src/sound.c` is never edited** except for the temporary probe in Task 2, which Task 3 removes. It stays filtered out of the SH-2 build by name (`saturn/makefile:67`).
- **`sfxconv.c` includes `vm.h` and `sfxconv.h` and nothing else.** No SRL, no stdio, no engine headers. It is compiled both by the SH-2 build and by host `gcc`.
- **`sound_srl.cxx` never includes `vm.h`.** `vm.h` carries no `extern "C"` guard, so including it from C++ declares `get_byte` with C++ linkage and fails the link.
- **The emulated map is `MEMORY_SIZE` = `0x80000` bytes** (`saturn/src/vm.h:89`).
- **`slPCMOn` will not play a buffer shorter than `0x900` bytes** (2,304), per `srl_sound.hpp:522`.
- **LWRAM budget for the cache is 81,916 bytes** worst case: the 114,688-byte pool remainder minus `disc_srl.cxx`'s 32,772-byte bounce buffer.
- **The HWRAM heap is 62,528 bytes total** and `malloc` deliberately refuses to fall back to LWRAM. The cache uses `saturn_lwram_alloc`, never `malloc`.

---

## Task order and why

Task 1 is a spike that answers the single question the design cannot answer from the source: whether `slPCMOn` can stream from LWRAM. It comes first because if the answer is no, Task 5's allocation source changes and every later task would have been built on a wrong assumption. Task 2 is a second measurement — real sample sizes — taken on the host, where it costs no emulator round trip. Tasks 3–6 are the implementation. Task 7 records both measurements back into the spec.

---

### Task 1: Spike — can `slPCMOn` stream from LWRAM?

**Files:**
- Modify: `saturn/src/system/sound_srl.cxx` (temporary; Task 5 replaces the file wholesale)
- Test: none — this task's output is an observation on hardware, recorded in the commit message

**Interfaces:**
- Consumes: nothing
- Produces: a yes/no answer that Task 5 depends on. If **yes**, Task 5 allocates with `saturn_lwram_alloc`. If **no**, Task 5 allocates with `malloc` (HWRAM, 62,528 bytes) and adds a running-total cap.

This is a throwaway. It exists because the SGL PCM driver streams into `SoundRAM + 0x78000` (`modules/sgl/SRC/workarea.c:30`), `LIBSGL.A` is a binary, and the SRL sample only ever plays from `autonew` allocations — so nothing in the tree says whether LWRAM is a legal source when the destination is on the B-bus. The 2026-08-06 handoff's "instrument before theorising" lesson has cost this project two sessions; this is that lesson applied before the design commits.

- [ ] **Step 1: Replace `sound_srl.cxx` with a spike that plays a generated tone from LWRAM**

Replace the whole file. The tone is generated, not loaded, so the spike depends on nothing else in the port.

```cpp
/*----------------------
 | sound_srl.cxx
 | Description: TEMPORARY SPIKE. Answers one question before the sound-effects
 |   design commits to it: can slPCMOn stream from Low Work RAM? Plays a
 |   generated square wave out of a saturn_lwram_alloc block on the first
 |   play_sample call. Replaced wholesale by the real backend.
 | Author: suinevere
 | Dependencies: srl.hpp, sound.h, saturn_compat.h
 ----------------------*/

#include <srl.hpp>

#include "sound.h"
#include "saturn_compat.h"

namespace
{
	/*----------------------
	 | SpikeTone
	 | Description: Non-owning IPcmFile over a caller-supplied buffer, so the
	 |   spike can hand slPCMOn a pointer of its own choosing. IPcmFile's
	 |   members are protected and its destructor is empty, which is what makes
	 |   a non-owning subclass correct here.
	 | Author: suinevere
	 ----------------------*/
	class SpikeTone : public SRL::Sound::Pcm::IPcmFile
	{
	public:
		SpikeTone(signed char *buffer, unsigned long bytes)
		{
			this->data = (int8_t *)buffer;
			this->dataSize = bytes;
			this->mode = _Mono;
			this->depth = _PCM8Bit;
			this->sampleRate = 8000;
		}
	};
}

extern "C" {

/*----------------------
 | play_sample
 | Description: Spike body. Allocates 8,000 bytes of LWRAM on first call, fills
 |   it with a 250 Hz square wave and plays it on channel 0. Every argument is
 |   discarded -- the question is whether any sound comes out of an LWRAM
 |   buffer at all, not whether the right sound does.
 | Author: suinevere
 | Globals: N/A
 | Params: index -- ignored; volume -- ignored; channel -- ignored
 | Returns: N/A
 ----------------------*/
void play_sample(int index, int volume, int channel)
{
	static signed char *tone = 0;
	int i;

	(void)index;
	(void)volume;
	(void)channel;

	if (tone == 0)
	{
		tone = (signed char *)saturn_lwram_alloc(8000);
		if (tone == 0)
		{
			printf("SPIKE: lwram alloc failed");
			return;
		}

		for (i = 0; i < 8000; i++)
		{
			tone[i] = ((i / 16) & 1) ? 100 : -100;
		}
	}

	printf("SPIKE: play from lwram %p", (void *)tone);
	SRL::Sound::Pcm::StopSound(0);
	SpikeTone(tone, 8000).PlayOnChannel(0, 100);
}

/*----------------------
 | sound_flush_cache
 | Description: Spike body. Nothing to flush.
 | Author: suinevere
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sound_flush_cache()
{
}

}
```

- [ ] **Step 2: Build the disc**

Run: `cd saturn && ./compile.bat`
Expected: builds clean; `saturn/BuildDrop/Heart of the Alien (USA).cue` is produced. If it fails to compile, the failure is in the spike, not in SRL — fix it here before going further.

- [ ] **Step 3: Hand the disc to Suinevere and record the answer**

Do **not** launch the emulator from a tool call — Suinevere runs it (`user-runs-the-emulator`).

Ask for: reach any moment where a script fires the SFX opcode, and report whether a buzzing tone is heard and whether `SPIKE: play from lwram` appears on screen.

Three outcomes:
- **Tone heard.** LWRAM works. Task 5 uses `saturn_lwram_alloc`. Record this.
- **Message printed, no tone.** LWRAM is not a legal stream source. Task 5 uses `malloc` and gains the cap described in its Step 6 alternative. Record this.
- **Neither.** Nothing reached the opcode. Not an answer about LWRAM — the spike must be re-triggered somewhere reachable (call it once unconditionally from `platform_init` instead) before drawing any conclusion.

- [ ] **Step 4: Commit the spike with the answer in the message**

```bash
git add saturn/src/system/sound_srl.cxx
git commit -m "Spike whether slPCMOn will stream from an LWRAM buffer before the sample cache commits to living there, and record the answer."
```

---

### Task 2: Measure real sample sizes on the host

**Files:**
- Modify: `saturn/src/sound.c:99` (temporary probe; Task 3 removes it)

**Interfaces:**
- Consumes: nothing
- Produces: a per-room worst-case byte total, recorded in Task 7. If it exceeds the 81,916-byte LWRAM budget, Task 5 gains a one-shot exhaustion warning.

The host build is a working headless oracle — verified 2026-08-07 that `--room 7` runs and prints opcode traces. This measurement costs no emulator round trip.

- [ ] **Step 1: Add the probe**

In `saturn/src/sound.c`, immediately after the existing `LOG(("sample data starts at 0x%x\n", ptr));` line, add:

```c
	fprintf(stderr, "PROBE sample=%d length=%d padded=%d\n",
	        index, length, length < 0x900 ? 0x900 : length);
```

`fprintf(stderr, ...)` rather than `LOG`, so the probe lines are separable from the opcode trace by stream. The padding arithmetic is inlined here rather than calling `sfxconv_padded_size`, so that `saturn/src/Makefile`'s explicit `OBJS` list does not have to gain `sfxconv.o` for a probe that is about to be deleted.

- [ ] **Step 2: Build the host**

Run: `make -C saturn/src`
Expected: builds clean, produces `saturn/src/alien.exe`.

- [ ] **Step 3: Collect sizes across rooms**

```bash
cd saturn
for r in 1 2 3 4 5 6 8 9; do
  echo "=== room $r ==="
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 30 \
    ./src/alien.exe --debug --room $r 2>&1 >/dev/null | grep PROBE | sort -u
done
```

`2>&1 >/dev/null` keeps stderr and discards the opcode trace on stdout. `sort -u` collapses repeats, because a room replays the same sample many times and the cache stores it once — the sum of the *unique* padded sizes is the number the budget cares about.

Expected: some rooms print `PROBE` lines. A room that prints none either uses no samples or was not driven far enough by the 30-second timeout; note which, and do not report zero as a measurement.

- [ ] **Step 4: Record the worst room's total**

Sum the unique `padded=` values per room. Keep the highest total and the room it came from. This number goes into the spec in Task 7.

- [ ] **Step 5: Commit the probe**

Committed rather than kept in the working tree, so that Task 3's removal is a reviewable diff and the measurement is reproducible from history.

```bash
git add saturn/src/sound.c
git commit -m "Probe the host's play_sample for per-sample lengths, so the Saturn cache's LWRAM budget is measured against real numbers instead of guessed at."
```

---

### Task 3: `sfxconv` — the decode table

**Files:**
- Create: `saturn/src/sfxconv.h`
- Create: `saturn/src/sfxconv.c`
- Create: `saturn/tests/test_sfxconv.c`
- Modify: `saturn/tests/run_tests.sh` (append a suite)
- Modify: `.gitignore:46` (append two binaries)
- Modify: `saturn/src/sound.c:99` (remove the Task 2 probe)

**Interfaces:**
- Consumes: `MEMORY_SIZE` from `vm.h`
- Produces:
  - `signed char sfxconv_decode_byte(unsigned char u);`
  - `#define SFXCONV_MIN_PLAYABLE 0x900`

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_sfxconv.c`:

```c
/*----------------------
 | test_sfxconv.c
 | Description: Host unit tests for sfxconv.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 |
 |   The decode table is pinned entry by entry against the two-branch form in
 |   src/sound.c, which is the only reference implementation anyone here can
 |   run. A wrong table does not error; it produces sound, just the wrong
 |   sound, and diagnosing that by ear on an emulator costs a round trip per
 |   attempt.
 | Author: suinevere
 | Dependencies: sfxconv.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "sfxconv.h"

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n"                      \
                   "  expected = %lld\n",                                     \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

/* src/sound.c's decode, transcribed byte for byte including its overflow:
   `s` is a signed char, so u == 0x80 assigns 128 and lands on -128. That is
   sign-magnitude negative zero, whose correct value is 0. The host is the
   reference this port is matched against, so the host's answer is what is
   pinned -- see the design spec's "The 0x80 decode edge". */
static signed char host_decode(unsigned char u)
{
	signed char s = 0;

	if (u > 0x80)
	{
		s = 0 - (u & 0x7f);
	}
	else if (u <= 0x80)
	{
		s = u;
	}

	return s;
}

static void test_decode_table_matches_host(void)
{
	int u;

	for (u = 0; u <= 0xff; u++)
	{
		CHECK_EQ(sfxconv_decode_byte((unsigned char)u), host_decode((unsigned char)u));
	}
}

static void test_decode_edges(void)
{
	CHECK_EQ(sfxconv_decode_byte(0x00), 0);
	CHECK_EQ(sfxconv_decode_byte(0x7f), 127);
	CHECK_EQ(sfxconv_decode_byte(0x80), -128);
	CHECK_EQ(sfxconv_decode_byte(0x81), -1);
	CHECK_EQ(sfxconv_decode_byte(0xff), -127);
}

int main(void)
{
	test_decode_table_matches_host();
	test_decode_edges();

	if (g_fail != 0)
	{
		printf("test_sfxconv: %d failure(s)\n", g_fail);
		return 1;
	}

	printf("test_sfxconv: all pass\n");
	return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run:
```bash
cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src \
    -o run_tests_sfxconv test_sfxconv.c ../src/sfxconv.c
```
Expected: FAIL — `sfxconv.h: No such file or directory`.

- [ ] **Step 3: Write `sfxconv.h`**

```c
/*----------------------
 | sfxconv.h
 | Description: Turns a sound-effect index into playable sample bytes: locates
 |   the sample inside the emulated 68000 map, decodes its 8-bit
 |   sign-magnitude data to the signed 8-bit the SCSP plays natively, and pads
 |   it to the minimum length slPCMOn will accept.
 |
 |   It is a separate file from sound_srl.cxx for two reasons. The first is
 |   testability, the same reason discsec.c and cdda_classify.c are separate:
 |   a wrong decode table or an off-by-eight in the header walk produces sound,
 |   just the wrong sound, and that is diagnosed by ear on an emulator at a
 |   round trip per attempt unless a host test pins it in milliseconds.
 |
 |   The second is linkage. Every map access in this port goes through vm.h's
 |   get_byte/get_long, which assemble their results byte by byte and are the
 |   reason the map has been endian-clean since the port began. vm.h carries no
 |   extern "C" guard, unlike disc.h, discsec.h, cdtoc.h and the rest of the
 |   seam headers, so including it from a .cxx would declare get_byte with C++
 |   linkage and fail the link. Keeping every map access on this side of the
 |   boundary means sound_srl.cxx never needs it.
 |
 |   Design: docs/superpowers/specs/2026-08-07-hota-saturn-sfx-design.md
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef SFXCONV_H
#define SFXCONV_H

/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link, the way the six seam headers did before 7f66fe3. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SFXCONV_MIN_PLAYABLE
 | Description: The shortest buffer slPCMOn will play, in bytes. SRL's own
 |   RawPcm constructor clamps to this with the comment "slPCMOn won't play
 |   samples shorter than 0x900" (srl_sound.hpp:522), and pads the tail with
 |   zeroes. 2,304 bytes is 0.288 seconds at 8 kHz, so a short sample holds its
 |   channel far longer than the host would -- silently, because the padding is
 |   silence, and harmlessly, because play_sample stops a channel before
 |   reusing it.
 | Author: suinevere
 ----------------------*/
#define SFXCONV_MIN_PLAYABLE 0x900

/*----------------------
 | sfxconv_decode_byte
 | Description: Converts one 8-bit sign-magnitude sample byte to signed 8-bit.
 |   Reproduces src/sound.c's two-branch form exactly, including its overflow
 |   at 0x80: that byte is sign-magnitude negative zero, whose correct value is
 |   0, and the host's `s = u` assigns 128 to a signed char and lands on -128.
 |   The host is the only reference implementation anyone here can run, so
 |   parity wins -- but the divergence is one table entry, pinned by name in
 |   tests/test_sfxconv.c, so reversing the decision later is one character.
 | Author: suinevere
 | Globals: N/A
 | Params: u -- raw byte from the emulated 68000 map
 | Returns: The decoded signed sample value
 ----------------------*/
signed char sfxconv_decode_byte(unsigned char u);

#ifdef __cplusplus
}
#endif

#endif /* SFXCONV_H */
```

- [ ] **Step 4: Write `sfxconv.c`**

```c
/*----------------------
 | sfxconv.c
 | Description: Sample location, decoding and padding for the sound-effect
 |   backend. See sfxconv.h for why this is its own translation unit.
 | Author: suinevere
 | Dependencies: sfxconv.h, vm.h
 ----------------------*/
#include "sfxconv.h"
#include "vm.h"

/*----------------------
 | g_decode
 | Description: sfxconv_decode_byte as a 256-entry table. A table rather than
 |   two compares because the decode runs once per byte over a whole sample --
 |   up to tens of thousands of iterations for one play -- and because the
 |   table is literally the test fixture, so the mapping cannot drift without
 |   test_sfxconv.c saying so. 0x00..0x7f map to 0..127, 0x80 maps to -128 (see
 |   sfxconv_decode_byte), and 0x81..0xff map to -1..-127.
 | Author: suinevere
 ----------------------*/
static const signed char g_decode[256] = {
	   0,    1,    2,    3,    4,    5,    6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
	  16,   17,   18,   19,   20,   21,   22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
	  32,   33,   34,   35,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,
	  48,   49,   50,   51,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   62,   63,
	  64,   65,   66,   67,   68,   69,   70,   71,   72,   73,   74,   75,   76,   77,   78,   79,
	  80,   81,   82,   83,   84,   85,   86,   87,   88,   89,   90,   91,   92,   93,   94,   95,
	  96,   97,   98,   99,  100,  101,  102,  103,  104,  105,  106,  107,  108,  109,  110,  111,
	 112,  113,  114,  115,  116,  117,  118,  119,  120,  121,  122,  123,  124,  125,  126,  127,
	-128,  -1,   -2,   -3,   -4,   -5,   -6,   -7,   -8,   -9,  -10,  -11,  -12,  -13,  -14,  -15,
	 -16,  -17,  -18,  -19,  -20,  -21,  -22,  -23,  -24,  -25,  -26,  -27,  -28,  -29,  -30,  -31,
	 -32,  -33,  -34,  -35,  -36,  -37,  -38,  -39,  -40,  -41,  -42,  -43,  -44,  -45,  -46,  -47,
	 -48,  -49,  -50,  -51,  -52,  -53,  -54,  -55,  -56,  -57,  -58,  -59,  -60,  -61,  -62,  -63,
	 -64,  -65,  -66,  -67,  -68,  -69,  -70,  -71,  -72,  -73,  -74,  -75,  -76,  -77,  -78,  -79,
	 -80,  -81,  -82,  -83,  -84,  -85,  -86,  -87,  -88,  -89,  -90,  -91,  -92,  -93,  -94,  -95,
	 -96,  -97,  -98,  -99, -100, -101, -102, -103, -104, -105, -106, -107, -108, -109, -110, -111,
	-112, -113, -114, -115, -116, -117, -118, -119, -120, -121, -122, -123, -124, -125, -126, -127
};

signed char sfxconv_decode_byte(unsigned char u)
{
	return g_decode[u];
}
```

`vm.h` is included even though this step does not use it yet, because Task 4 adds the functions that do and adding it now keeps that diff to the code being added.

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src \
    -o run_tests_sfxconv test_sfxconv.c ../src/sfxconv.c && ./run_tests_sfxconv
```
Expected: `test_sfxconv: all pass`

If it fails on `unused variable` or similar from `-Werror`, fix the code, not the flags.

- [ ] **Step 6: Wire the suite into `run_tests.sh`**

Append to `saturn/tests/run_tests.sh`:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_sfxconv test_sfxconv.c ../src/sfxconv.c
./run_tests_sfxconv
```

- [ ] **Step 7: Ignore the new test binaries**

Append to `.gitignore`, after the `run_tests_discsec.exe` line:

```
saturn/tests/run_tests_sfxconv
saturn/tests/run_tests_sfxconv.exe
```

- [ ] **Step 8: Remove the Task 2 probe from `sound.c`**

Delete the three-line `fprintf(stderr, "PROBE ...` block added in Task 2, restoring `saturn/src/sound.c` to byte-identical with its state before that task. Verify: `git diff <task-2-parent-commit> -- saturn/src/sound.c` prints nothing.

The measurement it produced lives in the spec (Task 7), not in the code — same discipline as the CD-DA timing probe retired in `d47ffe6`.

- [ ] **Step 9: Run the whole suite**

Run: `sh saturn/tests/run_tests.sh`
Expected: every suite passes, including `test_sfxconv: all pass`.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/sfxconv.h saturn/src/sfxconv.c saturn/tests/test_sfxconv.c \
        saturn/tests/run_tests.sh .gitignore saturn/src/sound.c
git commit -m "Add sfxconv with the sample decode pinned as a 256-entry table against the host's own two-branch form, and retire the host probe now that its numbers are recorded."
```

---

### Task 4: `sfxconv` — bounds-checked location, padding, and the decode loop

**Files:**
- Modify: `saturn/src/sfxconv.h`
- Modify: `saturn/src/sfxconv.c`
- Modify: `saturn/tests/test_sfxconv.c`
- Modify: `saturn/tests/run_tests.sh` (the new suite must also link `vm.c`)

**Interfaces:**
- Consumes: `sfxconv_decode_byte`, `SFXCONV_MIN_PLAYABLE` from Task 3; `get_long`, `get_byte`, `MEMORY_SIZE`, `vm_alloc_memory`, `get_memory_ptr` from `vm.h`
- Produces:
  - `int sfxconv_locate(int index, int *out_offset, int *out_length);` — returns 1 on success, 0 on refusal. `index` is already zero-based (the caller has done `index - 1`).
  - `int sfxconv_padded_size(int length);`
  - `void sfxconv_decode_into(int offset, int length, signed char *dst, int dst_size);`

- [ ] **Step 1: Write the failing tests**

Add to `saturn/tests/test_sfxconv.c` — the include block gains `vm.h` and `string.h`, and these functions go above `main`:

```c
/* The three indirections src/sound.c walks, laid into a real map so the walk
   is exercised rather than described:
       table   = get_long(0xf90c)
       entry   = get_long(table + index * 4)
       length  = get_long(entry)
       data    = entry + 8
   The four bytes between the length and the data are called "some unknown
   flags" by sound.c and are never read. */
static void put_long(int offset, unsigned long value)
{
	unsigned char *m = get_memory_ptr(offset);

	m[0] = (unsigned char)(value >> 24);
	m[1] = (unsigned char)(value >> 16);
	m[2] = (unsigned char)(value >> 8);
	m[3] = (unsigned char)(value);
}

#define TEST_TABLE 0x20000
#define TEST_ENTRY 0x30000

/* Every case below uses index 0, so the entry sits at `table` itself. The two
   range guards are not defensiveness: the out-of-range cases deliberately pass
   a table or an entry at or past MEMORY_SIZE, and writing there would run off
   the end of the host's 512 KB static map and corrupt whatever .bss follows
   it -- the test would then be measuring its own damage. */
static void build_map(unsigned long table, unsigned long entry, unsigned long length)
{
	memset(get_memory_ptr(0), 0, MEMORY_SIZE);
	put_long(0xf90c, table);

	if (table + 4 <= (unsigned long)MEMORY_SIZE)
	{
		put_long((int)table, entry);
	}

	if (entry + 8 <= (unsigned long)MEMORY_SIZE)
	{
		put_long((int)entry, length);
	}
}

static void test_locate_well_formed(void)
{
	int offset = -1;
	int length = -1;

	build_map(TEST_TABLE, TEST_ENTRY, 1000);

	CHECK_EQ(sfxconv_locate(0, &offset, &length), 1);
	CHECK_EQ(offset, TEST_ENTRY + 8);
	CHECK_EQ(length, 1000);
}

static void test_locate_refuses_bad_maps(void)
{
	int offset = -1;
	int length = -1;

	build_map(MEMORY_SIZE, TEST_ENTRY, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);
	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);

	build_map(TEST_TABLE, MEMORY_SIZE, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);

	build_map(TEST_TABLE, TEST_ENTRY, 0);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);

	build_map(TEST_TABLE, MEMORY_SIZE - 16, 1000);
	CHECK_EQ(sfxconv_locate(0, &offset, &length), 0);

	CHECK_EQ(offset, -1);
	CHECK_EQ(length, -1);
}

static void test_padded_size(void)
{
	CHECK_EQ(sfxconv_padded_size(1), SFXCONV_MIN_PLAYABLE);
	CHECK_EQ(sfxconv_padded_size(SFXCONV_MIN_PLAYABLE - 1), SFXCONV_MIN_PLAYABLE);
	CHECK_EQ(sfxconv_padded_size(SFXCONV_MIN_PLAYABLE), SFXCONV_MIN_PLAYABLE);
	CHECK_EQ(sfxconv_padded_size(SFXCONV_MIN_PLAYABLE + 1), SFXCONV_MIN_PLAYABLE + 1);
}

static void test_decode_into_pads_short_samples(void)
{
	static signed char dst[SFXCONV_MIN_PLAYABLE];
	unsigned char *m;
	int i;

	memset(get_memory_ptr(0), 0, MEMORY_SIZE);
	m = get_memory_ptr(TEST_ENTRY + 8);
	m[0] = 0x00;
	m[1] = 0x7f;
	m[2] = 0x80;
	m[3] = 0xff;

	memset(dst, 0x5a, sizeof(dst));
	sfxconv_decode_into(TEST_ENTRY + 8, 4, dst, SFXCONV_MIN_PLAYABLE);

	CHECK_EQ(dst[0], 0);
	CHECK_EQ(dst[1], 127);
	CHECK_EQ(dst[2], -128);
	CHECK_EQ(dst[3], -127);

	for (i = 4; i < SFXCONV_MIN_PLAYABLE; i++)
	{
		if (dst[i] != 0)
		{
			g_fail++;
			printf("FAIL padding not zeroed at %d (= %d)\n", i, (int)dst[i]);
			break;
		}
	}
}

static void test_decode_into_writes_nothing_past_length(void)
{
	static signed char dst[SFXCONV_MIN_PLAYABLE + 4];
	int i;

	memset(get_memory_ptr(0), 0, MEMORY_SIZE);
	memset(dst, 0x5a, sizeof(dst));

	sfxconv_decode_into(TEST_ENTRY + 8, SFXCONV_MIN_PLAYABLE + 4, dst,
	                    SFXCONV_MIN_PLAYABLE + 4);

	for (i = 0; i < SFXCONV_MIN_PLAYABLE + 4; i++)
	{
		if (dst[i] != 0)
		{
			g_fail++;
			printf("FAIL decoded byte %d is %d, expected 0\n", i, (int)dst[i]);
			break;
		}
	}
}
```

And in `main`, before the failure check:

```c
	if (vm_alloc_memory() == 0)
	{
		printf("test_sfxconv: vm_alloc_memory failed\n");
		return 1;
	}

	test_locate_well_formed();
	test_locate_refuses_bad_maps();
	test_padded_size();
	test_decode_into_pads_short_samples();
	test_decode_into_writes_nothing_past_length();
```

- [ ] **Step 2: Point the suite at `vm.c` and run it to confirm it fails**

In `saturn/tests/run_tests.sh`, change the `sfxconv` line to link `vm.c`:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_sfxconv test_sfxconv.c ../src/sfxconv.c ../src/vm.c
./run_tests_sfxconv
```

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — implicit declaration of `sfxconv_locate`, `sfxconv_padded_size`, `sfxconv_decode_into`.

- [ ] **Step 3: Declare the three functions in `sfxconv.h`**

Insert after the `sfxconv_decode_byte` declaration:

```c
/*----------------------
 | sfxconv_locate
 | Description: Walks the three indirections from the sample table to a
 |   sample's data, bounds-checking every one against MEMORY_SIZE, and reports
 |   where the bytes start and how many there are. Refuses rather than
 |   returning a wild offset.
 |
 |   src/sound.c does the same walk with no checks at all. It gets away with it
 |   because the host's map is a 512 KB file-scope static with the rest of .bss
 |   around it, so a garbage table entry reads nonsense and keeps running. On
 |   Saturn the map is an saturn_lwram_alloc block with the allocator's own
 |   bookkeeping beside it and get_byte is an unchecked memory[offset], so the
 |   same garbage walks off the end of the pool. Everything past the intro is
 |   unexercised, which is exactly where a table that has not been loaded yet,
 |   or an index the data does not cover, would first be reached.
 | Author: suinevere
 | Globals: N/A
 | Params: index -- zero-based sample index, the caller having already
 |   subtracted the script's one-based operand; out_offset -- receives the map
 |   offset of the first sample byte; out_length -- receives the byte count
 | Returns: 1 on success with both outputs written; 0 on refusal with both
 |   outputs left untouched
 ----------------------*/
int sfxconv_locate(int index, int *out_offset, int *out_length);

/*----------------------
 | sfxconv_padded_size
 | Description: The buffer size a sample of this length needs in order to be
 |   playable -- its own length, or SFXCONV_MIN_PLAYABLE if that is larger.
 |   Exists so the 0x900 constant appears at one call site rather than at every
 |   allocation and every memset.
 | Author: suinevere
 | Globals: N/A
 | Params: length -- decoded sample length in bytes
 | Returns: Bytes to allocate and to hand slPCMOn
 ----------------------*/
int sfxconv_padded_size(int length);

/*----------------------
 | sfxconv_decode_into
 | Description: Decodes length bytes from the emulated 68000 map at offset into
 |   dst, then zeroes dst[length .. dst_size). Zero is silence in signed 8-bit,
 |   so the padding is inaudible; it exists only because slPCMOn refuses
 |   anything shorter than SFXCONV_MIN_PLAYABLE.
 |
 |   Callers pass dst_size from sfxconv_padded_size and size dst to match. This
 |   function does not check that dst is that large -- it cannot, and neither
 |   can the host's equivalent loop.
 | Author: suinevere
 | Globals: N/A
 | Params: offset -- map offset of the first sample byte, from sfxconv_locate;
 |   length -- byte count, from sfxconv_locate; dst -- destination buffer;
 |   dst_size -- total bytes in dst, at least length
 | Returns: N/A
 ----------------------*/
void sfxconv_decode_into(int offset, int length, signed char *dst, int dst_size);
```

- [ ] **Step 4: Implement them in `sfxconv.c`**

Append:

```c
/*----------------------
 | in_map
 | Description: True when a half-open byte range [offset, offset + span) lies
 |   wholly inside the emulated 68000 map. Written as one predicate rather than
 |   inline comparisons because sfxconv_locate applies it four times and a
 |   sign slip in any one of them is the whole bug it exists to prevent.
 | Author: suinevere
 | Globals: N/A
 | Params: offset -- start of the range; span -- length of the range in bytes
 | Returns: 1 when the range fits, 0 otherwise
 ----------------------*/
static int in_map(long offset, long span)
{
	if (offset < 0 || span < 0)
	{
		return 0;
	}

	if (offset > (long)MEMORY_SIZE - span)
	{
		return 0;
	}

	return 1;
}

int sfxconv_locate(int index, int *out_offset, int *out_length)
{
	long table;
	long entry;
	long length;
	long data;

	if (index < 0 || out_offset == 0 || out_length == 0)
	{
		return 0;
	}

	table = (long)get_long(0xf90c);
	if (!in_map(table, (long)index * 4 + 4))
	{
		return 0;
	}

	entry = (long)get_long((int)(table + (long)index * 4));
	if (!in_map(entry, 8))
	{
		return 0;
	}

	length = (long)get_long((int)entry);
	data = entry + 8;
	if (length <= 0 || !in_map(data, length))
	{
		return 0;
	}

	*out_offset = (int)data;
	*out_length = (int)length;
	return 1;
}

int sfxconv_padded_size(int length)
{
	if (length < SFXCONV_MIN_PLAYABLE)
	{
		return SFXCONV_MIN_PLAYABLE;
	}

	return length;
}

void sfxconv_decode_into(int offset, int length, signed char *dst, int dst_size)
{
	int i;

	for (i = 0; i < length; i++)
	{
		dst[i] = g_decode[get_byte(offset + i)];
	}

	for (; i < dst_size; i++)
	{
		dst[i] = 0;
	}
}
```

`long` throughout `sfxconv_locate` because `get_long` returns `unsigned long` and the map offsets are `int`: assigning a garbage 0xFFFFFFFF straight into an `int` is implementation-defined and could land on a small negative that slips a naive `< MEMORY_SIZE` check. Widening first, then checking, then narrowing on the way out is what makes the guard real.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: every suite passes, including `test_sfxconv: all pass`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sfxconv.h saturn/src/sfxconv.c saturn/tests/test_sfxconv.c \
        saturn/tests/run_tests.sh
git commit -m "Bound the sample table walk against the map size and move the decode loop and its padding behind sfxconv, so no C++ translation unit has to reach into vm.h."
```

---

### Task 5: The Saturn backend

**Files:**
- Modify: `saturn/src/system/sound_srl.cxx` (replaces the Task 1 spike wholesale)

**Interfaces:**
- Consumes: `sfxconv_locate`, `sfxconv_padded_size`, `sfxconv_decode_into`, `SFXCONV_MIN_PLAYABLE` from Task 4; `saturn_lwram_alloc`/`saturn_lwram_free` from `saturn_compat.h`; `SRL::Sound::Pcm::{IPcmFile, StopSound}` from `srl_sound.hpp`
- Produces: working `play_sample` and `sound_flush_cache`. Nothing consumes these but `decode.c:1813` and `main.c:126`, which already call them.

**Before starting:** read Task 1's commit message. If the spike found LWRAM unusable, apply the alternative in Step 6 instead of the code below as written.

- [ ] **Step 1: Replace `sound_srl.cxx`**

```cpp
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

extern "C" {

/*----------------------
 | play_sample
 | Description: Plays a sound effect, converting and caching it on first use.
 |   Index 0 stops every channel, which is what src/sound.c's
 |   stop_all_channels does and the only way a script silences anything.
 |   Otherwise the index is one-based and is decremented into the cache.
 |
 |   Failure is silent at every step -- a refused location, a failed
 |   allocation, an out-of-range channel. src/sound.c is silent too on its one
 |   failure path, and on Saturn printf goes to SRL's debug text layer and
 |   would paint over the game.
 | Author: suinevere
 | Globals: g_sampleData, g_sampleSize
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
 | Author: suinevere
 | Globals: g_sampleData, g_sampleSize
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
}

}
```

- [ ] **Step 2: Build for SH-2**

Run: `cd saturn && ./compile.bat`
Expected: compiles and links clean.

Two failures are foreseeable and each has a specific cause:
- *Undefined reference to a mangled `sfxconv_*`* — `sfxconv.h`'s `extern "C"` guard is missing or misplaced. Fix the header.
- *`sfxconv.c` not compiled at all* — it must be directly under `saturn/src/` with a `.c` extension for `saturn/makefile:59`'s `find src/ -name '*.c'` to pick it up. No makefile edit should be needed; if one seems to be, the file is in the wrong place.

- [ ] **Step 3: Confirm the host build is untouched**

Run: `make -C saturn/src`
Expected: builds clean. `saturn/src/Makefile` uses an explicit `OBJS` list, so `sfxconv.c` is not built by the host and does not need to be — nothing on the host calls it.

- [ ] **Step 4: Run the unit tests**

Run: `sh saturn/tests/run_tests.sh`
Expected: all suites pass. Nothing in this task should have changed their behaviour; this is a regression check, not a new assertion.

- [ ] **Step 5: Record the `.bss` change**

Run: `grep -n '^\.bss' "saturn/BuildDrop/Heart of the Alien (USA).map"`

The map's basename tracks the disk name set at `saturn/makefile:38`; `shared.mk:189` derives it from the ELF.

Compare total `.bss` against the previous commit's build. Expected increase: 2,048 bytes for the two cache arrays, plus whatever `sfxconv.o` contributes (`g_decode` is `const`, so it belongs in `.rodata`, not `.bss` — if `.bss` grew by materially more than 2,048, find out why before moving on).

Put the measured number in the commit message. `disc_srl.cxx`'s banner sets the precedent for tracking this to the byte.

- [ ] **Step 6: Alternative — only if Task 1 found LWRAM unusable**

Change two things and nothing else:

Replace `saturn_lwram_alloc((unsigned long)padded)` with `malloc((size_t)padded)` and `saturn_lwram_free(...)` with `free(...)`, and add a running-total cap, because the HWRAM heap is 62,528 bytes and is shared with everything else on it:

```c
/*----------------------
 | SFX_HWRAM_BUDGET
 | Description: Ceiling on cached sample bytes, in force only because slPCMOn
 |   would not stream from LWRAM (see the design spec's risk 1) and the cache
 |   had to fall back to the HWRAM heap. That heap is 62,528 bytes in total and
 |   shared, so half of it is the most this may claim. Uncapped malloc here
 |   would starve every other allocation on the heap rather than dropping one
 |   sound.
 | Author: suinevere
 ----------------------*/
#define SFX_HWRAM_BUDGET 32768
```

with a `static unsigned long g_cached;` incremented on allocation, zeroed in `sound_flush_cache`, and checked before allocating:

```c
		if (g_cached + (unsigned long)padded > SFX_HWRAM_BUDGET)
		{
			return;
		}
```

Then update the file banner's memory paragraph to describe HWRAM and the cap rather than LWRAM and the remainder — a banner that describes the design that was abandoned is worse than no banner.

- [ ] **Step 7: Alternative — only if Task 2 measured a room within 16 KB of the budget**

Silent exhaustion is the spec's deliberate choice (design risk 3) and matches the host, whose own `malloc` failure path just returns. It stops being acceptable if the measurement shows a real room close to the ceiling, because then the first person to hit it gets a missing sound effect with nothing to go on.

If Task 2's worst room exceeded 65,916 bytes — the 81,916-byte budget less 16 KB of margin — add a one-shot warning on the first allocation failure, following the pattern `disc_srl.cxx` already uses for the small-`cdbuf` case: a `static int g_warned;` guarding a single `printf`, so a room that drops twenty samples paints one line rather than twenty.

```c
		if (buffer == 0)
		{
			if (!g_warned)
			{
				g_warned = 1;
				printf("SFX: lwram full at %d", index);
			}

			return;
		}
```

`printf` on Saturn goes to SRL's debug text layer and will sit on top of the game — which is the point, and why this is conditional on the measurement rather than always on. Add a banner for `g_warned` naming that trade-off.

If the worst room came in comfortably under, skip this step and say so in Task 7's Step 2.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/system/sound_srl.cxx
git commit -m "Give play_sample and sound_flush_cache real bodies over SRL::Sound::Pcm, caching each converted sample in LWRAM and stopping a channel before reusing it so a second sound interrupts the first the way the host does."
```

---

### Task 6: Hardware acceptance

**Files:**
- Modify: `saturn/makefile:62-67` (the stale comment on the `filter-out` line)

**Interfaces:**
- Consumes: the disc built by Task 5
- Produces: the observations Task 7 records

- [ ] **Step 1: Correct the makefile comment**

The comment above the `filter-out` still calls sound a later sub-project. It is not one any more.

Replace exactly this text:

```
# the glob above finds them and they have to be removed by name. Sound is a
# later sub-project; the pixel doublers have no Saturn equivalent at all.
```

with:

```
# the glob above finds them and they have to be removed by name. sound.c
# stays out because it is built on Mix_Chunk; src/system/sound_srl.cxx is the
# Saturn implementation of the same header. The pixel doublers have no Saturn
# equivalent at all.
```

Leave the `SOURCES := $(filter-out ...)` line below it and the two comment lines above it unchanged — `sound.c` is still filtered out, only the reason has changed.

- [ ] **Step 2: Build the disc**

Run: `cd saturn && ./compile.bat`
Expected: `saturn/BuildDrop/Heart of the Alien (USA).cue` is produced.

- [ ] **Step 3: Hand the disc to Suinevere with these five checks**

Do **not** launch the emulator from a tool call — Suinevere runs it.

1. **A sound effect is audible** at the first scripted SFX in play.
2. **Volume tracks the script** — quiet effects are quiet, loud ones loud. Not a measurement, a sanity check that `volume >> 1` is not landing on zero or on full for everything.
3. **A room transition with a sample playing** produces no noise burst, stall, or hang. This is the flush-ordering case and the streaming-starvation case (design risks 4) together.
4. **Music and SFX coexist** — a sample plays over CD-DA without either dropping out.
5. **Nothing sounds truncated by exactly half.** That is the tell for design risk 5, `length` being a word count rather than a byte count. If it appears, the fix is one multiply in `sfxconv_locate` and a new test row; do not guess at it before hearing it.

- [ ] **Step 4: Commit**

```bash
git add saturn/makefile
git commit -m "Retire the makefile comment calling sound a later sub-project, now that sound_srl.cxx implements the header it was deferring."
```

---

### Task 7: Fold the measurements back into the spec

**Files:**
- Modify: `docs/superpowers/specs/2026-08-07-hota-saturn-sfx-design.md`

**Interfaces:**
- Consumes: Task 1's spike answer, Task 2's per-room totals, Task 6's five observations
- Produces: a spec that records what was verified and what was not, matching the CD-DA and input specs

This is not bookkeeping. The CD-DA spec's filled-in Acceptance section and its "Known gaps left behind" are what let the 2026-08-06 handoff say what had been confirmed and what had merely been reasoned about. A spec whose risks are still phrased as open questions after the work has landed is worse than no spec, because it tells the next reader to re-investigate something already settled.

- [ ] **Step 1: Resolve risk 1 in place**

Under **Risks**, rewrite risk 1 from a question into its answer: whether `slPCMOn` streamed from LWRAM, how it was established (Task 1's spike, naming the commit), and — if the answer was no — that the design moved to the HWRAM fallback and where the cap lives.

- [ ] **Step 2: Resolve risk 2 in place**

Rewrite risk 2 with the measured per-room totals from Task 2, the worst room named, and that total set against the 81,916-byte budget. If any room came back with no samples because the 30-second timeout did not drive it far enough, say so rather than reporting zero.

Then settle risk 3 in the same pass: state whether Task 5's Step 7 warning was added and on what measured margin, or that it was skipped because the worst room came in comfortably under. "Silent by design" and "silent because nobody checked" read identically in six months unless this says which.

- [ ] **Step 3: Fill in Acceptance**

Mark each of the seven acceptance items met or not met, dated, in the form the CD-DA spec uses (`**Met 2026-08-05.**`). Anything Task 6 could not reach — a room never visited, an effect never triggered — is recorded as still unverified, named specifically, not quietly dropped.

- [ ] **Step 4: Add "Known gaps left behind" if any exist**

Only if Task 6 surfaced something judged non-blocking. Follow the CD-DA spec's section of the same name: what it is, why it was not fixed, and what fixing it would take.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-08-07-hota-saturn-sfx-design.md
git commit -m "Record the spike's answer, the measured sample sizes and what the emulator confirmed, so the sound-effects spec states what was verified rather than what was hoped."
```

---

## Self-review notes

**Spec coverage.** Every spec section maps to a task: `sfxconv_locate`/`decode_byte`/`padded_size`/`decode_into` → Tasks 3–4; the cache, `MemPcm`, `play_sample`, `sound_flush_cache` → Task 5; LWRAM-not-`malloc`, the `0x80` edge, padding-holds-the-channel, `vm.h`-stays-in-C → banners in Tasks 3–5; risks 1 and 2 → Tasks 1–2, resolved in Task 7; risks 3–5 → Task 6's checks 3–5 and Task 5's Step 6 cap; testing → Tasks 3–4; build and test → Steps in Tasks 3–5; acceptance → Tasks 6–7.

**Deliberately out of scope, per the spec:** `sound_init`/`sound_done` (nothing calls them, argued in Task 5's banner), panning, ducking, and any edit to `src/sound.c` beyond the probe Task 2 adds and Task 3 removes.
