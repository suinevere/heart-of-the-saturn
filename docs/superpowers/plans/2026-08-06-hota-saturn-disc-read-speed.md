# Saturn Disc Read Speed Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `disc_read_file` issue one drive request for a whole file instead of forty, so `INTRO1.BIN` drops from 7,183 ms to roughly 1,500 ms.

**Architecture:** A new pure C module `discsec.c` splits a byte count into whole sectors plus a tail, and `disc_srl.cxx` reads the whole-sector body with a single `SRL::Cd::File::ReadSectors` straight into the engine's pointer, then reads the partial final sector into a 2 KB file-scope bounce buffer and copies out exactly the bytes the file has — so `max_size` still bounds what is written to `out` even though the drive now transfers whole sectors.

**Tech Stack:** C99 (`discsec.c`, host-tested with gcc), C++ (`disc_srl.cxx`), SaturnRingLib `SRL::Cd::File::ReadSectors`, raw SGL GFS through the public `File::Handle` in the fallback only.

**Design spec:** `docs/superpowers/specs/2026-08-06-hota-saturn-disc-read-speed-design.md`

## Global Constraints

These apply to every task. Violating any of them produces a failure that is silent, not loud.

- **`SaturnRingLib/` is not modified by any task.** It is a submodule, it already shows as modified in `git status` for unrelated reasons, and adding to that divergence makes it harder to unpick later. The `File::Read` bug it contains is not fixed there.
- **`max_size` bounds bytes written to `out`, not sectors read off the disc.** `ROOMS7.BIN`'s last disc sector holds 1,082 real bytes followed by 966 bytes of mastering filler; a whole-sector read straight into the engine's pointer would write all 966 of them past `ROOMS_LOAD_BASE + 160,826`. The bounce buffer exists for exactly this.
- **Never ask `ReadSectors` for more sectors than the file has.** `srl_cd.hpp:515-519` clamps its *buffer size* to the sectors remaining but forwards the *unclamped* `sectorCount` to `GFS_Fread`. Both counts in this plan derive from `Size.Bytes`, the one number `max_size` was compared against.
- **Derive the split from `Size.Bytes`, never from `Size.Sectors` / `Size.LastSectorSize`.** Those are a different pair of numbers from the same struct; if `GFS_GetFileSize` ever reported them inconsistent with `Bytes`, a read length derived from them would write past the bound just checked. Cross-check against them, do not read from them.
- **`.cxx`, never `.cpp`.** `shared.mk:239` and `shared.mk:242` have pattern rules for `%.c` and `%.cxx` only; a `.cpp` maps to no object, is silently dropped from the link, and fails at the first missing symbol rather than at the file that was never compiled.
- **`printf`, never `fprintf`.** `fprintf` renders nothing on Saturn and the cause is unknown.
- **One new diagnostic message, not four.** The split-versus-SRL disagreement refusal is the only message this sub-project adds. Every other way `disc_read_file_body` can fail already prints.
- **Banner comments are mandatory** on every file, function and constant, in the house format. The `Description` carries the constraint or the bug avoided, never a restatement of the signature. `Author: suinevere`. **No comments inside function bodies.** Test files get a file header only.
- **Indentation follows the file being edited.** `saturn/src/*.c` and `*.h` (`cdtoc.c`, `cdda_classify.c`, `discfmt.c`) are 4-space; `saturn/src/system/disc_srl.cxx` is tabs. Match the neighbour, do not reformat.
- **Commit messages are one sentence.** No body, no bullets, no trailers, no URLs, and no mention of Claude, AI, or a session — even if a tool prompt asks for one.
- **The host and Saturn builds collide.** Both write `saturn/src/*.o` under identical names. Run `rm -f saturn/src/*.o` whenever switching between `make -C saturn/src` and `saturn/compile.bat`, or you will chase a phantom failure.
- **No header dependency tracking.** A `.h`-only edit does not rebuild dependents. Delete objects to be sure a change took effect.
- **Do not run `make` for the Saturn build from Git Bash.** It fails with `Cannot create temporary file in C:\WINDOWS\: Permission denied`. Use `compile.bat` from PowerShell or cmd, which puts the toolchain on PATH itself.
- **Never launch Mednafen or any emulator from a tool call.** Build the disc, hand over the path, let Suinevere run it.
- **The probe must not survive this sub-project**, and it must survive long enough to measure against. Task 4 records the numbers; Task 7 removes the probe. Do not reorder those two.

**Build and test commands:**

| Purpose | Command | Where |
| --- | --- | --- |
| Host unit tests | `sh saturn/tests/run_tests.sh` | Git Bash, repo root |
| Host engine build | `make -C saturn/src` | Git Bash, repo root |
| Saturn disc, data-only (default) | `.\compile.bat debug` | PowerShell, in `saturn/` |
| Saturn disc, with music | `$env:HOTA_AUDIO = "full"; .\compile.bat debug` | PowerShell, in `saturn/` |

Saturn build output: `saturn/BuildDrop/Heart of the Alien (USA).{elf,iso,bin,cue,map}`.

---

## File Structure

| File | Created / Modified | Single responsibility |
| --- | --- | --- |
| `saturn/src/discsec.h` | Create (Task 1) | Declares the whole-sectors / tail-bytes split and `DISC_MAX_SECTOR_BYTES`, with the `extern "C"` guard its C++ caller needs. |
| `saturn/src/discsec.c` | Create (Task 1) | Implements that split. Four lines of integer arithmetic over numbers somebody else fetched: no SRL, no stdio, no engine header. |
| `saturn/tests/test_discsec.c` | Create (Task 1) | Host unit tests for `discsec.c`, including `ROOMS7.BIN`'s 78 + 1,082 split pinned by literal. |
| `saturn/tests/run_tests.sh` | Modify (Task 1) | Gains a fifth compile-and-run so the split is checked by the same suite as `discfmt`, `vm`, `cdtoc` and `cdda_classify`. |
| `saturn/src/system/disc_srl.cxx` | Modify (Tasks 2, 3, 5, 7) | Owns the reads and the bounce buffer: `ReadSectors` for the whole-sector body, one more into `g_tailSector` for the tail, plus the refusal guards, the corrected heap figures, and (Task 7) the removal of the timing probe. |
| `saturn/src/disc.h` | Modify (Task 2) | The `max_size` contract, strengthened to say that it bounds bytes written to `out` rather than sectors read off the disc. |
| `SaturnRingLib/` | **Not modified by any task.** | — |

---

### Task 1: `discsec` — the whole-sectors / tail-bytes split

Builds the pure module that decides how many complete sectors a file has and how many bytes are left over. Deliberately the twin of `cdtoc.{c,h}` and `cdda_classify.{c,h}`: integer arithmetic over caller-supplied numbers, free of SRL, stdio and every engine header, so it compiles for host and SH-2 and is tested off-target in milliseconds. Nothing in this task touches the CD.

