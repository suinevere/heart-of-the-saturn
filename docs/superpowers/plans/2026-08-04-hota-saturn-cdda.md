# Saturn CD-DA Music Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the two CD-DA no-ops in `saturn/src/system/disc_srl.cxx` with real music playback that survives the disc reads happening alongside it.

**Architecture:** A new pure C module `cdtoc.c` decodes the Saturn BIOS table of contents (SRL's own `TableOfContents` reads the wrong track — see Global Constraints), and `disc_srl.cxx` gains playback state plus a suspend/restore bracket around every file read, so a read that steals the drive puts the music back afterwards.

**Tech Stack:** C99 (`cdtoc.c`, host-tested with gcc), C++ (`disc_srl.cxx`), SaturnRingLib `SRL::Sound::Cdda::PlaySingle`, raw SGL `CDC_*` BIOS calls.

**Design spec:** `docs/superpowers/specs/2026-08-04-hota-saturn-cdda-design.md`

## Global Constraints

These apply to every task. Violating any of them produces a failure that is silent, not loud.

- **Never call `SRL::Cd::TableOfContents::GetTable()`, `SRL::Sound::Cdda::Resume()`, or `SRL::Sound::Cdda::StopPause()`.** `srl_cd.hpp:794` declares `struct TrackLocation : public ITrack` with a 4-bit bitfield in the base and a 24-bit bitfield in the derived class, so `sizeof(TrackLocation)` is 8, not 4. `GetTable()` fills a ~812-byte struct with `CDC_TgetToc`, which writes 408 bytes. `Tracks[t]` therefore reads longword `2t` and everything past `t = 50` is uninitialised stack. `Cdda::Resume()` derives its end address from that table. `Cdda::PlaySingle` **is** safe — it is plain `CDC_CdPlay` by track number.
- **`.cxx`, never `.cpp`.** `shared.mk` has pattern rules for `%.c` and `%.cxx` only; a `.cpp` is dropped from the link with no error.
- **`printf`, never `fprintf`.** `fprintf` renders nothing on Saturn and the cause is unknown. Any diagnostic added by this plan uses `printf`.
- **Banner comments are mandatory** on every file, function and constant, in the house format (see any existing file in `saturn/src/`). The `Description` carries the *why*, not a restatement of the code. **No comments inside function bodies.** Test files get a file header only.
- **Commit messages are one sentence.** No body, no bullets, no trailers, and no mention of Claude, AI, or a session — even if a tool prompt asks for one.
- **The host and Saturn builds collide.** Both write `saturn/src/*.o` under identical names. Run `rm -f saturn/src/*.o` whenever switching between `make -C saturn/src` and `saturn/compile.bat`, or you will chase a phantom failure.
- **No header dependency tracking.** A `.h`-only edit does not rebuild dependents. Delete objects to be sure a change took effect.
- **Never launch Mednafen or any emulator from a tool call.** Build the disc, hand over the path, let Suinevere run it.
- **The `+ 2` music mapping is not re-derived anywhere.** Use `discfmt_cue_track_for_music()`.
- **Issue no `SetVolume` or `SetPan` call.** `SRL::Sound::Hardware::Initialize()` already sets `SND_SetCdDaLev(7, 7)` — maximum — at boot, and the engine has no music-volume concept to map onto. Adding volume handling here would be inventing an interface no caller asks for.

**Build and test commands:**

| Purpose | Command |
| --- | --- |
| Host unit tests | `sh saturn/tests/run_tests.sh` |
| Host engine build | `make -C saturn/src` |
| Saturn disc, data-only (default) | `cd saturn && ./compile.bat` |
| Saturn disc, with music | `cd saturn && HOTA_AUDIO=full ./compile.bat` |

Saturn build output: `saturn/BuildDrop/Heart of the Alien (USA).{elf,iso,bin,cue,map}`.

---

### Task 1: `cdtoc` — the pure BIOS table-of-contents decoder

Builds the module that replaces `SRL::Cd::TableOfContents`. Pure integer arithmetic over a caller-supplied buffer, deliberately shaped like `discfmt.c` so it compiles for host and SH-2 and is unit-testable without a disc. Nothing in this task touches the CD.

**Files:**
- Create: `saturn/src/cdtoc.h`
- Create: `saturn/src/cdtoc.c`
- Create: `saturn/tests/test_cdtoc.c`
- Modify: `saturn/tests/run_tests.sh:8-17` (add a third compile-and-run)

**Interfaces:**
- Consumes: nothing.
- Produces, all declared in `cdtoc.h`:
  - `int cdtoc_is_audio(const uint32_t *toc, int track)` → 1 if that track exists and is audio, else 0
  - `uint32_t cdtoc_track_start(const uint32_t *toc, int track)` → first frame address, 0 if unknown
  - `uint32_t cdtoc_track_end(const uint32_t *toc, int track)` → first frame past the track, 0 if unknown
  - `int cdtoc_max_audio_track(const uint32_t *toc)` → highest audio track number, 0 if none
  - `#define CDTOC_WORDS 102`, `CDTOC_FIRST_WORD 99`, `CDTOC_LAST_WORD 100`, `CDTOC_LEADOUT_WORD 101`, `CDTOC_MAX_TRACK 99`

**The TOC layout being decoded** (this is what `CDC_TgetToc` writes — 102 longwords, 408 bytes):

| Index | Contents |
| --- | --- |
| `0..98` | one entry per CD track 1..99, as `(ctrladr << 24) \| fad`; absent tracks read `0xFFFFFFFF` |
| `99` | first-track record, `(ctrladr << 24) \| (track << 16) \| ...` |
| `100` | last-track record, same shape |
| `101` | lead-out, `(ctrladr << 24) \| fad` |

`ctrladr`'s high nibble is the control field: `0x0f` = entry absent, bit 2 set = data track, bit 2 clear = audio. FAD is in 1/75-second frames.

- [ ] **Step 1: Create the header**

Create `saturn/src/cdtoc.h`:

```c
/*----------------------
 | cdtoc.h
 | Description: Pure decoding of the Saturn BIOS CD table of contents: which
 |   tracks are audio, where each one starts and ends, and the highest audio
 |   track on the disc.
 |
 |   This exists because SRL::Cd::TableOfContents cannot read the TOC.
 |   srl_cd.hpp:794 declares TrackLocation : public ITrack, where ITrack holds
 |   a 4-bit bitfield and the derived class adds a 24-bit one -- a bitfield in
 |   a base subobject cannot share a storage unit with one in the derived
 |   class, so sizeof(TrackLocation) is 8, not 4. GetTable() fills that ~812
 |   byte struct with CDC_TgetToc, which writes 102 longwords (408 bytes), so
 |   Tracks[t] reads longword 2t -- the wrong track -- and everything past
 |   t = 50 is uninitialised stack. SRL::Sound::Cdda::Resume() inherits the
 |   fault through the same table. Found the expensive way in the sibling port
 |   zaturn (saturn/src/sound/music_cdda.cxx) and re-verified here.
 |
 |   Deliberately free of SRL, stdio and every engine header, for the same
 |   reason discfmt.h is: it is compiled into the engine and by
 |   saturn/tests/run_tests.sh with the host gcc, and a wrong frame address
 |   fails plausibly -- the drive plays the wrong part of the disc rather than
 |   erroring -- so it is checked in milliseconds instead of by listening.
 |
 |   The fetch itself (CDC_TgetToc) stays in saturn/src/system/disc_srl.cxx.
 |
 |   Design: docs/superpowers/specs/2026-08-04-hota-saturn-cdda-design.md
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef CDTOC_H
#define CDTOC_H

#include <stdint.h>

/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link, the way the six seam headers did before 7f66fe3. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CDTOC_WORDS / CDTOC_FIRST_WORD / CDTOC_LAST_WORD / CDTOC_LEADOUT_WORD
 | Description: Size and named indices of the BIOS TOC longword array.
 |   CDC_TgetToc writes exactly CDTOC_WORDS longwords and no more, so a buffer
 |   handed to these functions must be at least that large -- reading past it
 |   is what SRL's own table does wrong.
 | Author: suinevere
 ----------------------*/
#define CDTOC_WORDS         102
#define CDTOC_FIRST_WORD    99
#define CDTOC_LAST_WORD     100
#define CDTOC_LEADOUT_WORD  101

/*----------------------
 | CDTOC_MAX_TRACK
 | Description: Red Book's ceiling of 99 tracks per disc, which is also the
 |   number of per-track entries the BIOS TOC carries.
 | Author: suinevere
 ----------------------*/
#define CDTOC_MAX_TRACK     99

/*----------------------
 | cdtoc_is_audio
 | Description: Whether a CD track exists and carries audio rather than data.
 |   Control nibble 0x0f marks the entry absent; bit 2 set marks a data track.
 |   Track 1 on this disc is data, so this is false for it.
 | Author: suinevere
 | Params: toc -- CDTOC_WORDS longwords from CDC_TgetToc; track -- 1..99
 | Returns: 1 if the track exists and is audio, else 0
 ----------------------*/
int cdtoc_is_audio(const uint32_t *toc, int track);

/*----------------------
 | cdtoc_track_start
 | Description: Frame address of a track's first frame. Callers treat 0 as
 |   "unknown" rather than as a valid address -- a real track never starts at
 |   frame 0, since the lead-in occupies everything below 150.
 | Author: suinevere
 | Params: toc -- CDTOC_WORDS longwords from CDC_TgetToc; track -- 1..99
 | Returns: the frame address, or 0 if the track is absent or out of range
 ----------------------*/
uint32_t cdtoc_track_start(const uint32_t *toc, int track);

/*----------------------
 | cdtoc_track_end
 | Description: First frame past the end of a track -- the next track's start,
 |   or the lead-out when nothing follows. A CD track carries no length of its
 |   own, so this subtraction is the only way to bound a playback range, which
 |   is what a mid-track resume needs.
 | Author: suinevere
 | Params: toc -- CDTOC_WORDS longwords from CDC_TgetToc; track -- 1..99
 | Returns: the frame address, or 0 if the track is absent, out of range, or
 |   the TOC's last-track record is unreadable
 ----------------------*/
uint32_t cdtoc_track_end(const uint32_t *toc, int track);

/*----------------------
 | cdtoc_max_audio_track
 | Description: Highest audio track number on the disc, walking only the range
 |   the TOC says exists so absent slots are never consulted. Returns 0 for a
 |   data-only disc, which is what the default HOTA_AUDIO=none build produces
 |   -- that 0 is what makes asking for music on such a disc a no-op instead
 |   of an undefined CD command.
 | Author: suinevere
 | Params: toc -- CDTOC_WORDS longwords from CDC_TgetToc
 | Returns: the track number, or 0 if the disc has no audio or the TOC is
 |   unreadable
 ----------------------*/
int cdtoc_max_audio_track(const uint32_t *toc);

#ifdef __cplusplus
}
#endif

#endif /* CDTOC_H */
```

- [ ] **Step 2: Write the failing test**

Create `saturn/tests/test_cdtoc.c`:

```c
/*----------------------
 | test_cdtoc.c
 | Description: Host unit tests for cdtoc.c. Built and run by run_tests.sh with
 |   the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: cdtoc.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "cdtoc.h"

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

#define CTRL_AUDIO   0x0
#define CTRL_DATA    0x4
#define CTRL_ABSENT  0xf

static uint32_t entry(int ctrl, uint32_t fad)
{
    return ((uint32_t)ctrl << 28) | (fad & 0x00ffffffu);
}

static uint32_t record(int ctrl, int track)
{
    return ((uint32_t)ctrl << 28) | ((uint32_t)track << 16);
}

/* This disc: TRACK 01 data, TRACK 02..42 audio, mirroring what
   HOTA_AUDIO=full lays down. Each track is given a distinct, easily checked
   frame address. */
static void build_hota_disc(uint32_t *toc)
{
    int t;

    for (t = 0; t < CDTOC_WORDS; t++) {
        toc[t] = 0xffffffffu;
    }

    toc[0] = entry(CTRL_DATA, 150);
    for (t = 2; t <= 42; t++) {
        toc[t - 1] = entry(CTRL_AUDIO, 1000u + (uint32_t)t * 100u);
    }

    toc[CDTOC_FIRST_WORD]   = record(CTRL_DATA, 1);
    toc[CDTOC_LAST_WORD]    = record(CTRL_AUDIO, 42);
    toc[CDTOC_LEADOUT_WORD] = entry(CTRL_AUDIO, 9000);
}

/* The default HOTA_AUDIO=none build: one data track and nothing else. */
static void build_data_only_disc(uint32_t *toc)
{
    int t;

    for (t = 0; t < CDTOC_WORDS; t++) {
        toc[t] = 0xffffffffu;
    }

    toc[0] = entry(CTRL_DATA, 150);
    toc[CDTOC_FIRST_WORD]   = record(CTRL_DATA, 1);
    toc[CDTOC_LAST_WORD]    = record(CTRL_DATA, 1);
    toc[CDTOC_LEADOUT_WORD] = entry(CTRL_DATA, 5300);
}

static void test_is_audio(void)
{
    uint32_t toc[CDTOC_WORDS];
    build_hota_disc(toc);

    CHECK_EQ(cdtoc_is_audio(toc, 1), 0);
    CHECK_EQ(cdtoc_is_audio(toc, 2), 1);
    CHECK_EQ(cdtoc_is_audio(toc, 33), 1);
    CHECK_EQ(cdtoc_is_audio(toc, 42), 1);

    /* Past the end of the disc: the entry reads 0xFFFFFFFF, control 0x0f. */
    CHECK_EQ(cdtoc_is_audio(toc, 43), 0);
    CHECK_EQ(cdtoc_is_audio(toc, 99), 0);

    CHECK_EQ(cdtoc_is_audio(toc, 0), 0);
    CHECK_EQ(cdtoc_is_audio(toc, -1), 0);
    CHECK_EQ(cdtoc_is_audio(toc, 100), 0);
    CHECK_EQ(cdtoc_is_audio(0, 2), 0);
}

static void test_track_start(void)
{
    uint32_t toc[CDTOC_WORDS];
    build_hota_disc(toc);

    CHECK_EQ(cdtoc_track_start(toc, 1), 150);
    CHECK_EQ(cdtoc_track_start(toc, 2), 1200);

    /* Engine music index 31, the first intro animation, maps to cue 33. */
    CHECK_EQ(cdtoc_track_start(toc, 33), 4300);
    CHECK_EQ(cdtoc_track_start(toc, 42), 5200);

    CHECK_EQ(cdtoc_track_start(toc, 43), 0);
    CHECK_EQ(cdtoc_track_start(toc, 0), 0);
    CHECK_EQ(cdtoc_track_start(toc, 100), 0);
    CHECK_EQ(cdtoc_track_start(0, 2), 0);
}

static void test_track_end(void)
{
    uint32_t toc[CDTOC_WORDS];
    build_hota_disc(toc);

    /* A track ends where the next one begins. */
    CHECK_EQ(cdtoc_track_end(toc, 33), cdtoc_track_start(toc, 34));
    CHECK_EQ(cdtoc_track_end(toc, 33), 4400);

    /* The last track ends at the lead-out, which is the only thing that can
       bound it -- there is no next track to read. */
    CHECK_EQ(cdtoc_track_end(toc, 42), 9000);

    CHECK_EQ(cdtoc_track_end(toc, 43), 0);
    CHECK_EQ(cdtoc_track_end(toc, 0), 0);
    CHECK_EQ(cdtoc_track_end(0, 2), 0);

    /* Every track must measure positive, or a resume would compute a
       negative-length playback range. */
    {
        int t;
        for (t = 2; t <= 42; t++) {
            CHECK(cdtoc_track_end(toc, t) > cdtoc_track_start(toc, t));
        }
    }
}

static void test_max_audio_track(void)
{
    uint32_t toc[CDTOC_WORDS];

    build_hota_disc(toc);
    CHECK_EQ(cdtoc_max_audio_track(toc), 42);

    build_data_only_disc(toc);
    CHECK_EQ(cdtoc_max_audio_track(toc), 0);

    CHECK_EQ(cdtoc_max_audio_track(0), 0);
}

static void test_unreadable_toc(void)
{
    uint32_t toc[CDTOC_WORDS];
    int t;

    /* No disc, or a read issued before the drive was ready: every longword
       reads back as absent, including the first/last records. Everything must
       answer "unknown" rather than inventing a frame address. */
    for (t = 0; t < CDTOC_WORDS; t++) {
        toc[t] = 0xffffffffu;
    }

    CHECK_EQ(cdtoc_max_audio_track(toc), 0);
    CHECK_EQ(cdtoc_is_audio(toc, 2), 0);
    CHECK_EQ(cdtoc_track_start(toc, 2), 0);
    CHECK_EQ(cdtoc_track_end(toc, 2), 0);
}

static void test_srl_offset_bug_regression(void)
{
    uint32_t toc[CDTOC_WORDS];
    int t;

    /* The bug this module exists to avoid: SRL::Cd::TableOfContents reads
       track t from longword 2t, because sizeof(TrackLocation) is 8 rather
       than 4. A 99-track disc populates both t-1 and 2t with distinct frame
       addresses, so a decoder with that fault returns the wrong one instead
       of failing to compile or reading absent data. */
    for (t = 0; t < CDTOC_WORDS; t++) {
        toc[t] = 0xffffffffu;
    }
    for (t = 1; t <= CDTOC_MAX_TRACK; t++) {
        toc[t - 1] = entry(CTRL_AUDIO, 10000u + (uint32_t)t);
    }
    toc[CDTOC_FIRST_WORD]   = record(CTRL_AUDIO, 1);
    toc[CDTOC_LAST_WORD]    = record(CTRL_AUDIO, CDTOC_MAX_TRACK);
    toc[CDTOC_LEADOUT_WORD] = entry(CTRL_AUDIO, 20000);

    CHECK_EQ(cdtoc_track_start(toc, 33), 10033);
    CHECK_EQ(cdtoc_track_start(toc, 33) != 10066, 1);
    CHECK_EQ(cdtoc_track_start(toc, 2), 10002);
    CHECK_EQ(cdtoc_track_start(toc, 99), 10099);
    CHECK_EQ(cdtoc_max_audio_track(toc), 99);
}

int main(void)
{
    test_is_audio();
    test_track_start();
    test_track_end();
    test_max_audio_track();
    test_unreadable_toc();
    test_srl_offset_bug_regression();

    if (g_fail != 0) {
        printf("%d cdtoc check(s) failed\n", g_fail);
        return 1;
    }

    printf("cdtoc: all checks passed\n");
    return 0;
}
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I saturn/src -o /tmp/run_tests_cdtoc saturn/tests/test_cdtoc.c saturn/src/cdtoc.c
```

Expected: FAIL — `cdtoc.c: No such file or directory`. (`cdtoc.h` exists from Step 1; the implementation does not.)

- [ ] **Step 4: Write the implementation**

Create `saturn/src/cdtoc.c`:

```c
/*----------------------
 | cdtoc.c
 | Description: Implementation of cdtoc.h. Every function takes the TOC buffer
 |   rather than owning one, so none of this needs a disc, an emulator or SRL
 |   to test -- see saturn/tests/test_cdtoc.c.
 | Author: suinevere
 | Dependencies: cdtoc.h
 ----------------------*/
#include "cdtoc.h"

/*----------------------
 | cdtoc_ctrl
 | Description: The control nibble of a TOC longword -- the high 4 bits of the
 |   ctrladr byte, which is itself the top byte.
 | Author: suinevere
 | Globals: N/A
 | Params: word -- one TOC longword
 | Returns: the nibble, 0..15
 ----------------------*/
static int cdtoc_ctrl(uint32_t word)
{
	return (int)((word >> 28) & 0xfu);
}

/*----------------------
 | cdtoc_fad
 | Description: The frame address packed into the low 24 bits of a TOC
 |   longword.
 | Author: suinevere
 | Globals: N/A
 | Params: word -- one TOC longword
 | Returns: the frame address
 ----------------------*/
static uint32_t cdtoc_fad(uint32_t word)
{
	return word & 0x00ffffffu;
}

/*----------------------
 | cdtoc_absent
 | Description: Whether a TOC entry describes nothing. The BIOS writes
 |   0xFFFFFFFF for tracks the disc does not have, and control 0x0f is the
 |   marker; treating such an entry as real yields a frame address of
 |   0xffffff, which is a seek off the end of the disc.
 | Author: suinevere
 | Globals: N/A
 | Params: word -- one TOC longword
 | Returns: 1 if the entry is absent, else 0
 ----------------------*/
static int cdtoc_absent(uint32_t word)
{
	return cdtoc_ctrl(word) == 0xf;
}

/*----------------------
 | cdtoc_record_track_no
 | Description: The track number carried in a first-track or last-track
 |   record, which store it in bits 16..23 rather than as a frame address.
 |   Returns 0 when the value is outside 1..99, which is what a TOC read
 |   before the drive was ready looks like.
 | Author: suinevere
 | Globals: N/A
 | Params: toc -- the TOC buffer; word -- CDTOC_FIRST_WORD or CDTOC_LAST_WORD
 | Returns: the track number, or 0 if unreadable
 ----------------------*/
static int cdtoc_record_track_no(const uint32_t *toc, int word)
{
	int n = (int)((toc[word] >> 16) & 0xffu);

	return (n >= 1 && n <= CDTOC_MAX_TRACK) ? n : 0;
}

int cdtoc_is_audio(const uint32_t *toc, int track)
{
	int ctrl;

	if (toc == 0 || track < 1 || track > CDTOC_MAX_TRACK)
	{
		return 0;
	}

	ctrl = cdtoc_ctrl(toc[track - 1]);

	return (ctrl != 0xf) && ((ctrl & 0x4) == 0);
}

uint32_t cdtoc_track_start(const uint32_t *toc, int track)
{
	if (toc == 0 || track < 1 || track > CDTOC_MAX_TRACK)
	{
		return 0;
	}

	if (cdtoc_absent(toc[track - 1]))
	{
		return 0;
	}

	return cdtoc_fad(toc[track - 1]);
}

uint32_t cdtoc_track_end(const uint32_t *toc, int track)
{
	int last;

	if (toc == 0 || track < 1 || track > CDTOC_MAX_TRACK)
	{
		return 0;
	}

	if (cdtoc_absent(toc[track - 1]))
	{
		return 0;
	}

	last = cdtoc_record_track_no(toc, CDTOC_LAST_WORD);

	if (last == 0)
	{
		return 0;
	}

	if (track >= last || cdtoc_absent(toc[track]))
	{
		return cdtoc_fad(toc[CDTOC_LEADOUT_WORD]);
	}

	return cdtoc_fad(toc[track]);
}

int cdtoc_max_audio_track(const uint32_t *toc)
{
	int first;
	int last;
	int track;
	int best = 0;

	if (toc == 0)
	{
		return 0;
	}

	first = cdtoc_record_track_no(toc, CDTOC_FIRST_WORD);
	last = cdtoc_record_track_no(toc, CDTOC_LAST_WORD);

	if (first == 0 || last == 0 || first > last)
	{
		return 0;
	}

	for (track = first; track <= last; track++)
	{
		if (cdtoc_is_audio(toc, track))
		{
			best = track;
		}
	}

	return best;
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I saturn/src -o /tmp/run_tests_cdtoc saturn/tests/test_cdtoc.c saturn/src/cdtoc.c && /tmp/run_tests_cdtoc
```

Expected: PASS — `cdtoc: all checks passed`, exit 0. If `-Werror` rejects a signed/unsigned comparison, fix the code, not the flags; `run_tests.sh` uses the same flags for the existing suites.

- [ ] **Step 6: Wire it into the test runner**

In `saturn/tests/run_tests.sh`, after the `run_tests_vm` block (line 17), append:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_cdtoc test_cdtoc.c ../src/cdtoc.c
./run_tests_cdtoc
```

- [ ] **Step 7: Run the whole suite**

Run: `sh saturn/tests/run_tests.sh`

Expected: all three suites pass, exit 0. The script is `set -e`, so any failure stops it.

- [ ] **Step 8: Confirm the Saturn build still links**

```bash
rm -f saturn/src/*.o
cd saturn && ./compile.bat
```

Expected: builds clean. `saturn/makefile` globs `find src/ -name '*.c'`, so `cdtoc.c` is picked up with no makefile edit — this step proves it compiles for SH-2, not just host gcc, before anything depends on it.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/cdtoc.h saturn/src/cdtoc.c saturn/tests/test_cdtoc.c saturn/tests/run_tests.sh
git commit -m "Decode the BIOS table of contents directly, since SRL's reads the wrong track."
```

---

### Task 2: Play a track, and refuse to on a disc that has none

Turns `disc_play_track` and `disc_stop_track` into real CD commands, gated by a TOC guard so the default data-only disc stays safe. Read coexistence is Task 3 — after this task the intro's music will be commanded and then immediately killed by the animation load, which is expected and is exactly what Task 3 fixes.

**Files:**
- Modify: `saturn/src/discfmt.h:20-25` (add the `extern "C"` guard)
- Modify: `saturn/src/system/disc_srl.cxx` — file banner, new includes, new state block, `disc_open`, `disc_play_track`, `disc_stop_track`, `disc_close`

**Interfaces:**
- Consumes from Task 1: `cdtoc_max_audio_track(const uint32_t *toc)`, `CDTOC_WORDS`.
- Consumes existing: `discfmt_cue_track_for_music(int engine_index)` from `discfmt.h`, returning `engine_index + 2` or 0 when out of range; `cls.nosound` from `client.h`; `g_discOpened` in this file.
- Produces for Task 3: file-static `g_toc[CDTOC_WORDS]`, `g_musicTrack` (engine index, −1 for none), `g_musicLoop`, and the private helper `static void cdda_halt(void)`.

- [ ] **Step 1: Add the `extern "C"` guard to `discfmt.h`**

`discfmt.h` has no guard — only `disc.h` got one in `7f66fe3`. `disc_srl.cxx` is C++ and is about to call into it, so without this the call looks for a mangled name and the link fails.

In `saturn/src/discfmt.h`, immediately after `#include <stddef.h>` (line 24), insert:

```c
/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link. */
#ifdef __cplusplus
extern "C" {
#endif
```

and immediately before the closing `#endif /* DISCFMT_H */` at the end of the file, insert:

```c
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Verify the guard by building both targets**

```bash
sh saturn/tests/run_tests.sh
rm -f saturn/src/*.o
cd saturn && ./compile.bat
```

Expected: both pass. `discfmt.h` is included by C and C++ translation units and by `tools/extract_disc`; this proves the guard broke none of them.

- [ ] **Step 3: Add includes and the state block to `disc_srl.cxx`**

After the existing includes at the top of `saturn/src/system/disc_srl.cxx` (`#include "disc.h"`), add:

```cpp
#include "discfmt.h"
#include "cdtoc.h"
#include "client.h"
```

Then, immediately after the existing `g_discOpened` definition and its banner, add:

```cpp
/*----------------------
 | g_toc / g_maxAudioTrack
 | Description: The disc's BIOS table of contents, fetched once by disc_open
 |   and decoded through cdtoc.h -- never through SRL::Cd::TableOfContents,
 |   which reads the wrong track (see cdtoc.h). g_maxAudioTrack is 0 on the
 |   data-only disc HOTA_AUDIO=none builds by default, and that 0 is what
 |   turns every music request on such a disc into a no-op instead of a
 |   CDC_CdPlay for a track the drive cannot find. No separate "is it fetched"
 |   flag: nothing reads either of these except through a live g_musicTrack,
 |   which only a successful disc_open can produce.
 | Author: suinevere
 ----------------------*/
static uint32_t g_toc[CDTOC_WORDS];
static int g_maxAudioTrack = 0;

/*----------------------
 | g_musicTrack / g_musicLoop
 | Description: The music this backend believes it is playing -- engine index,
 |   -1 for none, plus whether it was asked to repeat. The engine has no way
 |   to tell us what is playing and the CD block cannot be asked what it was
 |   asked for, so this is the only record of intent, and it is what lets a
 |   file read put the music back afterwards.
 | Author: suinevere
 ----------------------*/
static int g_musicTrack = -1;
static int g_musicLoop = 0;
```

- [ ] **Step 4: Add the halt helper**

Inside the `extern "C" {` block, above `disc_open`, add:

```cpp
/*----------------------
 | cdda_halt
 | Description: Stops CD-DA output. The seek is what silences it -- there is
 |   no stop command as such; SRL::Sound::Cdda::StopPause does the same two
 |   things, but also stashes the frame address into a private static that
 |   only its broken Resume consumes, so this port issues the pair itself.
 | Author: suinevere
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void cdda_halt(void)
{
	CdcPos pos;

	CDC_POS_PTYPE(&pos) = CDC_PTYPE_DFL;
	CDC_CdSeek(&pos);
}
```

- [ ] **Step 5: Fetch the TOC in `disc_open`**

In `disc_open`, immediately before its closing `g_discOpened = true;` — after the manifest check, so a disc that failed validation never records a track count — insert:

```cpp
	CDC_TgetToc(g_toc);
	g_maxAudioTrack = cdtoc_max_audio_track(g_toc);
	printf("disc_open: %d audio tracks\n", g_maxAudioTrack > 1 ? g_maxAudioTrack - 1 : 0);
```

Append to `disc_open`'s banner `Description`, before the closing line:

```
 |
 |   The TOC is read here, once, because it cannot change while a disc is
 |   mounted and because disc_play_track must not be the thing that discovers
 |   the disc has no audio -- on the default HOTA_AUDIO=none build that is
 |   every call.
```

- [ ] **Step 6: Implement `disc_play_track`**

Replace the whole existing `disc_play_track` function and its banner — the no-op that currently just discards both parameters — with:

```cpp
/*----------------------
 | disc_play_track
 | Description: Starts a CD-DA track for the engine's music index. Refuses,
 |   silently and by contract, before disc_open, after disc_close, with
 |   cls.nosound set, for an index outside 0..40, and -- the case that matters
 |   for every routine build -- for any track the disc does not carry, which
 |   is all of them on the 12 MB HOTA_AUDIO=none disc. That last guard is not
 |   defensive programming: CDC_CdPlay for a track outside the TOC is
 |   undefined, and the failure mode to avoid is one that leaves the CD block
 |   unable to serve the file reads the game is about to make.
 |
 |   A refusal leaves g_musicTrack untouched: whatever was already playing
 |   keeps playing and stays correctly tracked, matching disc_cue.c's host
 |   implementation, which runs every refusal check before it ever touches
 |   playback state. The two backends share one disc.h contract, and this is
 |   what keeps it literally true of both.
 |
 |   The +2 mapping is not repeated here. discfmt_cue_track_for_music owns it
 |   for both backends and returns 0, an invalid track, when out of range.
 |
 |   SRL::Sound::Cdda::PlaySingle is safe to use where the rest of Cdda is
 |   not: it is CDC_CdPlay by track number and never consults the table of
 |   contents SRL cannot read.
 | Author: suinevere
 | Globals: g_discOpened, g_maxAudioTrack, g_musicTrack, g_musicLoop
 | Params: engine_index -- music index 0..40; loop -- nonzero to repeat
 |   forever
 | Returns: N/A
 ----------------------*/
void disc_play_track(int engine_index, int loop)
{
	int cue;

	if (!g_discOpened || cls.nosound != 0)
	{
		return;
	}

	cue = discfmt_cue_track_for_music(engine_index);

	if (cue == 0 || cue > g_maxAudioTrack)
	{
		return;
	}

	SRL::Sound::Cdda::PlaySingle((uint16_t)cue, loop != 0);
	g_musicTrack = engine_index;
	g_musicLoop = loop;
}
```

- [ ] **Step 7: Implement `disc_stop_track`**

Replace the whole existing `disc_stop_track` function and its banner — currently an empty body — with:

```cpp
/*----------------------
 | disc_stop_track
 | Description: Stops the music and forgets it. Clearing g_musicTrack is the
 |   load-bearing half: it is what stops the next disc_read_file from putting
 |   back a track the engine deliberately silenced. Safe before disc_open,
 |   after disc_close, and with nothing playing, exactly as disc.h requires,
 |   which is what lets atexit_callback call it unconditionally.
 | Author: suinevere
 | Globals: g_musicTrack
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void disc_stop_track(void)
{
	if (g_musicTrack < 0)
	{
		return;
	}

	g_musicTrack = -1;
	cdda_halt();
}
```

- [ ] **Step 8: Clear music state in `disc_close`**

In `disc_close`, before `g_discOpened = false;`, insert:

```cpp
	disc_stop_track();
	g_maxAudioTrack = 0;
```

Append to `disc_close`'s banner `Description`:

```
 |
 |   Stopping the music here as well means a bare disc_close and the
 |   atexit stop-then-close pair land in the same state, and that a re-open
 |   cannot inherit a track request made against the previous disc.
```

- [ ] **Step 9: Update the file banner**

In `disc_srl.cxx`'s file banner, replace the sentence:

```
 |   max_size the same way the host backend does. CD-DA (disc_play_track/
 |   disc_stop_track) is a later sub-project; both are silent no-ops here,
 |   which disc.h's contract explicitly allows.
```

with:

```
 |   max_size the same way the host backend does. CD-DA plays through
 |   SRL::Sound::Cdda::PlaySingle and raw CDC_* calls -- not through
 |   Cdda::Resume or Cdda::StopPause, and not through
 |   SRL::Cd::TableOfContents, all of which read a table SRL lays out wrong
 |   (see cdtoc.h). There is one drive, so playback and file reads contend:
 |   disc_read_file brackets itself with a suspend and a restore, which is
 |   the only reason music survives the whole-file load that follows every
 |   disc_play_track in play_anm.
```

- [ ] **Step 10: Build both targets**

```bash
sh saturn/tests/run_tests.sh
make -C saturn/src
rm -f saturn/src/*.o
cd saturn && ./compile.bat
```

Expected: all pass. A link error naming `discfmt_cue_track_for_music` means Step 1's guard did not take — delete the objects and rebuild, since there is no header dependency tracking.

- [ ] **Step 11: Check the map for headroom**

```bash
grep -n "SRL::Sound::Cdda\|cdtoc_" "saturn/BuildDrop/Heart of the Alien (USA).map" | head
```

Expected: `cdtoc_*` and `SRL::Sound::Cdda::PlaySingle` appear. Compare the reported `.bss` figure against the previous build's: this task adds 408 bytes of TOC plus a handful of words, and nothing else should have moved.

- [ ] **Step 12: Regression-test the default disc**

The disc from Step 10 is the default `HOTA_AUDIO=none` build. Hand `saturn/BuildDrop/Heart of the Alien (USA).cue` to Suinevere and ask specifically: does it boot, does the intro render exactly as before, is there any hang or stall at an animation boundary?

Expected: identical to `3051d5c`, no music, no hang. This is acceptance criterion 3 and the regression that matters most — it is the disc every future build produces. **Do not run the emulator from a tool call.**

- [ ] **Step 13: Commit**

```bash
git add saturn/src/discfmt.h saturn/src/system/disc_srl.cxx
git commit -m "Play CD-DA tracks the disc actually carries, and refuse the ones it does not."
```

---

### Task 3: Survive the file reads

Adds the suspend/restore bracket that makes music coexist with `disc_read_file`, and with it the classification and loop rules. This is the task that makes the intro audible.

**Files:**
- Modify: `saturn/src/system/disc_srl.cxx` — new state, two helpers, `disc_read_file` split into a wrapper and a body, `disc_play_track` repeat guard
- Modify: `saturn/src/disc.h:94-116` (the `disc_play_track`/`disc_stop_track` banner)
- Modify: `saturn/makefile:1-13` (the `HOTA_AUDIO` comment)

**Interfaces:**
- Consumes from Task 1: `cdtoc_track_start(const uint32_t *toc, int track)`, `cdtoc_track_end(const uint32_t *toc, int track)`.
- Consumes from Task 2: `g_toc`, `g_musicTrack`, `g_musicLoop`, `cdda_halt()`, `discfmt_cue_track_for_music()`.
- Produces: nothing consumed by later tasks — this is the last one.

- [ ] **Step 1: Add the suspend state**

In `saturn/src/system/disc_srl.cxx`, after the `g_musicTrack` / `g_musicLoop` block from Task 2, add:

```cpp
/*----------------------
 | g_pauseFad / g_wasPlaying
 | Description: What the drive was doing when the last file read took it away.
 |   g_wasPlaying separates two states that look identical afterwards and need
 |   opposite treatment: a track genuinely interrupted mid-play, and a track
 |   commanded microseconds earlier that never got going because the read beat
 |   it to the drive -- which is what happens on every animation, since
 |   play_anm calls disc_play_track and then immediately reads a whole file.
 | Author: suinevere
 ----------------------*/
static uint32_t g_pauseFad = 0;
static bool g_wasPlaying = false;
```

- [ ] **Step 2: Add the suspend and restore helpers**

Inside the `extern "C" {` block, immediately after `cdda_halt`, add:

```cpp
/*----------------------
 | cdda_suspend
 | Description: Gives the drive up for a file read, remembering enough to put
 |   the music back. Reads the head position and play status before seeking,
 |   because the seek is what silences output and there is no way to ask
 |   afterwards where it had reached. A no-op when no music is wanted, which
 |   is what keeps the cost off every read on a data-only disc.
 | Author: suinevere
 | Globals: g_musicTrack, g_pauseFad, g_wasPlaying
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void cdda_suspend(void)
{
	CdcStat stat;

	if (g_musicTrack < 0)
	{
		return;
	}

	CDC_GetCurStat(&stat);
	g_wasPlaying = (CDC_GET_STC(&stat) == CDC_ST_PLAY);
	g_pauseFad = (uint32_t)CDC_STAT_FAD(&stat);

	cdda_halt();
}

/*----------------------
 | cdda_restore
 | Description: Puts the music back after a file read, choosing between three
 |   outcomes by where the head was when the read took over.
 |
 |   Not playing and at or past the track's end means the track finished on
 |   its own; restoring it would restart a one-shot minutes after it ended, so
 |   this forgets it instead. Playing, inside the track, and not looping means
 |   a genuine interruption, and the remainder is played as its own range so
 |   the listener hears the track continue rather than restart. Everything
 |   else -- the head below the track's start (the animation case, where
 |   playback never began), a looping track, or a table of contents that
 |   cannot answer -- restarts the track.
 |
 |   Looping tracks restart deliberately. A frame-range play is a one-shot, so
 |   resuming one would drop the repeat and leave the room silent once the
 |   remainder ended; restarting is audible, silence is not recoverable. The
 |   sibling port zaturn solved this with a per-frame music tick, which this
 |   seam has no equivalent of and does not want.
 |
 |   An unreadable TOC restarts for the same reason: restarting is a far
 |   better failure than silence.
 | Author: suinevere
 | Globals: g_toc, g_musicTrack, g_musicLoop, g_pauseFad, g_wasPlaying
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void cdda_restore(void)
{
	int cue;
	uint32_t start;
	uint32_t end;

	if (g_musicTrack < 0)
	{
		return;
	}

	cue = discfmt_cue_track_for_music(g_musicTrack);
	start = cdtoc_track_start(g_toc, cue);
	end = cdtoc_track_end(g_toc, cue);

	if (!g_wasPlaying && end != 0 && g_pauseFad >= end)
	{
		g_musicTrack = -1;
		return;
	}

	if (g_wasPlaying && g_musicLoop == 0 && end != 0 &&
		g_pauseFad >= start && g_pauseFad < end)
	{
		CdcPly ply;

		CDC_PLY_STYPE(&ply) = CDC_PTYPE_FAD;
		CDC_PLY_SFAD(&ply) = g_pauseFad;
		CDC_PLY_ETYPE(&ply) = CDC_PTYPE_FAD;
		CDC_PLY_EFAS(&ply) = end - g_pauseFad;
		CDC_PLY_PMODE(&ply) = CDC_PM_DFL;
		CDC_CdPlay(&ply);
		return;
	}

	SRL::Sound::Cdda::PlaySingle((uint16_t)cue, g_musicLoop != 0);
}
```

- [ ] **Step 3: Split `disc_read_file` into a body and a wrapper**

`disc_read_file` has six early returns. Bracketing it inline means six chances to miss a restore, so the existing body becomes a private function and the public one wraps it.

In `saturn/src/system/disc_srl.cxx`, rename the existing `int disc_read_file(const char *name, void *out, int max_size)` to:

```cpp
static int disc_read_file_body(const char *name, void *out, int max_size)
```

leaving its body **completely unchanged**, and change its banner's first line from `| disc_read_file` to `| disc_read_file_body`, appending to its `Description`:

```
 |
 |   Private because it must not be called without the CD-DA bracket around
 |   it; disc_read_file below is the only caller.
```

Then add, immediately after it:

```cpp
/*----------------------
 | disc_read_file
 | Description: disc_read_file_body with the drive handed over and taken back.
 |   There is one drive: a read seeks away from whatever CD-DA was playing and
 |   silences it, so every read is bracketed. The split exists because the
 |   body has six early returns and an inline bracket would need a restore on
 |   each -- a missed one leaves the music off for the rest of the session,
 |   with nothing to indicate why.
 |
 |   Both halves are no-ops when no music is wanted, so a data-only disc pays
 |   two branches per read and nothing else.
 | Author: suinevere
 | Globals: via cdda_suspend and cdda_restore
 | Params: name -- disc filename; out -- destination; max_size -- capacity of
 |   out in bytes
 | Returns: 0 on success, negative on failure
 ----------------------*/
int disc_read_file(const char *name, void *out, int max_size)
{
	int result;

	cdda_suspend();
	result = disc_read_file_body(name, out, max_size);
	cdda_restore();

	return result;
}
```

- [ ] **Step 4: Add the repeat guard to `disc_play_track`**

In `disc_play_track`, immediately after the `cue == 0 || cue > g_maxAudioTrack` block and before the `PlaySingle` call, insert:

```cpp
	if (g_musicTrack == engine_index && g_musicLoop != 0 && loop != 0)
	{
		CdcStat stat;

		CDC_GetCurStat(&stat);

		if (CDC_GET_STC(&stat) == CDC_ST_PLAY)
		{
			return;
		}
	}
```

and append to `disc_play_track`'s banner `Description`:

```
 |
 |   A request for the track the drive is confirmably already looping is
 |   dropped. Re-issuing CDC_CdPlay costs a seek and an audible gap, and "play
 |   T looping" while T loops is a no-op by definition. This is the one place
 |   this backend deliberately differs from src/host/disc_cue.c, where
 |   restarting an already-hooked stream is free. The status check is what
 |   makes it safe: a track that stopped for any reason we did not cause is
 |   started again rather than assumed to be running.
```

- [ ] **Step 5: Build both targets and run the suite**

```bash
sh saturn/tests/run_tests.sh
make -C saturn/src
rm -f saturn/src/*.o
cd saturn && ./compile.bat
```

Expected: all pass. `cdtoc`'s tests still pass unchanged — nothing in this task touches it.

Then confirm no forbidden SRL call slipped in anywhere in the port (spec acceptance criterion 5):

```bash
grep -rn "TableOfContents\|Cdda::Resume\|Cdda::StopPause" saturn/src
```

Expected: no matches. Any hit is a call into the table SRL lays out wrong, and it will read the wrong track rather than fail — replace it with the `cdtoc` equivalent before going further.

- [ ] **Step 6: Regression-test the default disc again**

Hand the `HOTA_AUDIO=none` disc from Step 5 to Suinevere: boots, intro renders, no music, no hang, no new stall at animation boundaries. The bracket now runs on every read, so this confirms it costs nothing when there is no music. **Do not run the emulator from a tool call.**

- [ ] **Step 7: Rewrite the `disc.h` seam banner**

The banner at `saturn/src/disc.h:94-116` describes `Mix_HookMusic` as though it were the implementation and tells a future reader that playback is a clean drop-in. It is not. Replace the `Description` body of the `disc_play_track / disc_stop_track` banner with:

```
 | Description: Starts and stops the disc's CD-DA music for the engine's
 |   music index, mapped to a cue track by discfmt_cue_track_for_music.
 |
 |   The two backends differ in kind, not just in API. src/host/disc_cue.c
 |   streams the raw track through Mix_HookMusic -- fread straight into the
 |   mixer's buffer, no decode, no resample -- and a concurrent
 |   disc_read_file cannot disturb it, because reads and playback are separate
 |   file handles. A Saturn has one drive: any read seeks away from the
 |   playing track and silences it, and main.c's play_anm calls
 |   disc_play_track and then immediately reads a whole animation file, so a
 |   backend that merely forwards these calls is silent for exactly the
 |   content the game has music for. saturn/src/system/disc_srl.cxx therefore
 |   owns playback state and brackets every read with a suspend and a
 |   restore. A third backend must do the same or accept silence.
 |
 |   disc_play_track requires the same precondition as disc_read_file (a
 |   disc_open that succeeded, no disc_close since) -- called any other time
 |   it is a silent no-op, same as calling it with cls.nosound set, or with a
 |   track the mounted disc does not carry. Call it any number of times; it
 |   always supersedes whatever was playing, except for a request identical to
 |   what the drive is confirmably already looping, which the Saturn backend
 |   drops to avoid a needless seek. disc_stop_track is always safe, including
 |   before disc_open, after disc_close, or with nothing playing, which is why
 |   atexit_callback can call it unconditionally right before disc_close.
```

- [ ] **Step 8: Correct the `HOTA_AUDIO` comment**

`saturn/makefile:5-7` currently reads "every compile.bat would otherwise re-lay 41 audio tracks for a build that never plays them", which stops being true with this task. Replace those three lines with:

```
# Default none on purpose: the tracks are ~425 MB and only the music needs
# them, so a build that is iterating on video, input or gameplay should not
# re-lay 41 of them. The Saturn backend's TOC guard makes a data-only disc
# silently music-free rather than undefined, so this is safe to leave at none.
# Flip to full for a release or to test music.
```

- [ ] **Step 9: Verify the `tracklist` before trusting any listening test**

```bash
grep -vc '^#' saturn/cd/music/tracklist
ls saturn/cd/music/*.raw | wc -l
```

Expected: `41` and `41`. If they disagree, stop — `shared.mk` numbers tracks sequentially from 2 in the order this file lists, so a short or resorted list plays a different song for every cue with no error, and any listening test after that is measuring the wrong thing.

- [ ] **Step 10: Build the music disc**

```bash
rm -f saturn/src/*.o
cd saturn && HOTA_AUDIO=full ./compile.bat
```

Expected: `saturn/BuildDrop/Heart of the Alien (USA).cue` with 42 tracks and a `.bin` of roughly 425 MB. Confirm the track count before handing it over:

```bash
grep -c '^  TRACK' "saturn/BuildDrop/Heart of the Alien (USA).cue"
```

Expected: `42`.

- [ ] **Step 11: Have Suinevere verify the music**

Hand over the path and ask for these four, specifically:

1. Each of the four intro animations plays with music under it.
2. The music is present from near the start of each animation — not missing for the first seconds, which would mean the restore is not firing.
3. No stutter, restart or gap partway through an animation.
4. Silence after the intro ends.

**Do not run the emulator from a tool call.** If music is absent on all four, the likely causes in order are: the TOC guard rejecting valid tracks (check the `disc_open: N audio tracks` line reads 41), `cdda_restore` classifying as finished (`g_pauseFad >= end` with a stale FAD), or Mednafen not modelling the contention. Add a `printf` in `cdda_restore` reporting which branch it took and rebuild — instrument before theorising; that is what settled the first-boot panic in one round after three wrong theories.

- [ ] **Step 12: Commit**

```bash
git add saturn/src/system/disc_srl.cxx saturn/src/disc.h saturn/makefile
git commit -m "Hand the drive over for file reads and put the music back afterwards."
```

---

## What this plan does not cover

Stated so the next reader does not mistake absence for oversight:

- **Sound effects.** `play_sample` and `sound_flush_cache` stay no-ops in `sound_srl.cxx`. Its own sub-project.
- **Anything past the intro.** Rooms and gameplay have never run on Saturn, so the loop-restart rule and mid-room loads are reasoned about, not proven.
- **Hardware.** Every finding this design rests on came from `zaturn` on real hardware; verification here is Mednafen only. "Works in Mednafen" is not "works".
- **The `fprintf` silent-failure bug.** Avoided, not investigated.
