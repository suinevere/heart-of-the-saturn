# bin/cue Disc Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the engine's `.iso` + `.mp3` data path with a single portable seam, `src/disc.h`, implemented on the host against the real Sega CD bin/cue set — and extract that set into the Saturn disc layout.

**Architecture:** `src/discfmt.{h,c}` holds pure disc-format logic (cue parse, ISO9660 walk, sector maths, track mapping) with no I/O and no SDL, so it is unit-testable on the host and shared verbatim by the runtime and the extractor. `src/host/disc_cue.c` holds the file handles and the `Mix_HookMusic` streamer. `src/cd_iso.*` and `src/music.*` are deleted.

**Tech Stack:** Host `gcc` + SDL2 + SDL2_mixer for the engine (`src/Makefile`), plain host `gcc` for unit tests (`saturn/tests/run_tests.sh`), SH-2 cross build via SaturnRingLib (`saturn/compile.bat`) for the disc, Mednafen for verification — run by Suinevere, never from a tool call.

**Spec:** `docs/superpowers/specs/2026-07-31-hota-bincue-disc-backend-design.md`

## Global Constraints

- **`git add -A` is forbidden in this repo.** `cd/` holds 425 MB of untracked, non-free game data. Task 1 ignores it, but until then — and as a habit after — stage only the specific files each task names.
- **`src/discfmt.{h,c}` must not include `<SDL.h>`, `<SDL_mixer.h>`, `<stdio.h>`, or any engine header.** It is compiled three times: into the engine, into the extractor, and by the host test runner. Its only dependencies are `<stdint.h>`, `<stddef.h>` and `<string.h>`. Anything needing a `FILE *` belongs in `src/host/disc_cue.c`.
- **`cue_track = engine_index + 2`.** Engine 0 → `TRACK 02`; engine 40 → `TRACK 42`. The old `music.c` used `track + 1` for a *filename over the mp3 rip*, which numbered audio tracks 01..41. Reading that as a cue-track formula aims engine index 0 at the **data track**. The constant exists in exactly one function.
- **MODE1/2352 user data is at `lba * 2352 + 16`, 2048 bytes per sector.** A multi-sector read is a per-sector loop, never one `fread`.
- **Do not touch `src/sound.c`, and do not remove `-lSDL2_mixer`.** SDL_mixer is retired as a *music file decoder* only; `sound.c` uses it for sound effects and `Mix_HookMusic` needs the device open. Deleting it silences the entire game while looking like a music-only change.
- **New C files use `.c`.** `saturn/makefile` derives objects with `$(SOURCES:.c=.o)` then `:.cxx=.o` and has pattern rules for only `%.c` and `%.cxx`; anything else drops out of the link with no error.
- **Follow the existing comment style.** Every new file and every non-obvious symbol gets a `/*---------------------- | Name | Description: ... | Author: suinevere ----------------------*/` banner whose Description carries the *reason* — the constraint, the bug avoided, the ordering requirement — not a restatement of the name.
- **Commit every task.**

## File Structure

| File | Responsibility |
|---|---|
| `.gitignore` | New. Non-free game data, build output, host test binary. |
| `src/discfmt.h` | New. Pure format interface: sector maths, ISO9660 walk, cue parse, track mapping. |
| `src/discfmt.c` | New. Its implementation. No I/O. |
| `src/disc.h` | New. **The seam.** Five functions the engine calls. |
| `src/host/disc_cue.c` | New. Host backend: file handles, validation, `Mix_HookMusic` streamer. |
| `tools/extract_disc.c` | New. Extracts blobs, tracks and `tracklist` into `saturn/cd/`. |
| `saturn/tests/test_discfmt.c` | New. Host unit tests. |
| `saturn/tests/run_tests.sh` | Replaced. Currently Another-Saturn's, and broken here. |
| `saturn/tests/test_scsp_voice.cxx`, `run_tests.exe` | Deleted. Another project's files. |
| `saturn/cd/music-off/tracklist` | New. Empty tracklist for `HOTA_AUDIO = none`. |
| `src/cd_iso.c`, `src/cd_iso.h` | Deleted (Task 4). |
| `src/music.c`, `src/music.h` | Deleted (Task 5). |
| `src/main.c` | Modified across Tasks 4, 5, 6. |
| `src/animation.c`, `src/game2bin.c`, `src/decode.c` | Modified. Call-site renames. |
| `src/client.h` | Modified (Task 6). `use_iso`, `iso_prefix` removed. |
| `src/Makefile` | Modified (Tasks 4, 5). |
| `saturn/makefile` | Modified (Task 8). `CD_NAME`, `HOTA_AUDIO`. |

The `discfmt` (pure) / `disc_cue` (I/O) split is the one structural refinement over a
literal reading of the spec's five-function seam. It exists because sector arithmetic,
an ISO9660 record walk and a track off-by-one all fail *plausibly* — producing data
that decodes far enough to look like someone else's bug — and all three are testable in
milliseconds once no file handle is involved.

---

### Task 1: Ignore the non-free data before anything else can commit it

No code. This is first because `cd/` is 425 MB of copyrighted game data sitting
untracked in the working tree, and every later task ends in a commit.