**Files:**
- Create: `saturn/src/discsec.h`
- Create: `saturn/src/discsec.c`
- Create: `saturn/tests/test_discsec.c`
- Modify: `saturn/tests/run_tests.sh:22-25` (append a fifth compile-and-run after the `run_tests_cdda_classify` block)
- Test: `saturn/tests/test_discsec.c`

**Interfaces:**
- Consumes: nothing.
- Produces, all declared in `discsec.h`:
  - `int32_t discsec_whole_sectors(int32_t bytes, int32_t sector_size)` → number of complete sectors in `bytes`; 0 if `sector_size <= 0` or `bytes < 0`
  - `int32_t discsec_tail_bytes(int32_t bytes, int32_t sector_size)` → the `0 .. sector_size - 1` remainder; 0 if `sector_size <= 0` or `bytes < 0`
  - `#define DISC_MAX_SECTOR_BYTES 2048`

`int32_t` rather than `int` because both arguments are copied straight out of `SRL::Cd::FileSize`, whose `Bytes`, `Sectors`, `SectorSize` and `LastSectorSize` are all `int32_t` (`srl_cd.hpp:180-198`).

- [ ] **Step 1: Create the header**

Create `saturn/src/discsec.h` (4-space indent, matching `cdtoc.h` and `cdda_classify.h`):

```c
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
```

- [ ] **Step 2: Write the failing test**

Create `saturn/tests/test_discsec.c`:

```c
/*----------------------
 | test_discsec.c
 | Description: Host unit tests for discsec.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: discsec.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "discsec.h"

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

#define SECTOR 2048

typedef struct {
    const char *name;
    int32_t bytes;
    int32_t whole;
    int32_t tail;
    int32_t sectors;
    int32_t last_sector_size;
} manifest_row;

/* Every distinct size in saturn/src/disc_manifest.h, plus the sectors and
   last-sector-size SRL reports for each. The last two columns are what
   disc_read_file_body cross-checks the split against, so they are pinned here
   in the same table rather than reasoned about at the call site. */
static const manifest_row MANIFEST[] = {
    { "INTRO1.BIN and the seven other 432,128-byte animations",
      432128, 211, 0, 211, SECTOR },
    { "GAME2.BIN", 409600, 200, 0, 200, SECTOR },
    { "MAKE2MB.BIN", 436224, 213, 0, 213, SECTOR },
    { "ROOMS1.BIN and the six other 370,688-byte rooms",
      370688, 181, 0, 181, SECTOR },

    /* The row that matters. ROOMS7.BIN is the only manifest file with a tail,
       so if the split ever silently becomes a round-up, this is what fails.
       78 * 2048 = 159,744, leaving 1,082 bytes in a 79th sector whose other
       966 bytes belong to nobody. */
    { "ROOMS7.BIN -- the only file with a tail",
      160826, 78, 1082, 79, 1082 },
};

static void test_manifest_sizes(void)
{
    size_t i;

    for (i = 0; i < sizeof(MANIFEST) / sizeof(MANIFEST[0]); i++) {
        const manifest_row *r = &MANIFEST[i];
        int32_t whole = discsec_whole_sectors(r->bytes, SECTOR);
        int32_t tail = discsec_tail_bytes(r->bytes, SECTOR);

        if (whole != r->whole || tail != r->tail) {
            g_fail++;
            printf("FAIL %s\n  actual   = %d whole + %d tail\n"
                   "  expected = %d whole + %d tail\n",
                   r->name, (int)whole, (int)tail,
                   (int)r->whole, (int)r->tail);
            continue;
        }

        /* The split must reconstruct the byte count exactly. */
        CHECK_EQ(whole * SECTOR + tail, r->bytes);

        /* And it must agree with the pair SRL reports, because that agreement
           is the guard disc_read_file_body refuses on. */
        CHECK_EQ(whole + (tail != 0 ? 1 : 0), r->sectors);
        CHECK_EQ(tail != 0 ? tail : SECTOR, r->last_sector_size);
    }
}

static void test_boundaries(void)
{
    CHECK_EQ(discsec_whole_sectors(0, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(0, SECTOR), 0);

    CHECK_EQ(discsec_whole_sectors(1, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(1, SECTOR), 1);

    /* One byte under a sector boundary: no whole sector at all. */
    CHECK_EQ(discsec_whole_sectors(2047, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(2047, SECTOR), 2047);

    /* Exactly one sector: no tail, and specifically not a second sector. */
    CHECK_EQ(discsec_whole_sectors(2048, SECTOR), 1);
    CHECK_EQ(discsec_tail_bytes(2048, SECTOR), 0);

    /* One byte over: one whole sector and a one-byte tail. */
    CHECK_EQ(discsec_whole_sectors(2049, SECTOR), 1);
    CHECK_EQ(discsec_tail_bytes(2049, SECTOR), 1);

    /* One byte under two sectors. */
    CHECK_EQ(discsec_whole_sectors(4095, SECTOR), 1);
    CHECK_EQ(discsec_tail_bytes(4095, SECTOR), 2047);
}

static void test_degenerate_inputs(void)
{
    /* A non-positive sector size is a divide by zero or worse. Both functions
       answer 0, so the caller reads nothing and fails its own length check. */
    CHECK_EQ(discsec_whole_sectors(160826, 0), 0);
    CHECK_EQ(discsec_tail_bytes(160826, 0), 0);
    CHECK_EQ(discsec_whole_sectors(160826, -1), 0);
    CHECK_EQ(discsec_tail_bytes(160826, -1), 0);

    /* A negative byte count is a size field that was never filled in.
       C99 truncates toward zero, so -1 / 2048 would be 0 and -1 % 2048 would
       be -1 -- a negative memcpy length. Refused explicitly instead. */
    CHECK_EQ(discsec_whole_sectors(-1, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(-1, SECTOR), 0);
    CHECK_EQ(discsec_whole_sectors(-160826, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(-160826, SECTOR), 0);
}

static void test_sector_ceiling(void)
{
    /* The bounce buffer is built for this many bytes, so the constant and the
       Mode 1 sector size the split is exercised at must not drift apart. */
    CHECK_EQ(DISC_MAX_SECTOR_BYTES, 2048);
    CHECK_EQ(DISC_MAX_SECTOR_BYTES, SECTOR);

    /* A Mode 2 sector still splits correctly -- the arithmetic has no ceiling
       of its own. Refusing it is disc_read_file_body's job, against
       DISC_MAX_SECTOR_BYTES, because that is where the fixed buffer lives. */
    CHECK_EQ(discsec_whole_sectors(4672, 2336), 2);
    CHECK_EQ(discsec_tail_bytes(4672, 2336), 0);
    CHECK_EQ(discsec_whole_sectors(4673, 2336), 2);
    CHECK_EQ(discsec_tail_bytes(4673, 2336), 1);
}

int main(void)
{
    test_manifest_sizes();
    test_boundaries();
    test_degenerate_inputs();
    test_sector_ceiling();

    if (g_fail != 0) {
        printf("%d discsec check(s) failed\n", g_fail);
        return 1;
    }

    printf("discsec: all checks passed\n");
    return 0;
}
```