**Files:**
- Create: `.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: a clean `git status`, which every later task's verification depends on.

- [ ] **Step 1: Confirm what is currently untracked**

```bash
cd /c/Users/saggl/CLionProjects/heart-of-the-saturn
git status --short
```
Expected: `?? cd/`, `?? saturn/`, `?? tools/assets/` — and `A .gitmodules`, `A SaturnRingLib` already staged.

- [ ] **Step 2: Write the root `.gitignore`**

Create `.gitignore`. Modelled on Another-Saturn's, with the rationale kept in comments
because a bare list of paths does not explain why a file is forbidden rather than
merely absent:

```gitignore
# --- Host build output -------------------------------------------------------
src/*.o
src/alien
src/alien.exe

# --- Saturn build output -----------------------------------------------------
saturn/BuildDrop/
**/*.o
**/*.elf
**/*.iso
**/*.bin.tmp

# --- Cross-compiler and iso2raw (fetched into the SDK submodule, never committed)
SaturnRingLib/Compiler/
SaturnRingLib/tools/bin/

# --- Host/IDE build dirs -----------------------------------------------------
# .idea/ is deliberately NOT here: Another-Saturn commits its CLion project
# files and this repo follows that blueprint.
cmake-build-*/

# --- Host unit-test binary ---------------------------------------------------
# saturn/tests/run_tests.sh builds this with the host gcc, not the SH-2 cross
# compiler, so the pure disc-format logic can be tested off-target. Build
# output, not source. Both spellings: MSYS gcc emits .exe on Windows.
saturn/tests/run_tests
saturn/tests/run_tests.exe

# --- Extractor binary --------------------------------------------------------
tools/extract_disc
tools/extract_disc.exe

# --- Non-free game data ------------------------------------------------------
# The source Sega CD rip: 42 tracks, 425 MB, copyrighted. Required to build a
# disc, never committed. Listed as a bare directory because the track filenames
# contain spaces and parentheses that are awkward to match individually.
cd/

# Everything extracted from it is the same data in a different shape.
# The 19 engine blobs. Both cases: ISO9660 stores them uppercase, but an
# extractor or a case-insensitive copy can produce either, and git's ignore
# matching is case-sensitive wherever core.ignorecase is false (e.g. CI on Linux).
saturn/cd/data/*.BIN
saturn/cd/data/*.bin

# The 41 CD-DA tracks and sox's raw intermediates, ~828 MB with HOTA_AUDIO=full.
# cd/music/tracklist is NOT ignored: it is the track ordering, it contains no
# game data, and without it shared.mk falls back to `find` order and lays the
# music onto the disc in an arbitrary sequence.
saturn/cd/music/*.wav
saturn/cd/music/*.raw

# SGL sound driver files the makefile copies in when SRL_USE_SGL_SOUND_DRIVER=1,
# and the linked program objcopy'd into the image. Build output, not source.
saturn/cd/data/0.bin
saturn/cd/data/SDDRVS.DAT
saturn/cd/data/SDDRVS.TSK
saturn/cd/data/BOOTSND.MAP
```

> **Note the ordering trap:** `saturn/cd/data/*.bin` (lowercase) already covers
> `0.bin`, so the later explicit entries are redundant but harmless. They are kept
> because they document *why* those four are build output, which the glob does not.

- [ ] **Step 3: Verify the big data is now invisible**

```bash
git status --short
git check-ignore -v "cd/Heart of the Alien - Out of This World Parts I and II (USA) (RE).cue"
```
Expected: `cd/` no longer appears in `git status`; `check-ignore` reports the `cd/` rule.

- [ ] **Step 4: Commit**

```bash
git add .gitignore
git commit -m "Ignore the non-free rip and every build output before anything commits them

cd/ is 425 MB of copyrighted Sega CD data and every task after this one ends
in a commit. The entries carry their reasons because a path list does not
explain why cd/music/tracklist is tracked while everything beside it is not:
it is the track ordering, and without it shared.mk falls back to find order
and lays the music onto the disc in an arbitrary sequence."
```

---

### Task 2: Host test harness and the pure arithmetic

The three functions here are one-liners whose failure modes are expensive: a wrong
sector offset corrupts every read past the first 2 KB, and a wrong track constant plays
the wrong music for the entire game. They go under test before anything calls them.

**Files:**
- Create: `src/discfmt.h`
- Create: `src/discfmt.c`
- Create: `saturn/tests/test_discfmt.c`
- Replace: `saturn/tests/run_tests.sh`
- Delete: `saturn/tests/test_scsp_voice.cxx`, `saturn/tests/run_tests.exe`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `uint32_t discfmt_mode1_user_offset(uint32_t lba)`
  - `uint32_t discfmt_sector_span(uint32_t size)`
  - `int discfmt_iso_name_eq(const char *iso_name, uint8_t iso_len, const char *want)`
  - `int discfmt_cue_track_for_music(int engine_index)`
  - `saturn/tests/run_tests.sh` — the command every later task re-runs.

- [ ] **Step 1: Clear out Another-Saturn's test scaffolding**

`saturn/` was copied from Another-Saturn, so `saturn/tests/` holds that project's SCSP
tests and a stale binary. `run_tests.sh` there builds `../src/system/scsp_voice.cxx`,
which does not exist in this repo — the runner cannot work until it is replaced.

```bash
cd /c/Users/saggl/CLionProjects/heart-of-the-saturn
rm -f saturn/tests/test_scsp_voice.cxx saturn/tests/run_tests.exe
```

- [ ] **Step 2: Write the test runner**

Replace `saturn/tests/run_tests.sh`:

```sh
#!/bin/sh
# Host unit tests for the pure disc-format logic. Nothing here opens the disc,
# links SDL, or needs the SH-2 toolchain -- that is the point. Sector
# arithmetic, the ISO9660 record walk and the music track mapping all fail
# plausibly rather than loudly, producing data that decodes far enough to look
# like a decoder bug, so they are checked here in milliseconds instead of by
# playing the game.
set -e
cd "$(dirname "$0")"
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../../src \
    -o run_tests test_discfmt.c ../../src/discfmt.c
./run_tests
```

Make it executable: `chmod +x saturn/tests/run_tests.sh`

- [ ] **Step 3: Write the failing test**

Create `saturn/tests/test_discfmt.c`:

```c
/*----------------------
 | test_discfmt.c
 | Description: Host unit tests for discfmt.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: discfmt.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "discfmt.h"

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

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static void test_mode1_user_offset(void)
{
    /* 12 bytes of sync plus a 4-byte header precede the 2048 bytes of user
       data in every MODE1/2352 sector. Sector 0 is the Sega CD header. */
    CHECK_EQ(discfmt_mode1_user_offset(0),  16);

    /* The ISO9660 Primary Volume Descriptor. */
    CHECK_EQ(discfmt_mode1_user_offset(16), 16 * 2352 + 16);

    /* The root directory of this disc. */
    CHECK_EQ(discfmt_mode1_user_offset(20), 20 * 2352 + 16);

    /* END1.BIN, the first entry of the validation manifest. */
    CHECK_EQ(discfmt_mode1_user_offset(2593), 2593 * 2352 + 16);

    /* The last sector of the data track: 5132 sectors total. Computed in 32
       bits without overflow -- 5131 * 2352 is ~12 M, comfortably inside. */
    CHECK_EQ(discfmt_mode1_user_offset(5131), 5131 * 2352 + 16);
}