- [ ] **Step 3: Run test to verify it fails**

Run, from the repo root in Git Bash:

```bash
gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I saturn/src -o /tmp/run_tests_discsec saturn/tests/test_discsec.c saturn/src/discsec.c
```

Expected: FAIL with `gcc: error: saturn/src/discsec.c: No such file or directory`. (`discsec.h` exists from Step 1; the implementation does not.)

- [ ] **Step 4: Write minimal implementation**

Create `saturn/src/discsec.c` (4-space indent, matching `cdda_classify.c`):

```c
/*----------------------
 | discsec.c
 | Description: Implementation of discsec.h. Pure arithmetic over
 |   caller-supplied values, so none of this needs a disc, an emulator or SRL
 |   to test -- see saturn/tests/test_discsec.c.
 | Author: suinevere
 | Dependencies: discsec.h
 ----------------------*/
#include "discsec.h"

int32_t discsec_whole_sectors(int32_t bytes, int32_t sector_size)
{
    if (sector_size <= 0 || bytes < 0)
    {
        return 0;
    }

    return bytes / sector_size;
}

int32_t discsec_tail_bytes(int32_t bytes, int32_t sector_size)
{
    if (sector_size <= 0 || bytes < 0)
    {
        return 0;
    }

    return bytes % sector_size;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I saturn/src -o /tmp/run_tests_discsec saturn/tests/test_discsec.c saturn/src/discsec.c && /tmp/run_tests_discsec
```

Expected: PASS — `discsec: all checks passed`, exit 0. If `-Werror` rejects a signed/unsigned or size comparison, fix the code, not the flags; `run_tests.sh` uses the same flags for the four existing suites.

- [ ] **Step 6: Wire it into the test runner**

In `saturn/tests/run_tests.sh`, after the `run_tests_cdda_classify` block (line 25), append:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_discsec test_discsec.c ../src/discsec.c
./run_tests_discsec
```

- [ ] **Step 7: Run the whole suite**

Run: `sh saturn/tests/run_tests.sh`

Expected: all five suites pass, exit 0. The script is `set -e`, so any failure stops it.

- [ ] **Step 8: Confirm the Saturn build still links**

```bash
rm -f saturn/src/*.o
```

then, from PowerShell in `saturn/`:

```powershell
.\compile.bat debug
```

Expected: builds clean. `saturn/makefile` globs `find src/ -name '*.c'`, so `discsec.c` is picked up with no makefile edit — this step proves it compiles for SH-2, not just host gcc, before anything depends on it.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/discsec.h saturn/src/discsec.c saturn/tests/test_discsec.c saturn/tests/run_tests.sh
git commit -m "Split a file's size into whole sectors and a tail as pure arithmetic, since deriving the read length from SRL's separate sector fields could overrun the bound max_size was just checked against."
```

---

### Task 2: Whole-sector reads with a bounce buffer

Replaces the single `file.Read(file.Size.Bytes, out)` — forty-three `GFS_Fread` calls for `INTRO1.BIN`, each costing about 160 ms — with one `ReadSectors` for the whole-sector body and, when there is a tail, one more into a buffer this file owns. Also rewrites the two banner claims that stop being true the moment this lands, and adds the sentence to `disc.h` that tells a third backend what `max_size` actually bounds.

**Files:**
- Modify: `saturn/src/system/disc_srl.cxx:17-26` (the file banner's heap-cost paragraph)
- Modify: `saturn/src/system/disc_srl.cxx:28-44` (the file banner's `.bss` measurement paragraph — one appended sentence)
- Modify: `saturn/src/system/disc_srl.cxx:48-57` (includes)
- Modify: `saturn/src/system/disc_srl.cxx:109-121` (insert `g_tailSector` after `g_musicObserved`)
- Modify: `saturn/src/system/disc_srl.cxx:482-549` (`disc_read_file_body`'s banner and body)
- Modify: `saturn/src/disc.h:88-90` (the `disc_read_file` banner's `max_size` contract)
- Test: none on the host. `disc_srl.cxx` includes `<srl.hpp>` and cannot be host-compiled; its arithmetic lives in `discsec.c`, which Task 1 tested, and the rest of this task is verified by the Saturn link in Task 3 and the emulator run in Task 4.

**Interfaces:**
- Consumes from Task 1: `int32_t discsec_whole_sectors(int32_t bytes, int32_t sector_size)`, `int32_t discsec_tail_bytes(int32_t bytes, int32_t sector_size)`, `#define DISC_MAX_SECTOR_BYTES 2048`.
- Consumes existing, unchanged: `g_discOpened`, `normalize_name(const char *, char *, int32_t)`, `cdda_suspend()`, `cdda_restore()`.
- Consumes from SRL: `int32_t SRL::Cd::File::ReadSectors(const int32_t sectorCount, void *destination)` (`srl_cd.hpp:509`), returning bytes read, 0 for a closed file / EOF / a clamped-to-nothing request, negative on error.
- Produces for Task 7: nothing new. Task 7 edits the same file but only removes probe code.

Note: `memcpy` needs no new include. `saturn_compat.h`, already included at line 50, pulls in `<string.h>` behind the guard that keeps SGL's unguarded copy from breaking C++ linkage.

- [ ] **Step 1: Add the include**

In `saturn/src/system/disc_srl.cxx`, in the include block at lines 52-57, after `#include "disc.h"`, add:

```cpp
#include "discsec.h"
```

- [ ] **Step 2: Add the bounce buffer**

Immediately after the `g_musicObserved` definition (line 121) and before `normalize_name`'s banner, add (tabs, matching this file):

```cpp
/*----------------------
 | g_tailSector
 | Description: Where the final, partial sector of a file lands so that only
 |   the bytes the file actually has reach the caller. A whole-sector read
 |   straight into the engine's pointer would also write the rest of that
 |   sector -- for ROOMS7.BIN, 966 bytes of whatever the mastering tool put
 |   after 160,826 bytes of room data -- into the emulated 68000 map past the
 |   end of the room. disc.h promises that will not happen, and none of the
 |   three call sites bounds-checks the pointer it hands in.
 |
 |   File-scope static, not a stack array, for three reasons in order of
 |   weight. The stack cannot absorb it: disc_read_file is reached through
 |   main -> play_anm -> play_animation -> disc_read_file, and nothing in this
 |   tree states the SH-2 master stack size -- SGL's convention puts it below
 |   the 0x06004000 load address sgl.linker:4 gives PRELOADER, which is
 |   single-digit kilobytes, so a 2 KB frame against an unstated budget is an
 |   overflow that would present as corruption somewhere else entirely. A
 |   static is visible: it appears in the map as .bss and comes out of the same
 |   HWRAM the heap figure above is computed from, so the cost is subtracted
 |   once and stays subtracted, where a stack frame is invisible to the map and
 |   gets argued about instead of measured. And uint32_t guarantees the
 |   alignment: GFS transfers into this buffer, and a char[2048] has no
 |   alignment contract beyond whatever the compiler happens to give it.
 | Author: suinevere
 ----------------------*/