static void test_sector_span(void)
{
    /* Reads are whole sectors even when the file is not a multiple of one.
       Getting this wrong by a sector truncates the tail of every odd-sized
       file -- of which ROOMS7.BIN is the one that matters. */
    CHECK_EQ(discfmt_sector_span(0),      0);
    CHECK_EQ(discfmt_sector_span(1),      1);
    CHECK_EQ(discfmt_sector_span(2047),   1);
    CHECK_EQ(discfmt_sector_span(2048),   1);
    CHECK_EQ(discfmt_sector_span(2049),   2);

    /* GAME2.BIN: 409600 bytes, exactly 200 sectors. */
    CHECK_EQ(discfmt_sector_span(409600), 200);

    /* ROOMS7.BIN: 160826 bytes, NOT sector-aligned -- 78.5 sectors. */
    CHECK_EQ(discfmt_sector_span(160826), 79);

    /* MAKE2MB.BIN, the largest blob: 436224 bytes. */
    CHECK_EQ(discfmt_sector_span(436224), 213);
}

static void test_iso_name_eq(void)
{
    /* ISO9660 stores a version suffix. A plain strcmp against "END1.BIN"
       matches nothing on this disc, which presents as "every file missing". */
    CHECK(discfmt_iso_name_eq("END1.BIN;1", 10, "END1.BIN"));

    /* Some masterings omit the suffix. Both must work. */
    CHECK(discfmt_iso_name_eq("END1.BIN", 8, "END1.BIN"));

    /* Case-insensitive: the engine asks in uppercase and so does this disc,
       but nothing guarantees that of a remaster. */
    CHECK(discfmt_iso_name_eq("end1.bin;1", 10, "END1.BIN"));

    /* A shared prefix must NOT match. This is the check that stops a lookup
       for ROOMS1.BIN silently returning ROOMS11.BIN on a disc that has one. */
    CHECK(!discfmt_iso_name_eq("ROOMS11.BIN;1", 13, "ROOMS1.BIN"));
    CHECK(!discfmt_iso_name_eq("ROOMS1.BIN;1", 12, "ROOMS11.BIN"));

    /* Different files entirely. */
    CHECK(!discfmt_iso_name_eq("END2.BIN;1", 10, "END1.BIN"));

    /* The record length is authoritative, not a NUL: ISO9660 names are not
       NUL-terminated in the record. Passing a short length must truncate. */
    CHECK(!discfmt_iso_name_eq("END1.BINXX", 4, "END1.BIN"));
}

static void test_cue_track_for_music(void)
{
    /* The disc has 41 audio tracks, TRACK 02 through TRACK 42, and the engine
       indexes music 0..40. 41 onto 41, in order. */
    CHECK_EQ(discfmt_cue_track_for_music(0),  2);
    CHECK_EQ(discfmt_cue_track_for_music(1),  3);
    CHECK_EQ(discfmt_cue_track_for_music(31), 33);  /* INTRO1.BIN's track */
    CHECK_EQ(discfmt_cue_track_for_music(35), 37);  /* MAKE2MB.BIN's track */
    CHECK_EQ(discfmt_cue_track_for_music(40), 42);  /* END4.BIN's track */

    /* The whole point. The deleted music.c built its filename as track + 1,
       because the mp3 rip numbered the 41 AUDIO tracks 01..41 -- audio track 1
       is disc track 2. Reading that as a cue-track formula aims engine index 0
       at TRACK 01, which is the DATA track: noise or a hang on hardware, and
       often silence in an emulator. Nothing may ever return 1. */
    for (int i = 0; i <= 40; i++) {
        CHECK(discfmt_cue_track_for_music(i) >= 2);
        CHECK(discfmt_cue_track_for_music(i) <= 42);
    }

    /* Out of range is refused rather than clamped: a bytecode operand that
       lands here is a bug worth seeing, not one worth papering over. */
    CHECK_EQ(discfmt_cue_track_for_music(-1), 0);
    CHECK_EQ(discfmt_cue_track_for_music(41), 0);
}

int main(void)
{
    test_mode1_user_offset();
    test_sector_span();
    test_iso_name_eq();
    test_cue_track_for_music();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }

    printf("%d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `discfmt.h: No such file or directory`

- [ ] **Step 5: Write the header**

Create `src/discfmt.h` with the four declarations. The banner must carry the
constraint that keeps this file portable:

```c
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
```

Each function gets its own banner. The two that carry real rationale:

```c
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
```

- [ ] **Step 6: Write the implementation**

Create `src/discfmt.c`. The four functions are small; the only subtleties are that
`discfmt_iso_name_eq` must stop at `;` *and* respect `iso_len` (ISO9660 names are not
NUL-terminated inside the record), and that `discfmt_cue_track_for_music` returns 0
rather than clamping.

```c
#define DISCFMT_RAW_SECTOR   2352
#define DISCFMT_USER_SECTOR  2048
#define DISCFMT_SYNC_HEADER  16

#define DISCFMT_MUSIC_FIRST_TRACK 2   /* TRACK 01 is data */
#define DISCFMT_MUSIC_MAX_INDEX   40  /* 41 audio tracks, 02..42 */

uint32_t discfmt_mode1_user_offset(uint32_t lba)
{
    return lba * (uint32_t)DISCFMT_RAW_SECTOR + DISCFMT_SYNC_HEADER;
}

uint32_t discfmt_sector_span(uint32_t size)
{
    return (size + (DISCFMT_USER_SECTOR - 1u)) / DISCFMT_USER_SECTOR;
}

int discfmt_cue_track_for_music(int engine_index)
{
    if (engine_index < 0 || engine_index > DISCFMT_MUSIC_MAX_INDEX)
    {
        return 0;
    }

    return engine_index + DISCFMT_MUSIC_FIRST_TRACK;
}
```

`discfmt_iso_name_eq` compares `want` against `iso_name` for at most `iso_len` bytes,
stopping early at `';'`, folding case with an explicit A–Z fold rather than `tolower`
(which is locale-dependent and drags in `<ctype.h>`), and requiring **both** sides to
end together so a shared prefix does not match.

- [ ] **Step 7: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — `all tests passed`

- [ ] **Step 8: Commit**

```bash
git add src/discfmt.h src/discfmt.c saturn/tests/test_discfmt.c saturn/tests/run_tests.sh
git rm --cached -q saturn/tests/test_scsp_voice.cxx 2>/dev/null || true
git commit -m "Pure disc-format arithmetic, under test before anything calls it

Three one-liners whose failure modes are all quiet. A wrong sector offset
gives data that is right for 2048 bytes and garbage after, which reads as a
decoder bug. A wrong track constant plays the wrong music for the whole game.

The track mapping is +2, not the +1 the old music.c used: that +1 numbered
the mp3 rip, whose track 01 is the disc's track 02. Carried over literally it
aims engine index 0 at the data track.

saturn/tests/ came from Another-Saturn and its runner built a source file
this repo does not have, so it is replaced rather than extended."
```

---

### Task 3: Cue parsing and the ISO9660 directory walk

**Files:**
- Modify: `src/discfmt.h`, `src/discfmt.c`
- Modify: `saturn/tests/test_discfmt.c`

**Interfaces:**
- Consumes: Task 2's `discfmt.h`.
- Produces:
  - `typedef struct { int number; int is_audio; char filename[256]; } DiscCueTrack;`
  - `typedef struct { int count; DiscCueTrack tracks[DISCFMT_MAX_TRACKS]; } DiscCue;`
  - `int discfmt_cue_parse(const char *text, size_t len, DiscCue *out)`
  - `int discfmt_iso_root(const uint8_t *pvd_user, uint32_t *lba, uint32_t *len)`
  - `int discfmt_iso_find(const uint8_t *dir, uint32_t dir_len, const char *name, uint32_t *lba, uint32_t *size)`

- [ ] **Step 1: Write the failing tests**

Append to `saturn/tests/test_discfmt.c`. Build the ISO9660 fixtures in code rather than
committing a binary — the records are small and an explicit builder documents the
format better than a blob would.

Cover:

- **`discfmt_cue_parse` on a synthesised multi-file cue**: 42 tracks, track 1 not audio,
  tracks 2–42 audio, filenames preserved **including spaces and parentheses** (every
  filename in this set has both, and a naive whitespace tokeniser loses them — the
  quoted string must be taken whole).
- **Single-file cue rejection**: two `TRACK` entries under one `FILE`. Must return a
  distinct failure, not a mis-parse. Silently accepting it yields 42 tracks all
  pointing at the same file at offset zero.
- **`discfmt_iso_root`**: a synthesised 2048-byte PVD with a root record at byte 156;
  confirm LBA 20 and length 4096 come back.
- **`discfmt_iso_find`** over a synthesised directory:
  - finds a record by name;
  - returns its LBA and size;
  - **crosses a record-length-0 pad correctly.** A zero length byte means "skip to the
    next 2048-byte boundary", not "end of directory". A walk that stops there misses
    every file in the second block — and this disc's root directory is 4096 bytes, i.e.
    two blocks, so that is not a hypothetical;
  - **does not run past `dir_len`** on a truncated or malformed directory;
  - returns not-found for an absent name rather than garbage.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `'DiscCue' undeclared` or similar.

- [ ] **Step 3: Implement**

Notes that will otherwise cost time:

- The cue's `FILE "name" BINARY` line: take everything between the first and last
  double-quote on the line. Do not tokenise on spaces.
- A `TRACK nn MODE1/2352` line sets `is_audio = 0`; `TRACK nn AUDIO` sets it to 1.
  Track numbers in a cue are decimal and zero-padded.
- Single-file detection: a second `TRACK` encountered with no intervening `FILE`.
- ISO9660 directory record fields, all at fixed offsets from the record start:
  `[0]` record length, `[2..5]` LBA little-endian, `[10..13]` size little-endian,
  `[32]` name length, `[33..]` name. (Both LBA and size are stored twice, LE then BE;
  read the LE copy.)
- The PVD's root directory record is 34 bytes at offset 156 of the 2048-byte user area,
  in the same record format.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — `all tests passed`

- [ ] **Step 5: Commit**

```bash
git add src/discfmt.h src/discfmt.c saturn/tests/test_discfmt.c
git commit -m "Cue parsing and the ISO9660 directory walk

The hardcoded offset table in cd_iso.c was only ever a hand-copied snapshot
of this directory -- all 19 entries match it exactly, LBA and size -- so
reading the real thing replaces the table with what the table was copied
from, and gives name-based lookup, which is the shape SRL::Cd::File offers.

Two traps have tests of their own: a record-length-0 byte means skip to the
next 2048-byte boundary rather than end-of-directory, and this disc's root
spans two blocks, so a walk that stops there loses half the files. And cue
filenames here all contain spaces and parentheses, so the quoted string is
taken whole rather than tokenised."
```

---

### Task 4: The seam, and the host data backend

The host build changes data source in this task. Music is untouched and still runs off
`music.c` — `cls.iso_prefix` stays alive until Task 6 precisely so this window stays
narrow and the build never breaks.

**Files:**
- Create: `src/disc.h`
- Create: `src/host/disc_cue.c`
- Modify: `src/main.c`, `src/animation.c`, `src/game2bin.c`, `src/Makefile`
- Delete: `src/cd_iso.c`, `src/cd_iso.h`

**Interfaces:**
- Consumes: `discfmt.h` from Tasks 2–3.
- Produces: `disc_open`, `disc_read_file`, `disc_close` (the music pair is stubbed here and implemented in Task 5).

- [ ] **Step 1: Write `src/disc.h`**

The five-function seam, with a banner explaining that it is the single platform
boundary and that a Saturn implementation is a sibling of `disc_cue.c`.

`disc_read_file` takes `max_size`. That parameter is the reason the signature differs
from the old `read_file`, and its banner must say so: the three call sites pass raw
pointers into the emulated 68000 address space with no bounds checking anywhere, so
without it a wrong dump scribbles past a buffer instead of failing.

- [ ] **Step 2: Write `src/host/disc_cue.c`**

`disc_open`:
1. Read the cue into memory, `discfmt_cue_parse`.
2. Resolve track filenames relative to the cue's own directory, not the process CWD —
   the engine is normally launched from elsewhere.
3. `fopen` the data track, keep it open for the process lifetime.
4. Read sector 16 → `discfmt_iso_root`.
5. Read the root directory (2 sectors here; use the returned length) into a cached buffer.
6. Run the validation pass over the 19-entry manifest.

The manifest and the validation rule, verbatim from the spec — **size mismatch or
missing file is fatal; LBA mismatch is a warning**. Size is what protects the buffers;
LBA is a fingerprint of this particular dump, and a differently-mastered but playable
disc shifts every LBA while every size stays identical.

`disc_read_file(name, out, max_size)`:
1. `discfmt_iso_find` in the cached directory.
2. If `size > max_size`, fail loudly naming both numbers.
3. Loop `discfmt_sector_span(size)` times: `fseek(discfmt_mode1_user_offset(lba + i))`,
   `fread` 2048 (or the remainder on the last sector).

- [ ] **Step 3: Switch the three call sites**

| File | Was | Becomes |
|---|---|---|
| `src/main.c:117` | `read_file(filename, ptr)` | `disc_read_file(filename, ptr, <space at 0xf900>)` |
| `src/animation.c:871` | `read_file(filename, ptr)` | `disc_read_file(filename, ptr, <space at read_offset>)` |
| `src/game2bin.c:56` | `read_file("GAME2.BIN", game2bin)` | `disc_read_file("GAME2.BIN", game2bin, GAME2BIN_SIZE)` |

`game2bin` is exact — `GAME2BIN_SIZE` is 409600 and so is the file. The two
`get_memory_ptr` sites bound against the emulated map: `memory` is
`512*1024*2` = 1,048,576 bytes (`src/vm.c:26`), so the space available from offset `N`
is `1048576 - N`. Compute it from the array size, do not hardcode a number.

- [ ] **Step 4: Wire `disc_open`/`disc_close` into `main.c`**

`disc_open` must run **before `game2bin_init()`** (`main.c:170`), which is the first
thing that reads from the disc. Take the cue path from `argv` after option parsing;
if none is given, fall back to searching `cd/*.cue`. `disc_close` goes in
`atexit_callback` (`main.c:132`).

- [ ] **Step 5: Delete `cd_iso` and update the Makefile**

```bash
git rm src/cd_iso.c src/cd_iso.h
```
In `src/Makefile`: drop `cd_iso.o`, add `discfmt.o` and `disc_cue.o`, and add
`-Isrc/host` handling for the new subdirectory. Remove the `#include "cd_iso.h"` lines
from `src/main.c`, `src/animation.c`, `src/game2bin.c` (and `src/music.c`, which
includes it too — but leave the rest of `music.c` alone until Task 5).

- [ ] **Step 6: Build and verify**

```bash
cd src && make clean && make
```
Expected: builds clean, no reference to `cd_iso` remains:
```bash
grep -rn "cd_iso\|read_file(" src/
```
Expected: no matches (`disc_read_file` does not match `read_file(` because of the
leading `disc_`; if your grep disagrees, anchor it with `\bread_file(`).

- [ ] **Step 7: Run it**

```bash
cd src && ./alien "../cd/Heart of the Alien - Out of This World Parts I and II (USA) (RE).cue"
```
Expected: the game loads and reaches the intro. Data now comes from the bin/cue set.
Music will not play (mp3s are absent) — that is Task 5, and it must not crash.

- [ ] **Step 8: Commit**

```bash
git add src/disc.h src/host/disc_cue.c src/main.c src/animation.c src/game2bin.c src/Makefile
git commit -m "One disc seam, and the host bin/cue backend behind it

disc.h is the single platform boundary: a Saturn backend is a sibling of
disc_cue.c over SRL::Cd::File, with no engine change above it.

disc_read_file gained max_size because the three call sites pass raw pointers
into the emulated 68000 address space and nothing anywhere compares a file's
size against the room at the destination. A truncated or wrong-region dump
did not fail, it scribbled into unrelated VM state and surfaced as a
corrupted room minutes later. Startup validation catches the same class
earlier: size mismatch is fatal, LBA mismatch only warns, because a
differently-mastered but playable disc shifts every LBA and no size."
```

---

### Task 5: Music over `Mix_HookMusic`

**Files:**
- Modify: `src/host/disc_cue.c`, `src/decode.c`, `src/main.c`, `src/Makefile`
- Delete: `src/music.c`, `src/music.h`

**Interfaces:**
- Consumes: `discfmt_cue_track_for_music` from Task 2, the cue track table from Task 4.
- Produces: `disc_play_track`, `disc_stop_track`.

- [ ] **Step 1: Implement the streamer in `disc_cue.c`**

The audio tracks are raw CD-DA — 44,100 Hz, 16-bit signed, stereo, little-endian, no
header — and `main.c:149` already opens the device as
`Mix_OpenAudio(44100, AUDIO_S16, 2, 4096)`, which is that format exactly. So the
callback is a buffered `fread` and a `memcpy`, with no decode and no conversion.

`disc_play_track(engine_index, loop)`:
1. `discfmt_cue_track_for_music(engine_index)`; a 0 return is out of range — log and
   return, do not play.
2. **`Mix_HookMusic(NULL, NULL)` first**, then `fclose` the previous track.
   In that order, with no path that skips it: the callback runs on the audio thread,
   and closing the handle under it is a use-after-free that fires rarely and
   confusingly.
3. `fopen` the new track file; `Mix_HookMusic(stream_cb, state)`.

`disc_stop_track` is step 2 alone.

The callback fills the requested byte count from the file; at EOF it either seeks back
to zero (`loop != 0`) or zero-fills the remainder and unhooks (`loop == 0`).

- [ ] **Step 2: Switch the call sites**

| Was | Becomes | Sites |
|---|---|---|
| `play_music_track(t, l)` | `disc_play_track(t, l)` | `decode.c:1846,1848,1856,1858,1860,1862,1893,1900`; `main.c:708` |
| `stop_music()` | `disc_stop_track()` | `decode.c:1850,1885`; `main.c:134,724` |
| `music_update()` | *(deleted)* | `main.c:830` |
| `music_init()` | *(deleted)* | `main.c:161` |

`music_update` was empty and `music_init` was empty; both go away rather than becoming
no-op wrappers. Replace `#include "music.h"` with `#include "disc.h"`.

- [ ] **Step 3: Delete `music.c` and update the Makefile**

```bash
git rm src/music.c src/music.h
```
Drop `music.o` from `src/Makefile`. **Leave `-lSDL2_mixer`** — `src/sound.c` needs it
for sound effects and `Mix_HookMusic` needs the device open.

- [ ] **Step 4: Build and verify**

```bash
cd src && make clean && make
grep -rn "Mix_LoadMUS\|Mix_PlayMusic\|Mix_FreeMusic\|music_init\|music_update" src/
```
Expected: builds clean; no matches.

Confirm sound effects survived:
```bash
grep -c "Mix_" src/sound.c
```
Expected: non-zero — `sound.c` is untouched.

- [ ] **Step 5: Listen**

```bash
cd src && ./alien "../cd/Heart of the Alien - Out of This World Parts I and II (USA) (RE).cue"
```
Check, in order:
1. Music plays at all during the intro.
2. It is the *right* music — the intro sequence uses engine indices 31–34
   (`anm_files`, `main.c:57-60`), i.e. cue tracks 33–36. If the music is recognisable
   but wrong, suspect the mapping before suspecting the streamer.
3. Sound effects still play. If they stopped, SDL_mixer was over-removed.
4. Track changes between animations are clean, with no crash — that is the
   `Mix_HookMusic(NULL,NULL)`-before-`fclose` ordering being exercised.

- [ ] **Step 6: Commit**

```bash
git add src/host/disc_cue.c src/decode.c src/main.c src/Makefile
git commit -m "Stream CD-DA straight off the disc with Mix_HookMusic

The tracks are raw 44100/16/stereo and Mix_OpenAudio already opens the device
in exactly that format, so the callback is an fread and a memcpy -- no
decode, no resample. Mix_LoadWAV_RW over a synthesised header would have
pulled whole tracks into RAM, and Track 03 alone is 29 MB.

Mix_HookMusic(NULL, NULL) always precedes fclose. The callback runs on the
audio thread and closing the handle under it is a use-after-free that fires
rarely enough to survive casual testing.

SDL_mixer stays: sound.c uses it for effects, and this needs the device open."
```

---

### Task 6: Retire `--iso`, `--iso-prefix`, `use_iso` and `iso_prefix`

Small, mechanical, and its own task so that the deletion is visible in history rather
than buried in Task 4's diff.

**Files:**
- Modify: `src/client.h`, `src/main.c`

- [ ] **Step 1: Remove the fields**

`src/client.h:28` `int use_iso;` and `:33` `char *iso_prefix;`.

- [ ] **Step 2: Remove the options**

In `src/main.c`:
- `:949` the `--iso` help line. Replace the whole usage block's implied "files in the
  current directory" model with a line naming the cue argument.
- `:977` `{"iso", no_argument, 0, 'i'}` and `:979` `{"iso-prefix", required_argument, 0, 'e'}`.
- `:992` `cls.use_iso = 0;` and `:996` `cls.iso_prefix = NULL;`.
- `:1043` `case 'i':` and `:1051` `case 'e':`.

The short-option string at `:1001` is `"hdr:23s:"` and never contained `i` or `e`, so
it needs no change — worth checking rather than assuming.

- [ ] **Step 3: Verify**

```bash
cd src && make clean && make
grep -rn "use_iso\|iso_prefix\|\-\-iso" src/
```
Expected: builds clean; no matches.

```bash
cd src && ./alien --help
```
Expected: no `--iso` or `--iso-prefix` line; usage names the `.cue` argument.

- [ ] **Step 4: Commit**

```bash
git add src/client.h src/main.c
git commit -m "Drop --iso, --iso-prefix and the client fields behind them

bin/cue is the only source now. Removed as its own commit so the deletion is
findable in history rather than folded into the backend that replaced it."
```

---

### Task 7: The extractor

**Files:**
- Create: `tools/extract_disc.c`

**Interfaces:**
- Consumes: `src/discfmt.{h,c}` — linked directly, so the extractor and the runtime cannot disagree about the disc layout.
- Produces: `saturn/cd/data/*.BIN`, `saturn/cd/music/trackNN.wav`, `saturn/cd/music/tracklist`.

- [ ] **Step 1: Write it**

```
extract_disc <path-to.cue> <saturn-cd-dir>
```

Data: for each of the 19 manifest names, `discfmt_iso_find` then the same per-sector
read as the runtime, writing `<dir>/data/<NAME>`. Sizes must come out exactly equal to
the manifest.

Music: for each audio track, write `<dir>/music/track<NN>.wav` where `NN` is the **cue
track number** (02..42), not the engine index. A canonical 44-byte RIFF/WAVE header
followed by the track file's bytes, unmodified:

| Offset | Bytes | Value |
|---|---|---|
| 0 | 4 | `"RIFF"` |
| 4 | 4 | `36 + data_size` (LE) |
| 8 | 8 | `"WAVEfmt "` |
| 16 | 4 | `16` (LE) — PCM fmt chunk size |
| 20 | 2 | `1` (LE) — PCM |
| 22 | 2 | `2` (LE) — stereo |
| 24 | 4 | `44100` (LE) |
| 28 | 4 | `176400` (LE) — byte rate |
| 32 | 2 | `4` (LE) — block align |
| 34 | 2 | `16` (LE) — bits |
| 36 | 4 | `"data"` |
| 40 | 4 | `data_size` (LE) |

`.wav` rather than raw because `shared.mk:409` auto-discovers only
`*.mp3|wav|ogg|flac|aac|m4a|wma` — a bare `.raw` is invisible to it. sox's conversion
back to raw is then a byte-identical round trip, since the source already is
44.1/16/stereo.

Tracklist: write `<dir>/music/tracklist`, one filename per line in cue order, with a
leading comment explaining that it exists to pin the order.

- [ ] **Step 2: Build and run it**

```bash
gcc -std=c99 -Wall -Wextra -O2 -Isrc -o tools/extract_disc tools/extract_disc.c src/discfmt.c
./tools/extract_disc "cd/Heart of the Alien - Out of This World Parts I and II (USA) (RE).cue" saturn/cd
```

- [ ] **Step 3: Verify the output**

```bash
ls saturn/cd/data/*.BIN | wc -l          # expect 19
du -sh saturn/cd/data                    # expect ~7.2 MB
ls saturn/cd/music/*.wav | wc -l         # expect 41
wc -l < saturn/cd/music/tracklist        # expect 41 (+1 if the comment line counts)
head -3 saturn/cd/music/tracklist        # expect track02.wav first
```

Check three sizes against the manifest exactly:
```bash
stat -c%s saturn/cd/data/MAKE2MB.BIN     # expect 436224
stat -c%s saturn/cd/data/GAME2.BIN       # expect 409600
stat -c%s saturn/cd/data/ROOMS7.BIN      # expect 160826  (the non-sector-aligned one)
```

`ROOMS7.BIN` is the one that matters: it is the only blob whose size is not a multiple
of 2048, so it is the only one that proves the last-sector remainder is handled.

- [ ] **Step 4: Confirm nothing extracted is about to be committed**

```bash
git status --short
```
Expected: only `tools/extract_disc.c` is untracked. If any `.BIN` or `.wav` appears,
Task 1's `.gitignore` is wrong — fix it before committing.

- [ ] **Step 5: Commit**

```bash
git add tools/extract_disc.c
git commit -m "Extract the 19 blobs and 41 CD-DA tracks into the Saturn disc layout

Links src/discfmt.c directly rather than reimplementing the sector maths, so
the extractor and the runtime cannot drift about what the disc contains.

Tracks are written as .wav because shared.mk auto-discovers only mp3/wav/ogg/
flac/aac/m4a/wma -- a bare .raw is invisible to it. The header is 44 bytes in
front of the untouched bytes, and sox's conversion back to raw is a
byte-identical round trip because the source already is 44.1/16/stereo.

The tracklist is not optional: without it shared.mk falls back to find order,
which is undefined, and every song ends up on the wrong track. That failure
is obvious to a listener and invisible in a build log."
```

---

### Task 8: The `HOTA_AUDIO` build knob

**Files:**
- Modify: `saturn/makefile`
- Create: `saturn/cd/music-off/tracklist`

- [ ] **Step 1: Fix `CD_NAME`**

`saturn/makefile` was copied from Another-Saturn and still reads
`CD_NAME = Another World (USA)`. Change it to `Heart of the Alien (USA)`.

Leave `-DBYPASS_PROTECTION` and the LIBPCM block alone — they are Another World's and
this sub-project does not own them. Note them for the cleanup that will be needed
later; do not expand scope here.

- [ ] **Step 2: Add the knob**

Add near the top of `saturn/makefile`:

```make
# CD-DA audio on the built disc. `full` lays all 41 tracks (~425 MB, ~41 min);
# `none` builds a data-only disc of about 12 MB.
#
# Default none on purpose: while video and input are being brought up, every
# compile.bat would otherwise re-lay 41 audio tracks for a build that never
# plays them. Flip to full for a release or to test music.
HOTA_AUDIO ?= none
```

And **after** `include $(SDK_ROOT)/shared.mk`:

```make
# shared.mk:146 sets MUSIC_DIR = ./cd/music with `=`, and every use of it is
# inside a recipe, so a later assignment wins -- the same "append after the
# include" idiom this makefile already documents for LIBS. Pointing it at a
# directory whose tracklist is empty makes shared.mk report "No audio files
# found" and lay a data-only disc, with no patch to the SDK.
ifneq ($(HOTA_AUDIO),full)
MUSIC_DIR = ./cd/music-off
endif
```

- [ ] **Step 3: Create the empty tracklist**

`saturn/cd/music-off/tracklist`:
```
# Intentionally empty. Selected by HOTA_AUDIO = none (the default) so that
# shared.mk finds no audio and builds a data-only disc. The real track order
# is saturn/cd/music/tracklist, written by tools/extract_disc.
```

- [ ] **Step 4: Build both ways**

```bash
cd saturn && ./compile.bat clean && ./compile.bat debug
ls -la BuildDrop/
```
Expected: builds clean; `BuildDrop/Heart of the Alien (USA).cue` exists and contains
**one** `TRACK` line; the `.bin` is roughly 12 MB.

```bash
cd saturn && ./compile.bat clean && HOTA_AUDIO=full ./compile.bat debug
```
Expected: 42 `TRACK` lines in the cue; the `.bin` is roughly 425 MB. This build is slow
— that is the whole reason for the default.

> If Mednafen later reports `M:S:F time … out of range`, that is a stale `BuildDrop/`
> held open by another process, not an audio-track fault. Close it, `clean.bat`, rebuild.

- [ ] **Step 5: Hand the disc to Suinevere**

Do **not** launch Mednafen. Report that `BuildDrop/Heart of the Alien (USA).cue` is
built, say which `HOTA_AUDIO` setting produced it, and ask what they see. The engine
has no Saturn backend yet, so a disc that boots to a blank screen or a debug message is
the expected outcome at this stage — the point of this build is that the *disc layout*
is right, not that the game runs.

- [ ] **Step 6: Commit**

```bash
git add saturn/makefile saturn/cd/music-off/tracklist
git commit -m "HOTA_AUDIO knob, defaulting to a data-only disc

All 41 tracks is ~425 MB and re-lays 41 audio tracks on every build. While
video and input are being brought up that cost buys nothing, so the default
is none and full is opt-in.

Done by reassigning MUSIC_DIR after the shared.mk include rather than
patching the SDK: shared.mk assigns it with = and only ever expands it inside
recipes, so the later assignment wins. Same idiom this makefile already
documents for LIBS.

CD_NAME was still Another World (USA) from the scaffolding this directory was
copied out of."
```

---

## Notes for whoever executes this

**Task 2's track mapping is the one to get right, and it is deliberately first.** `+1`
versus `+2` is a one-character difference that plays a plausible-but-wrong song for the
entire game, or aims engine index 0 at the data track. The test asserting the function
never returns 1 is not ceremony — 1 is the data track, and on hardware that is noise or
a hang while an emulator may just go quiet. If music comes out recognisable but wrong
in Task 5, fix the mapping; do not compensate downstream.

**Do not "simplify" the per-sector read loop.** A MODE1/2352 track interleaves 304
bytes of sync, header and ECC between every 2048 bytes of payload. One contiguous
`fread` produces a file that is correct for its first 2048 bytes and corrupt after,
which decodes far enough to look like a bug in `decode.c`. The old `cd_iso.c` read from
a 2048-byte-per-sector `.iso` where the payload *was* contiguous; that is what changed.

**`src/sound.c` is not in this plan.** "Drop SDL_mixer" means stop using it to decode
music files. It stays linked, the device stays open, and sound effects keep running
through it. Removing `-lSDL2_mixer` deletes every sound effect in the game while
looking like a music-only change.

**The validation manifest is not a lookup table.** If a later reader is tempted to
"simplify" by looking files up in the manifest instead of the ISO9660 directory, that
re-introduces exactly what this work removed. The manifest exists only to fail fast on
a bad dump, and the LBA half of it is a warning on purpose.

**The real blocker is not in this plan.** `src/vm.c:26` declares
`static unsigned char memory[512*1024*2]` — 1 MB — and `src/game2bin.c:27` another
400 KB. That is ~1.4 MB of static BSS against the Saturn's 1 MB of High Work RAM, and
being `static` it never passes through SRL's TLSF arena, so no allocator tuning
touches it. Nothing here changes those arrays and nothing here needs to. It is written
down so it is not discovered late; it belongs to the memory-map sub-project.