static uint32_t g_tailSector[DISC_MAX_SECTOR_BYTES / 4];
```

- [ ] **Step 3: Rewrite `disc_read_file_body`'s banner**

In `saturn/src/system/disc_srl.cxx`, replace this sentence in the `disc_read_file_body` banner (lines 489-492):

```
 |   512 KB. File::Read copies byte-by-byte into the destination through an
 |   internal sector buffer rather than GFS_Load's sector-rounded
 |   destination write, so it cannot overshoot max_size the way a raw
 |   LoadBytes into a tight buffer could.
```

with:

```
 |   512 KB. That claim used to rest on File::Read, which copied byte-by-byte
 |   into the destination through an internal sector buffer and so could not
 |   overshoot. It no longer does, and the guarantee is now this function's own
 |   to keep: the whole-sector body goes straight into out through one
 |   ReadSectors -- one GFS_Fread instead of File::Read's forty-three
 |   five-sector requests, each of which cost about 160 ms of the drive losing
 |   its place and re-acquiring it -- and the partial final sector goes into
 |   g_tailSector, out of which exactly discsec_tail_bytes bytes are copied.
 |   ROOMS7.BIN is the only file that takes that second path, and it is the
 |   reason the path exists: its last sector carries 1,082 bytes of room data
 |   and 966 bytes belonging to nobody.
 |
 |   Both sector counts derive from Size.Bytes, never from Size.Sectors, and
 |   that is load-bearing rather than tidy. ReadSectors clamps its buffer size
 |   to the sectors actually remaining but forwards the unclamped sectorCount
 |   to GFS_Fread (srl_cd.hpp:515-519), so asking for more sectors than the
 |   file has hands GFS a count larger than the buffer it was told about.
 |   Size.Bytes is the one number max_size was compared against, so a split
 |   derived from it cannot exceed what was bounded; the guard below then
 |   cross-checks that split against Size.Sectors and Size.LastSectorSize and
 |   refuses on any disagreement, rather than trusting either pair alone.
 |
 |   Neither Seek nor LoadBytes is used. Seek (srl_cd.hpp:538) allocates the
 |   same 10,240-byte work buffer this change exists to remove. LoadBytes
 |   (srl_cd.hpp:399) would be one GFS_Load for the whole file, but it closes
 |   and reopens the handle around the call and GFS_Load's destination write is
 |   sector-rounded -- precisely the overrun refused above.
```

- [ ] **Step 4: Replace the read**

In `saturn/src/system/disc_srl.cxx`, replace lines 538-546 — from `int32_t got = file.Read(file.Size.Bytes, out);` through the closing brace of the `if (got != file.Size.Bytes)` block — with (tabs):

```cpp
	int32_t whole = discsec_whole_sectors(file.Size.Bytes, file.Size.SectorSize);
	int32_t tail = discsec_tail_bytes(file.Size.Bytes, file.Size.SectorSize);

	if (file.Size.Bytes <= 0 ||
		file.Size.SectorSize <= 0 ||
		file.Size.SectorSize > DISC_MAX_SECTOR_BYTES ||
		whole + (tail != 0 ? 1 : 0) != file.Size.Sectors ||
		(tail != 0 ? tail : file.Size.SectorSize) != file.Size.LastSectorSize)
	{
		printf("disc_read_file: '%s' reports %d bytes / %d sectors / %d per sector / %d last, split says %d + %d\n",
			resolved, (int)file.Size.Bytes, (int)file.Size.Sectors,
			(int)file.Size.SectorSize, (int)file.Size.LastSectorSize,
			(int)whole, (int)tail);
		file.Close();
		return -1;
	}

	int32_t got = 0;

	if (whole > 0)
	{
		got = file.ReadSectors(whole, out);
	}

	if (tail > 0)
	{
		int32_t tailGot = file.ReadSectors(1, g_tailSector);

		if (tailGot >= tail)
		{
			memcpy((uint8_t *)out + (whole * file.Size.SectorSize), g_tailSector, (size_t)tail);
			got += tail;
		}
	}

	file.Close();

	if (got != file.Size.Bytes)
	{
		printf("disc_read_file: error reading '%s'\n", resolved);
		return -1;
	}
```

Three things about this block that are deliberate and not obvious:

- The five refusals are one guard with one message, not five with five. The design budgets exactly one new diagnostic for this function, and the message prints every number involved, so a listening test with no rebuild available can still tell which term fired.
- `got` accumulates `tail`, the bytes copied out, not `tailGot`, the bytes the drive moved — the tail read returns a whole sector. That is what makes the surviving `got != file.Size.Bytes` check at the end mean "the file arrived", which is the check that catches `ReadSectors` returning 0 for a closed file, EOF, or a clamped-to-nothing request, all of which are indistinguishable from a legitimate zero-length read at the call.
- `tailGot >= tail`, not `tailGot > 0`: a short read of the final sector would otherwise copy uninitialised buffer into the destination and still be counted.

- [ ] **Step 5: Correct the heap-cost paragraph**

In `saturn/src/system/disc_srl.cxx`'s file banner, replace lines 17-26:

```
 |   Heap cost, which is not obvious from anything in this file:
 |   srl_cd.hpp:441 allocates SectorSize * SectorsToReadAtOnce = 2048 * 5 =
 |   10,240 bytes on the first Read of every SRL::Cd::File and frees it at
 |   close. That is 14.7% of the 69,440-byte HWRAM heap the linker map leaves,
 |   held for the duration of each disc_read_file -- and saturn_compat.cxx's
 |   malloc deliberately refuses to fall back to LWRAM, so a caller holding
 |   59 KB across a read turns it into a NULL rather than a slow read. Nothing
 |   here stacks two open files to make it worse, and it is transient, but any
 |   future allocator sharing this heap has to be sized against what is left
 |   after it, not against the whole.
```

with:

```
 |   Heap cost, which is not obvious from anything in this file: there is none.
 |   srl_cd.hpp:441 allocates SectorSize * SectorsToReadAtOnce = 2048 * 5 =
 |   10,240 bytes on the first File::Read of every SRL::Cd::File and frees it
 |   at close; this file no longer calls File::Read, so that buffer is never
 |   created. ReadSectors hands the caller's destination straight to GFS_Fread
 |   and allocates nothing. The linker map leaves 64,800 bytes of HWRAM heap
 |   (__heap_start 0x060b02e0 .. __heap_end 0x060c0000), and
 |   saturn_compat.cxx's malloc deliberately refuses to fall back to LWRAM, so
 |   that figure is the entire budget for any future allocator on this heap --
 |   but it is no longer competing with a 10,240-byte transient that existed
 |   for exactly as long as a read was in flight.
```

The 64,800 figure and the two addresses come from `saturn/BuildDrop/Heart of the Alien (USA).map` as it stands before this change; Task 3 rebuilds and updates them to what the map says afterwards. The stale 69,440 they replace came from a much earlier build.

- [ ] **Step 6: Note the new `.bss` in the measurement paragraph**

In the same file banner, immediately after the `.bss` measurement paragraph's closing sentence (line 44, `... stacking on it.`), append:

```
 |
 |   g_tailSector adds 2,048 bytes on top of that, and frees none of it in
 |   exchange -- the 10,240 bytes it displaces were heap, not .bss. The trade
 |   is still strongly positive: those 10,240 bytes came out of the same HWRAM
 |   the heap figure above is computed from, and came out of it at exactly the
 |   moment a read was in flight, so peak HWRAM during a read drops by 8,192
 |   bytes even though total .bss rises.
```

- [ ] **Step 7: Strengthen the `disc.h` contract**

In `saturn/src/disc.h`, in the `disc_read_file` banner, replace line 89:

```
 |   called any other time, returns negative without touching *out.
```

with:

```
 |   called any other time, returns negative without touching *out.
 |
 |   max_size bounds the bytes written to out, not the sectors read off the
 |   disc. A backend is free to transfer whole sectors for speed -- that is
 |   the difference between one drive request and forty -- but the partial
 |   final sector of a file carries bytes past the file's end that belong to
 |   nobody, and those must land somewhere the backend owns rather than in the
 |   caller's buffer. saturn/src/system/disc_srl.cxx does exactly that. This is
 |   the non-obvious constraint a third implementer would otherwise discover
 |   the way the second one nearly did.
```

- [ ] **Step 8: Build both targets**

```bash
sh saturn/tests/run_tests.sh
make -C saturn/src
rm -f saturn/src/*.o
```

then, from PowerShell in `saturn/`:

```powershell
.\compile.bat debug
```

Expected: all pass. A link error naming `discsec_whole_sectors` or `discsec_tail_bytes` means the `extern "C"` guard in `discsec.h` did not take — delete the objects and rebuild, since there is no header dependency tracking.

- [ ] **Step 9: Confirm nothing under `SaturnRingLib/` changed**

```bash
git status --short SaturnRingLib
git diff --stat -- SaturnRingLib
```

Expected: whatever `SaturnRingLib` showed before this sub-project started, and nothing more — this task must not have added a single line to it. This is acceptance criterion 7.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/system/disc_srl.cxx saturn/src/disc.h
git commit -m "Read whole sectors in one request and bounce the partial last sector, since SRL's five-sector work-buffer loop charges 160 ms per request and issues forty-three of them for one animation."
```

---

### Task 3: Check the map and quote it

Confirms the two things the map can prove without an emulator — that 2,048 bytes of `.bss` appeared and that the 10,240-byte heap transient is gone — and replaces the heap numbers Task 2 wrote from the old map with the ones the new map reports. Acceptance criterion 3.

**Files:**
- Modify: `saturn/src/system/disc_srl.cxx:17-26` (the heap figure and the two addresses)
- Modify: `saturn/src/system/disc_srl.cxx:28-50` (the `.bss` measurement paragraph, which gains the measured totals)
- Test: `saturn/BuildDrop/Heart of the Alien (USA).map`, read directly.

**Interfaces:**
- Consumes from Task 2: the built `disc_srl.o` containing `g_tailSector`, and the banner text Task 2 wrote.
- Produces: nothing later tasks call. The numbers this task writes are the record Task 7 leaves behind.

- [ ] **Step 1: Confirm the new symbols are in the link**

```bash
grep -n "discsec_whole_sectors\|discsec_tail_bytes\|g_tailSector" "saturn/BuildDrop/Heart of the Alien (USA).map"
```

Expected: all three appear. If `g_tailSector` is absent the buffer was optimised away, which would mean nothing writes to it — stop and find out why before reading any other number off this map.

- [ ] **Step 2: Read the `.bss` and heap lines**

```bash
grep -n "^\.bss " "saturn/BuildDrop/Heart of the Alien (USA).map"
grep -n "__heap_start\|__heap_end" "saturn/BuildDrop/Heart of the Alien (USA).map"
```

Expected, against the pre-change map (`.bss 0x060222d0 0x8e000`, `__heap_start 0x060b02e0`, `__heap_end 0x060c0000`):

| Line | Before | Expected after | Why |
| --- | --- | --- | --- |
| `.bss` size | `0x8e000` (581,632) | `0x8e800` (583,680) | `g_tailSector` is 2,048 bytes |
| `__heap_start` | `0x060b02e0` | `0x060b0ae0` | the heap starts after `.bss`, so it moves up by the same 2,048 |
| `__heap_end` | `0x060c0000` | `0x060c0000` | fixed by the linker script |
| heap size | 64,800 | 62,752 | `__heap_end - __heap_start` |

Alignment padding can move these by a few bytes either way; a difference of roughly 2,048 in each is the pass. A difference of roughly 12,288 means something else grew too — find it before continuing. **The heap gets smaller and that is the expected outcome**: the 2,048 bytes are now permanently resident instead of 10,240 transient, so peak HWRAM during a read drops by 8,192 even as the standing budget drops by 2,048.

- [ ] **Step 3: Write the measured numbers into the banner**

In `saturn/src/system/disc_srl.cxx`'s heap-cost paragraph, replace the figure and the two addresses Task 2 wrote:

```
 |   created. ReadSectors hands the caller's destination straight to GFS_Fread
 |   and allocates nothing. The linker map leaves 64,800 bytes of HWRAM heap
 |   (__heap_start 0x060b02e0 .. __heap_end 0x060c0000), and
```

with the values Step 2 actually reported, in the same shape — for the expected map that is:

```
 |   created. ReadSectors hands the caller's destination straight to GFS_Fread
 |   and allocates nothing. The linker map leaves 62,752 bytes of HWRAM heap
 |   (__heap_start 0x060b0ae0 .. __heap_end 0x060c0000), and
```

Do this even if the numbers happen to match what Task 2 wrote: the point is that they come off this build's map rather than a previous one.

Then, in the `.bss` measurement paragraph, replace the sentence Task 2 appended:

```
 |   g_tailSector adds 2,048 bytes on top of that, and frees none of it in
```

with the same sentence carrying the measured totals — for the expected map:

```
 |   g_tailSector adds 2,048 bytes on top of that, taking total .bss to
 |   583,680 (0x8e800) and this file's own object to 2,469, and frees none of it in
```

If Step 2 reported different totals, use those. Re-read this file's own `.bss` contribution with:

```bash
grep -n "disc_srl.o" "saturn/BuildDrop/Heart of the Alien (USA).map" | head
```

- [ ] **Step 4: Confirm `disc_cue.c` is byte-identical**

```bash
git diff --stat -- saturn/host/disc_cue.c
```

Expected: empty. The host backend is untouched by this sub-project, which is why the host read path carries no regression risk and is not re-tested by hand. This is half of acceptance criterion 2; `make -C saturn/src` in Task 2 Step 8 was the other half.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/system/disc_srl.cxx
git commit -m "Quote the heap and .bss figures this build's linker map actually reports, since the tail buffer moved both."
```

---

### Task 4: The emulator run that settles the alignment question

This is the largest risk in the design and the only thing that can answer it. `ANIMATION_LOAD_BASE` is `0x809a` (`disc_manifest.h:56`) and the LWRAM block holding the emulated map is 4-byte aligned (`srl_memory.hpp:322-328`), so every animation read with `fileoffset 0` — `INTRO1.BIN` through `INTRO4.BIN`, `END1.BIN` through `END4.BIN`, `MID2.BIN` — lands at `base + 2 (mod 4)`. `File::Read` hid this completely, because GFS only ever wrote into SRL's own aligned work buffer. `ReadSectors` does not: GFS now writes directly to a 2-mod-4 destination, and whether the default transfer mode tolerates that is **UNVERIFIED**. SRL calls `GFS_Init` (`srl_cd.hpp:629`) and never calls `GFS_SetTmode`, so GFS runs in whatever it defaults to among `GFS_TMODE_SCU`, `GFS_TMODE_SDMA0`, `GFS_TMODE_SDMA1`, `GFS_TMODE_CPU` and `GFS_TMODE_STM` (`sega_gfs.h:115-120`). Splitting the request does not help — the alignment is a property of `ANIMATION_LOAD_BASE`, not of where the read starts, so every sector of an animation lands at the same offset mod 4.

**Files:**
- Create: none.
- Modify: none.
- Test: `saturn/BuildDrop/Heart of the Alien (USA).cue`, run by Suinevere.

**Interfaces:**
- Consumes from Task 3: the built and map-checked disc.
- Produces for Task 5: a yes/no on whether the default transfer mode tolerates a 2-mod-4 destination.
- Produces for Task 7: the measured `probe read:` milliseconds for `INTRO1.BIN`, which Task 7 writes into the banner before the probe that produced them is deleted.

**No commit.** This task changes no file. Its deliverables are a decision and a number, both of which are inputs to later tasks. Record the numbers where the next session can find them; do not invent a file to commit.

- [ ] **Step 1: Build the disc with audio**

```bash
rm -f saturn/src/*.o
```

then, from PowerShell in `saturn/`:

```powershell
$env:HOTA_AUDIO = "full"; .\compile.bat debug
```

Expected: `saturn/BuildDrop/Heart of the Alien (USA).{bin,cue}`, with the `.bin` roughly 443 MB. This takes several minutes.

`HOTA_AUDIO=full` and not the default data-only build, for one reason: the 2026-08-06 baseline in the design spec's measurement table was taken on an audio disc — the probe added in `be08f5f` measures how long the drive takes to reach `CDC_ST_PLAY` after a play command, which only produces output when there are audio tracks to play. Measuring the fix on a data-only disc would compare a read with `cdda_suspend`/`cdda_restore` doing real work against one where both are no-ops, and the difference would be attributed to the wrong change.

- [ ] **Step 2: Confirm the track count before trusting any timing**

```bash
grep -c '^  TRACK' "saturn/BuildDrop/Heart of the Alien (USA).cue"
```

Expected: `42`. Anything else means the audio layout differs from the disc the baseline was measured on, and the comparison is not like-for-like.

- [ ] **Step 3: Hand the disc to Suinevere**

Hand over `saturn/BuildDrop/Heart of the Alien (USA).cue` and ask for exactly these two things, in this order:

1. **Do all four intro animations render correctly?** This is the alignment question and nothing else decides it. `INTRO1.BIN` is the first thing the game loads into an unaligned destination, on the way into the first intro animation, before any input is needed — the run either produces a correct animation or an obviously broken one, immediately, with no ambiguity and nothing to drive the game into first.
   - **PASS:** all four animations look the way they did before this sub-project.
   - **FAIL:** any of them shows torn, shifted, garbled or byte-swapped imagery, or the game hangs during one. That is the default GFS transfer mode refusing a 2-mod-4 destination. Go to Task 5.
2. **What does `probe read: 'INTRO1.BIN' took N ms` report?** Also capture the lines for `GAME2.BIN`.
   - **PASS:** roughly 1,500 ms for `INTRO1.BIN`, against 7,183 ms in the baseline table. Anything under 3,000 ms is the fix working; the exact figure depends on what Mednafen charges per request.
   - **FAIL:** still near 7,000 ms. The read did not change shape — check that `disc_srl.o` was actually rebuilt (`rm -f saturn/src/*.o`, there is no header dependency tracking) before concluding anything about the drive.

**Do not run the emulator from a tool call.** **Do not claim the alignment works until this run has happened.**

- [ ] **Step 4: Write the numbers down**

Record the reported `probe read:` figures for `INTRO1.BIN` and `GAME2.BIN` verbatim. Task 7 puts them in `disc_srl.cxx`'s banner, in the same commit that deletes the probe — after which nothing else in the tree records what the change bought.

---

### Task 5 (conditional): Force `GFS_TMODE_CPU`

**Run this task only if Task 4 Step 3 question 1 failed.** If the animations rendered correctly, the default transfer mode tolerates a 2-mod-4 destination, this task is not needed, and skipping it is the right outcome — say so and go to Task 6.

Trades DMA for a software transfer that still costs one request instead of forty-three. It is a two-line change inside `disc_srl.cxx`, touches no submodule file, and is why the alignment risk is survivable rather than fatal to the approach.

**Files:**
- Modify: `saturn/src/system/disc_srl.cxx:532-537` (immediately after `file.Open()` succeeds, before the split guard)
- Test: `saturn/BuildDrop/Heart of the Alien (USA).cue`, run by Suinevere.

Line numbers here are as of `83a643a`, before Task 2 edited this file. Task 2's banner rewrites move everything below them down by roughly forty lines — locate each edit by the quoted code, not by the number.

**Interfaces:**
- Consumes: `SRL::Cd::File::Handle`, the public `GfsHn` member at `srl_cd.hpp:241`; `GFS_SetTmode(GfsHn, int32_t)` at `sega_gfs.h:423`; `GFS_TMODE_CPU` at `sega_gfs.h:115-120`.
- Produces: nothing later tasks call.

- [ ] **Step 1: Set the transfer mode after opening**

In `saturn/src/system/disc_srl.cxx`, immediately after the `if (!file.Open())` block and before the split guard Task 2 added, insert (tabs):

```cpp
	GFS_SetTmode(file.Handle, GFS_TMODE_CPU);
```

- [ ] **Step 2: Say why in the banner**

In the `disc_read_file_body` banner, immediately after the paragraph ending `... belonging to nobody.`, insert:

```
 |
 |   GFS_SetTmode(GFS_TMODE_CPU) is not tuning. ANIMATION_LOAD_BASE is 0x809a
 |   and the LWRAM block holding the emulated map is 4-byte aligned
 |   (srl_memory.hpp:322-328), so every animation read with fileoffset 0 lands
 |   at base + 2 (mod 4). File::Read hid that -- GFS only ever wrote into SRL's
 |   own aligned work buffer -- and ReadSectors does not, because GFS writes
 |   the destination directly. The default transfer mode was measured on the
 |   emulator producing corrupt animation frames at that offset, so this port
 |   pays a software transfer to get a correct one. It is still one request per
 |   file rather than forty-three, which is where the time was.
```

- [ ] **Step 3: Rebuild**

```bash
rm -f saturn/src/*.o
```

then, from PowerShell in `saturn/`:

```powershell
$env:HOTA_AUDIO = "full"; .\compile.bat debug
```

Expected: builds clean. A compile error naming `GFS_SetTmode` or `GFS_TMODE_CPU` means `sega_gfs.h` is not reached through `<srl.hpp>` — add nothing to `SaturnRingLib/`; include the SGL header directly from this file instead.

- [ ] **Step 4: Hand the disc back to Suinevere**

Hand over `saturn/BuildDrop/Heart of the Alien (USA).cue` and ask the same two questions as Task 4 Step 3.

- **PASS:** all four intro animations render correctly, and `probe read: 'INTRO1.BIN'` is still well under 3,000 ms. A software transfer is slower per byte than DMA, but the bytes were never the cost.
- **FAIL, animations still corrupt:** the destination alignment is not the cause. Stop and re-diagnose rather than trying the remaining `GFS_TMODE_*` values one at a time; the design does not claim any of them fixes an unaligned write.
- **FAIL, animations correct but the read is slow again:** CPU transfer is genuinely too slow for a 432 KB request on this hardware. That is a real finding and it belongs in the spec's Deferred section, not in another guess.

**Do not run the emulator from a tool call.**

- [ ] **Step 5: Commit**

```bash
git add saturn/src/system/disc_srl.cxx
git commit -m "Force CPU transfer for disc reads, since the default GFS mode corrupts the two-mod-four destination every animation loads into."
```

---

### Task 6: The tail path and `ROOMS7.BIN`

`ROOMS7.BIN` is the only manifest file with a tail, so it is the only file that exercises the bounce buffer at all. Any room that renders correctly proves the whole-sector path; `ROOMS7.BIN` specifically proves the bounce. Rooms need the game driven into them, which the input sub-project has only recently made possible — this is why the design reasons about the tail and proves it here rather than earlier.

**Files:**
- Create: none.
- Modify: none.
- Test: the disc already built by Task 4 Step 1, or Task 5 Step 3 if the fallback was applied.

**Interfaces:**
- Consumes: the same disc Task 4 or Task 5 handed over. No rebuild.
- Produces: nothing later tasks call.

**No commit.** Verification only, for the same reason Task 4 has none.

- [ ] **Step 1: Hand the same disc to Suinevere with the room questions**

Hand over `saturn/BuildDrop/Heart of the Alien (USA).cue` — the disc from Task 4 Step 1, or Task 5 Step 3 if the fallback was applied — and ask for these three, specifically:

1. **Drive the game into a room and confirm it renders correctly.** Any room proves the whole-sector path: `ROOMS1.BIN` through `ROOMS8.BIN` except `ROOMS7.BIN` are all 370,688 bytes, an exact 181 sectors, so they take the `whole > 0, tail == 0` path with no bounce at all.
2. **Reach room 7 and confirm it renders correctly.** `ROOMS7.BIN` is 160,826 bytes: 78 whole sectors plus 1,082 bytes. This is the only read in the game that copies out of `g_tailSector`.
   - **PASS:** the room renders the way it did before this sub-project.
   - **FAIL, corrupt near the end of the room:** the tail copy has the wrong length or the wrong source offset. `whole * file.Size.SectorSize` is 159,744 and the copy is 1,082 bytes.
   - **FAIL, `disc_read_file: error reading 'ROOMS7.BIN'`:** the tail `ReadSectors` returned less than 1,082, so `got` never reached 160,826.
   - **FAIL, `disc_read_file: 'ROOMS7.BIN' reports ...`:** the split disagreed with `Size.Sectors` / `Size.LastSectorSize`. The message prints all six numbers; expected is `160826 / 79 / 2048 / 1082, split says 78 + 1082`.
3. **What does `probe read: 'ROOMS7.BIN' took N ms` report?** Expected roughly 840 ms — two requests instead of thirty-seven. `ROOMS1.BIN` should report roughly 1,300 ms against 5,800 ms in the baseline table.

**Do not run the emulator from a tool call.**

- [ ] **Step 2: Record the room figures**

Record the `probe read:` figures for `ROOMS7.BIN` and one full-size room. Task 7 needs them for the same reason it needs the animation figures.

---

### Task 7: Remove the timing probe

The probe is not a neutral observer. `cdda_probe_wait` spins on `CDC_GetCurStat` for up to four seconds inside `disc_play_track` and inside `cdda_restore`, and that blocking wait distorts game-loop pacing: `main.c:459`'s `rest()` sprints to catch up after the stall, so the frames immediately following a track change do not run at the rate they will once the probe is gone. Any timing conclusion drawn with the probe in place is a conclusion about a game loop that no longer exists — which is exactly why Tasks 4 and 6 happen first, and why their numbers go into the banner in this commit rather than being lost with the code that produced them. Acceptance criterion 6.

**Files:**
- Modify: `saturn/src/system/disc_srl.cxx:57` (drop `#include "platform.h"`)
- Modify: `saturn/src/system/disc_srl.cxx:190-227` (delete `cdda_probe_wait` and `CDDA_PROBE_CAP_MS`)
- Modify: `saturn/src/system/disc_srl.cxx:370` (delete the `restore/resume` call)
- Modify: `saturn/src/system/disc_srl.cxx:384` (delete the `restore/restart` call)
- Modify: `saturn/src/system/disc_srl.cxx:568-582` (`disc_read_file`: drop `t0`, `t1` and the `probe read` printf)
- Modify: `saturn/src/system/disc_srl.cxx:667` (delete the `play_track` call)
- Modify: `saturn/src/system/disc_srl.cxx:17-26` (record the measured before/after in the file banner)
- Test: `grep`, then a boot smoke test by Suinevere.

Line numbers here are as of `83a643a`, before Tasks 2, 3 and possibly 5 edited this file; every one of them has shifted down by roughly forty to sixty lines by the time this task runs. Locate each edit by the quoted code, not by the number.

**Interfaces:**
- Consumes from Task 4 Step 4 and Task 6 Step 2: the measured `probe read:` milliseconds.
- Produces: nothing. This is the last task.

- [ ] **Step 1: Delete `cdda_probe_wait` and its cap**

In `saturn/src/system/disc_srl.cxx`, delete lines 190-227 in full: the `cdda_probe_wait` banner, the `#define CDDA_PROBE_CAP_MS 4000`, and the function body. The next thing after the opening `extern "C" {` becomes `cdda_halt`'s banner.

- [ ] **Step 2: Delete the three call sites**

Delete each of these lines where it appears:

```cpp
		cdda_probe_wait("restore/resume", cue);
```

```cpp
		cdda_probe_wait("restore/restart", cue);
```

```cpp
	cdda_probe_wait("play_track", cue);
```

The first two are in `cdda_restore`'s `switch`; the third is in `disc_play_track`, immediately after `SRL::Sound::Cdda::PlaySingle` and before `g_musicTrack = engine_index;`.

- [ ] **Step 3: Restore `disc_read_file` to a plain bracket**

Replace the body of `disc_read_file` — lines 569-582, from `int result;` through `return result;` — with (tabs):

```cpp
	int result;

	cdda_suspend();
	result = disc_read_file_body(name, out, max_size);
	cdda_restore();

	return result;
```

- [ ] **Step 4: Drop the `platform.h` include**

Delete line 57:

```cpp
#include "platform.h"
```

`platform_ticks` was the only thing this file took from it, and the probes were the only callers.

- [ ] **Step 5: Record what the change bought**

In `saturn/src/system/disc_srl.cxx`'s file banner, immediately after the heap-cost paragraph Task 3 finished, insert a paragraph carrying the figures Tasks 4 and 6 measured. For the expected numbers that is:

```
 |
 |   What the whole-sector read bought, measured on the emulator with a
 |   temporary probe around disc_read_file_body and recorded here because the
 |   probe itself does not survive: INTRO1.BIN (432,128 bytes, 211 sectors)
 |   fell from 7,183 ms to 1,530 ms, GAME2.BIN (409,600, 200) from 6,300 ms to
 |   1,450 ms, ROOMS1.BIN (370,688, 181) from 5,800 ms to 1,320 ms, and
 |   ROOMS7.BIN (160,826, 78 + a 1,082-byte tail) to 840 ms across its two
 |   requests. The cost was never the bytes -- 10,240 of them take 33 ms at 2x
 |   -- it was the roughly 160 ms the drive charges per request, and File::Read
 |   issued forty-three of them for one animation.
```

**Replace every figure above with what Task 4 Step 4 and Task 6 Step 2 actually reported.** The "before" column comes from the design spec's measurement table and is fixed; the "after" column is whatever the runs said. If a run was not taken for one of these files, drop that file from the sentence rather than estimating it.

- [ ] **Step 6: Verify the probe is gone**

```bash
grep -n "cdda_probe_wait\|probe read\|platform_ticks\|CDDA_PROBE_CAP_MS\|platform.h" saturn/src/system/disc_srl.cxx
```

Expected: no matches, exit 1. This is acceptance criterion 6 exactly.

- [ ] **Step 7: Rebuild both targets**

```bash
sh saturn/tests/run_tests.sh
make -C saturn/src
rm -f saturn/src/*.o
```

then, from PowerShell in `saturn/`:

```powershell
.\compile.bat debug
```

Expected: all pass. This is the default data-only build — small and quick, which is what a boot smoke test wants. A compile error about an unused variable in `disc_read_file` means `t0` or `t1` survived Step 3.

- [ ] **Step 8: Boot smoke test**

Hand `saturn/BuildDrop/Heart of the Alien (USA).cue` to Suinevere and ask: does it boot, do the intro animations render, and is there any new stall at an animation boundary now that `cdda_probe_wait`'s four-second cap is gone from `disc_play_track`?

Expected: boots, animations render, no music (this is the `HOTA_AUDIO=none` disc), no stall. Removing a blocking wait cannot make the game slower, so a new stall here would mean something else in Step 1-4 was deleted by mistake.

**Do not run the emulator from a tool call.**

- [ ] **Step 9: Commit**

```bash
git add saturn/src/system/disc_srl.cxx
git commit -m "Remove the read and play-status timing probes now that the whole-sector read is measured, keeping the numbers in the banner since the probe that produced them does not survive."
```

---

## What this plan does not cover

Stated so the next reader does not mistake absence for oversight. These come from the design spec's Deferred and Out of scope sections.

- **`GFS_TMODE` selection is not tuned.** Task 5 reaches for `GFS_TMODE_CPU` only as the alignment fallback, and only if Task 4 says it is needed. Whether SCU DMA versus cycle-steal versus CPU transfer is measurably faster for a 432 KB request is a separate question this sub-project does not ask.
- **Streaming, overlapped or background reads.** `GFS_NwFread` returns immediately and would let a read overlap with decode, but every caller here preloads and then plays from RAM, so there is nothing to overlap with until an animation decoder is rewritten to consume the file as it arrives.
- **Read-ahead or caching across calls.** `ROOMS*.BIN` are re-read on every room change. Caching them is a memory-budget question, not a throughput one.
- **The residual CD-DA start gap**, and **the discarded first `PlaySingle`** in `play_anm`. Both are CD-DA timing, both have their own sub-projects, and faster reads shorten the window the wasted play command sits in without removing it.
- **Physical audio track reordering.**
- **The host backend.** `saturn/host/disc_cue.c` is not touched, so the host read path carries no regression risk and is not re-tested by hand.
- **Hardware.** Verification here is Mednafen only. Whether Mednafen models per-request drive latency the way a real CD block does is unknown; the design does not depend on the exact 160 ms, only on the cost being per-request, which is the part the three baseline measurements agree on.
