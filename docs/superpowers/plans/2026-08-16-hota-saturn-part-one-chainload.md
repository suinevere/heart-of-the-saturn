# Part I Chain-load Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the boot menu's *OUT OF THIS WORLD* entry launch Another-Saturn's program from a disc that carries both games.

**Architecture:** Heart of the Alien stays the disc's first-read program. Both games link to `0x06004000`, so Part I is loaded *over* us: `chainload_run()` fades to black, stages `ANOTHER.BIN` in LWRAM, quiesces the hardware, and jumps through a seven-instruction position-independent trampoline that copies the image via the uncached HWRAM mirror. Every decision about *whether* to do this lives in pure, host-tested C in `bootmenu.c`; the hardware seam carries none.

**Tech Stack:** SaturnRingLib (SGL/SH-2 cross), C99 + C++ seams, host gcc for tests, `xorriso`/`iso2raw` for disc authoring, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-16-hota-saturn-part-one-chainload-design.md`

## Global Constraints

- **Never modify `../Another-Saturn` or anything under `.another/`.** The glue is one-directional. Its program is consumed exactly as its own build emits it.
- **Every method, constant and file gets a banner comment** in the repo's form (`| name`, `| Description:`, `| Author: suinevere`, `| Dependencies:`, `| Globals:`, `| Params:`, `| Returns:`, `N/A` where inapplicable). Tests get a file banner only.
- **No comments inside function bodies.** Prose is one sentence; say the non-obvious thing once.
- **`bootmenu.{h,c}` must not include `srl.hpp`, any `sega_*.h`, or any engine header.** It is compiled by host gcc in `run_tests.sh`; an include breaks that and is the whole reason the file is shaped this way.
- **New C files use `.c`, new C++ files use `.cxx`.** `saturn/makefile` globs only those two; a `.cpp` would be silently dropped from the link.
- **Host tests build with** `gcc -std=c99 -Wall -Wextra -Werror -O1 -g`.
- **Polyglot `.bat` files stay LF-only** and use no `\` line continuations in the POSIX half — the next line's `:;` prefix would splice in.
- **Commit messages are one sentence.** No body, no bullets, no trailers, no mention of tooling or sessions.
- **Author of record is suinevere.**

---

## File Structure

| File | Responsibility |
|---|---|
| `saturn/src/bootmenu.h` | + `part1_available`/`part2_available` state, `start_part1` frame flag, new `bootmenu_init` signature |
| `saturn/src/bootmenu.c` | Confirm gate becomes two arms, each on its own flag |
| `saturn/tests/test_bootmenu.c` | Cases for both flags in all four combinations |
| `saturn/src/disc.h` | + `disc_part2_available()` declaration |
| `saturn/src/system/disc_srl.cxx` | + `disc_part2_available()` over the existing `DISC_MANIFEST_LIST` X-macro |
| `saturn/src/system/chainload.h` | Two-function contract: probe and jump |
| `saturn/src/system/chainload.cxx` | The SRL seam — stage, quiesce, trampoline, jump |
| `saturn/src/main.c` | `boot_sequence()` passes both flags in and handles `start_part1` |
| `tools/another/CONFIG.ME` | `PART1_REPO`, `PART1_REF` |
| `tools/another/fetch.sh` | Clone at pinned ref, `make convert_binary`, copy allowlist |
| `saturn/makefile` | `HOTA_PART1` opt-in, `part1_assets` before the include, `part1_clean` after |
| `.gitignore` | `bank*`, `BANK*`, `OPENING.CPK`, `/.another/` |
| `tools/assets/update.bat` | Optional Part I data step in both halves |
| `.github/workflows/release.yml` | Kit assembly, dual leak assertion, smoke test |
| `.github/workflows/full-image.yml` | Manual, artifact-only, both data sets |

---

## Task 1: Boot menu availability flags

**Files:**
- Modify: `saturn/src/bootmenu.h:129-150` (structs), `saturn/src/bootmenu.h:163` (`bootmenu_init` decl)
- Modify: `saturn/src/bootmenu.c:43-51` (`bootmenu_init`), `saturn/src/bootmenu.c:62` and `:93-98` (`bootmenu_step`)
- Test: `saturn/tests/test_bootmenu.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `void bootmenu_init(bootmenu_state *st, uint32_t now_ms, int part1_available, int part2_available);` and `boot_frame.start_part1` (int, 0 or 1). Task 4 calls both.

- [ ] **Step 1: Write the failing tests**

In `saturn/tests/test_bootmenu.c`, add a parameterised menu helper beside the existing `at_menu` (which stays, now meaning "both games present") and four new cases:

```c
static void at_menu_with(int part1, int part2)
{
    boot_frame f;
    bootmenu_init(&g_menu_st, 0u, part1, part2);
    bootmenu_step(&g_menu_st, 0u, 0u, &f);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS, 0u, &f);
}

static void test_part_one_starts_when_available(void)
{
    boot_frame f;
    at_menu_with(1, 1);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_A, &f);
    expect_int("confirming OUT OF THIS WORLD asks for Part I", f.start_part1, 1);
    expect_int("and does not start Part II", f.start_game, 0);
}

static void test_part_one_inert_when_unavailable(void)
{
    boot_frame f;
    at_menu_with(0, 1);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_A, &f);
    expect_int("an absent Part I does not launch", f.start_part1, 0);
    expect_int("and does not fall through to Part II", f.start_game, 0);
    expect_int("but the entry still lights",
               (int)f.highlight, (int)BOOT_ENTRY_OUT_OF_THIS_WORLD);
    expect_screen("and the menu stays up", f.screen, BOOT_SCREEN_MENU);
}

static void test_part_two_inert_when_unavailable(void)
{
    boot_frame f;
    at_menu_with(1, 0);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_DOWN, &f);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 200u, BOOT_KEY_B, &f);
    expect_int("an absent Part II does not start the game", f.start_game, 0);
    expect_int("and does not launch Part I instead", f.start_part1, 0);
    expect_int("but the entry still lights",
               (int)f.highlight, (int)BOOT_ENTRY_HEART_OF_THE_ALIEN);
}

static void test_neither_game_present(void)
{
    boot_frame f;
    at_menu_with(0, 0);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 100u, BOOT_KEY_A, &f);
    expect_int("no Part I", f.start_part1, 0);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 200u, BOOT_KEY_DOWN, &f);
    bootmenu_step(&g_menu_st, BOOT_OPENING_MS + 300u, BOOT_KEY_C, &f);
    expect_int("no Part II", f.start_game, 0);
    expect_screen("the menu is still drawable", f.screen, BOOT_SCREEN_MENU);
}
```

Rewrite the existing `at_menu` to delegate, so every other test keeps its current meaning:

```c
static void at_menu(void)
{
    at_menu_with(1, 1);
}
```

Replace the old `test_part_one_cannot_be_confirmed` body — its premise is now conditional — leaving the function deleted and its four replacements registered. In `main()`, remove the `test_part_one_cannot_be_confirmed();` line and add:

```c
    test_part_one_starts_when_available();
    test_part_one_inert_when_unavailable();
    test_part_two_inert_when_unavailable();
    test_neither_game_present();
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cd saturn/tests && bash run_tests.sh
```

Expected: FAIL — compile error, `too many arguments to function 'bootmenu_init'` and `'boot_frame' has no member named 'start_part1'`.

- [ ] **Step 3: Extend the header**

In `saturn/src/bootmenu.h`, add to `bootmenu_state` after `music_started`:

```c
    int      part1_available;
    int      part2_available;
```

Add to `boot_frame` after `start_game`:

```c
    int         start_part1;
```

Update `boot_frame`'s banner to name the distinction:

```c
/*----------------------
 | boot_frame
 | Description: What the caller should do this frame. start_game and
 |   start_part1 are mutually exclusive: one confirm can only choose one game.
 | Author: suinevere
 ----------------------*/
```

Replace `bootmenu_init`'s declaration and banner:

```c
/*----------------------
 | bootmenu_init
 | Description: Starts the sequence at the first opening still, with the
 |   cursor on OUT OF THIS WORLD to match the capture's first menu frame.
 |
 |   An unavailable game's entry still lights and still moves the cursor; only
 |   confirming it is refused. The original's menu has no greyed-out state and
 |   the capture the art is cropped from cannot supply one.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to initialise; now_ms -- the clock's current reading,
 |         which need not be zero; part1_available -- non-zero if Part I's
 |         program is on the disc; part2_available -- non-zero if Part II's
 |         blobs are
 | Returns: N/A
 ----------------------*/
void bootmenu_init(bootmenu_state *st, uint32_t now_ms,
                   int part1_available, int part2_available);
```

- [ ] **Step 4: Extend the implementation**

In `saturn/src/bootmenu.c`, replace `bootmenu_init`:

```c
void bootmenu_init(bootmenu_state *st, uint32_t now_ms,
                   int part1_available, int part2_available)
{
    st->phase_start_ms = now_ms;
    st->music_start_ms = now_ms;
    st->idle_start_ms = now_ms;
    st->in_menu = 0;
    st->highlight = (int)BOOT_ENTRY_OUT_OF_THIS_WORLD;
    st->music_started = 0;
    st->part1_available = part1_available;
    st->part2_available = part2_available;
}
```

In `bootmenu_step`, add beside the existing `out->start_game = 0;`:

```c
    out->start_part1 = 0;
```

Replace the confirm gate:

```c
        if ((pressed & BOOT_KEY_CONFIRM) != 0u)
        {
            if (highlight_before_move == (int)BOOT_ENTRY_HEART_OF_THE_ALIEN
                && st->part2_available)
            {
                out->start_game = 1;
            }
            else if (highlight_before_move == (int)BOOT_ENTRY_OUT_OF_THIS_WORLD
                     && st->part1_available)
            {
                out->start_part1 = 1;
            }
        }
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cd saturn/tests && bash run_tests.sh
```

Expected: PASS — `bootmenu: all checks passed`, and every other suite still green.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/bootmenu.h saturn/src/bootmenu.c saturn/tests/test_bootmenu.c
git commit -m "Gate each boot menu entry on its own game's presence, so an absent Part I or Part II lights and moves but refuses to confirm rather than launching into a disc that cannot serve it."
```

---

## Task 2: Fetch and build Part I into the CD skeleton

**Files:**
- Create: `tools/another/CONFIG.ME`, `tools/another/fetch.sh`
- Modify: `saturn/makefile`, `.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: `saturn/cd/data/ANOTHER.BIN` and `saturn/cd/data/OPENING.CPK` when built with `HOTA_PART1=1`. Task 4's `chainload_available()` probes for the first by that exact name.

- [ ] **Step 1: Write the config**

Create `tools/another/CONFIG.ME`:

```
# Part I (Another-Saturn) source pin.
# KEY=VALUE, one per line. Lines starting with '#' are ignored.
# Relative paths resolve against this file's own directory, not the caller's cwd.

# Upstream repository. Nothing in it knows about this project; the glue is
# entirely on our side, and this pin is the whole of the coupling.
PART1_REPO=https://github.com/suinevere/Another-Saturn.git

# Pinned commit. Bumped deliberately -- CI builds what this says, so upstream
# cannot break a release here without a commit in this repository.
PART1_REF=4b73deb

# Where the checkout lands. Gitignored; safe to delete at any time.
PART1_DIR=../../.another/Another-Saturn
```

- [ ] **Step 2: Write the fetch script**

Create `tools/another/fetch.sh`:

```sh
#!/bin/sh
# Builds Part I's program into our CD skeleton. Follows tools/build.sh's
# convention -- plain sh, invoked as `sh ../tools/another/fetch.sh`, because
# the makefile already calls its helpers that way and this is never
# double-clicked the way the asset kit's polyglot .bat files are.
#
# Deliberately does NOT run Another-Saturn's data.bat: the engine build must
# carry no gated data, the same discipline its own release.yml keeps. Its
# bank files reach our disc through the setup kit, not through this script.
set -eu
cd "$(dirname "$0")"

cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }
PART1_REPO=$(cfg PART1_REPO)
PART1_REF=$(cfg PART1_REF)
PART1_DIR=$(cfg PART1_DIR)

SRL_HERE=$(cd ../../SaturnRingLib && pwd)
DEST=$(cd ../../saturn/cd/data && pwd)

if [ ! -d "$PART1_DIR/.git" ]; then
    mkdir -p "$(dirname "$PART1_DIR")"
    git clone --recurse-submodules "$PART1_REPO" "$PART1_DIR"
fi

git -C "$PART1_DIR" fetch --tags origin
git -C "$PART1_DIR" checkout --detach "$PART1_REF"
git -C "$PART1_DIR" submodule update --init --recursive

# convert_binary, not all: shared.mk defines it as `convert_binary :
# compile_objects` and its recipe objcopies the ELF straight to cd/data/0.bin.
# Stopping there means Part I never needs xorrisofs, iso2raw or a music pass,
# and never authors a second disc image we would only throw away.
#
# COMPILER_DIR is passed on the command line so it overrides shared.mk's
# `COMPILER_DIR=$(SDK_ROOT)/../Compiler`, pointing Part I at the SH-2 toolchain
# we already fetched. The SDK checkout must stay separate -- shared.mk compiles
# preloader.cxx to modules/sgl/SRC/preloader.o inside the SDK tree, and our
# SGL_MAX_POLYGONS of 256 against Part I's 1500 would otherwise share one
# object that make declines to rebuild.
( cd "$PART1_DIR/saturn" && make convert_binary \
    SRL_INSTALL_ROOT=../SaturnRingLib \
    COMPILER_DIR="$SRL_HERE/Compiler" )

SRC="$PART1_DIR/saturn/cd/data"
cp -f "$SRC/0.bin"        "$DEST/ANOTHER.BIN"
cp -f "$SRC/OPENING.CPK"  "$DEST/OPENING.CPK"

echo "Part I: ANOTHER.BIN $(wc -c < "$DEST/ANOTHER.BIN") bytes, OPENING.CPK $(wc -c < "$DEST/OPENING.CPK") bytes"
```

An explicit two-file copy, not `cp -r`: `REFAUD.CPK` is a 7.7 MB reference encode behind a compile switch at their `opening.cxx:63`, `OPENING.BIN` is untracked scratch, and `SDDRVS.*`, `BOOTSND.MAP` and the `.TXT` files are ours to write.

- [ ] **Step 3a: Make the asset self-repair suppressible**

`saturn/makefile` currently hangs `data.bat` off every build as one-click self-repair:

```make
all: game_assets

.PHONY: game_assets
game_assets:
	@[ -x ../tools/extract_disc ] || [ -x ../tools/extract_disc.exe ] || sh ../tools/build.sh
	@sh ../tools/assets/data.bat
```

That fetches the Sega CD rip and installs the 19 blobs. Task 6's release build must not do
that — it would ship data that is not ours and trip its own leak assertion. Add the switch
and guard the prerequisite, keeping the existing recipe and its comment intact:

```make
# The self-repair is right for a checkout and wrong for a release build, which
# must author a disc with no game data in it at all. Defaults on, so nothing
# about a normal build changes; release.yml passes 0.
HOTA_ASSETS ?= 1

ifeq ($(HOTA_ASSETS),1)
all: game_assets
endif
```

Leave the `.PHONY: game_assets` target itself exactly as it is — only its place in `all`'s
prerequisites is conditional. `full-image.yml` leaves the default alone, because that build
does want the data.

- [ ] **Step 3b: Wire Part I into the makefile**

In `saturn/makefile`, immediately after the block from Step 3a and **before** the `include`, add:

```make
# Part I's program, when asked for. Declared before the include for the reason
# game_assets is: make records prerequisites in the order it first sees them,
# and shared.mk's own `all:` lines would otherwise schedule the fetch after the
# xorrisofs run that consumes what it produces.
#
# Opt-in rather than default: it clones and builds a second project, which is
# not what somebody iterating on this one wants on every make.
HOTA_PART1 ?= 0

all: part1_assets

.PHONY: part1_assets
part1_assets:
ifeq ($(HOTA_PART1),1)
	@sh ../tools/another/fetch.sh
endif
```

After the include, add:

```make
.PHONY: part1_clean
part1_clean:
	rm -f $(ASSETS_DIR)/ANOTHER.BIN $(ASSETS_DIR)/OPENING.CPK

clean: part1_clean
```

- [ ] **Step 4: Extend `.gitignore`**

Append to the non-free game data section:

```
# Part I's PC DOS data, installed by the setup kit's part1 step. Both cases:
# Another-Saturn's bank.cxx builds the name lower-case with sprintf("bank%02x")
# and ISO9660 stores it upper-case, so either spelling can appear depending on
# where it was copied from, and git's ignore matching is case-sensitive
# wherever core.ignorecase is false, such as CI on Linux. ANOTHER.BIN and
# MEMLIST.BIN are already covered by the saturn/cd/data/*.BIN rule above.
saturn/cd/data/BANK*
saturn/cd/data/bank*

# Part I's opening movie. Committed in Another-Saturn, copied in here by
# tools/another/fetch.sh -- build output on this side of the fence.
saturn/cd/data/OPENING.CPK
```

And to the build-output section:

```
# --- Part I checkout ---------------------------------------------------------
# tools/another/fetch.sh clones Another-Saturn here at the pinned ref, with its
# own SaturnRingLib. Never committed; safe to delete.
/.another/
```

- [ ] **Step 5: Verify the disc carries Part I**

```bash
cd saturn && make all HOTA_PART1=1
xorriso -indev "BuildDrop/Heart of the Alien (USA).iso" -find / 2>/dev/null | grep -E "ANOTHER|OPENING"
```

Expected: `'/ANOTHER.BIN'` and `'/OPENING.CPK'` both listed. Confirm `git status --short` shows no new untracked files under `saturn/cd/data` or at the root.

- [ ] **Step 6: Verify the default build is unchanged**

```bash
cd saturn && make clean && bash compile.bat release
xorriso -indev "BuildDrop/Heart of the Alien (USA).iso" -find / 2>/dev/null | grep -cE "ANOTHER|OPENING"
```

Expected: `0`. The opt-in default leaves today's disc exactly as it was.

- [ ] **Step 7: Commit**

```bash
git add tools/another/CONFIG.ME tools/another/fetch.sh saturn/makefile .gitignore
git commit -m "Build Part I's program into the CD skeleton from a pinned Another-Saturn ref, using its own SaturnRingLib checkout so two projects' polygon ceilings cannot share one preloader object, and copying in only the program and its opening movie."
```

---

## Task 3: Part II availability probe

**Files:**
- Modify: `saturn/src/disc.h` (declaration beside `disc_open` at `:75`)
- Modify: `saturn/src/system/disc_srl.cxx` (new function beside the `DISC_MANIFEST_LIST` use at `:635`)

**Interfaces:**
- Consumes: nothing.
- Produces: `int disc_part2_available(void);` — returns non-zero when every blob in `DISC_MANIFEST_LIST` exists at its expected size. Task 4 calls it.

- [ ] **Step 1: Declare it**

In `saturn/src/disc.h`, after `disc_open`'s declaration:

```c
/*----------------------
 | disc_part2_available
 | Description: Reports whether this disc carries Heart of the Alien's own
 |   data, without failing if it does not. Same manifest disc_open checks, and
 |   deliberately the same test -- a blob of the wrong size is as unplayable as
 |   an absent one, and reporting it available would trade a menu that says so
 |   for a crash partway into the game.
 |
 |   Answers a question about our data, so it lives here rather than in
 |   chainload.h: this file's implementation is the only one that knows the
 |   blob list.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: non-zero when every blob is present and correctly sized
 ----------------------*/
int disc_part2_available(void);
```

- [ ] **Step 2: Implement it**

In `saturn/src/system/disc_srl.cxx`, after `disc_open`:

```cpp
/*----------------------
 | disc_part2_available
 | Description: Walks DISC_MANIFEST_LIST and reports whether all of it is
 |   there. Requires disc_open to have run, since GFS must be up for
 |   SRL::Cd::File to resolve a name.
 | Author: suinevere
 | Dependencies: disc_manifest.h, srl.hpp
 | Globals: N/A
 | Params: N/A
 | Returns: 1 when every blob is present and correctly sized, 0 otherwise
 ----------------------*/
int disc_part2_available(void)
{
	int bad = 0;

#define DISC_MANIFEST_PROBE(name, lba, size)                     \
	{                                                             \
		SRL::Cd::File probeFile(name);                           \
		if (!probeFile.Exists()                                  \
		    || probeFile.Size.Bytes != (int32_t)(size))          \
		{                                                         \
			bad++;                                               \
		}                                                         \
	}

	DISC_MANIFEST_LIST(DISC_MANIFEST_PROBE)
#undef DISC_MANIFEST_PROBE

	return bad == 0;
}
```

- [ ] **Step 3: Verify it compiles**

```bash
cd saturn && make all HOTA_PART1=1
```

Expected: clean build, no new warnings.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/disc.h saturn/src/system/disc_srl.cxx
git commit -m "Ask the disc whether Heart of the Alien's own blobs are present without failing when they are not, reusing the manifest list disc_open already walks."
```

---

## Task 4: The chain-load seam

**Files:**
- Create: `saturn/src/system/chainload.h`, `saturn/src/system/chainload.cxx`
- Modify: `saturn/src/main.c` (`boot_sequence()`, around `:1313` and `:1352`)

**Interfaces:**
- Consumes: `bootmenu_init(st, now, p1, p2)` and `boot_frame.start_part1` (Task 1); `disc_part2_available()` (Task 3); `ANOTHER.BIN` on the disc (Task 2).
- Produces: `int chainload_available(void);` and `void chainload_run(void);`. Nothing consumes these — this task closes the loop.

- [ ] **Step 1: Write the header**

Create `saturn/src/system/chainload.h`:

```c
/*----------------------
 | chainload.h
 | Description: Hands the console to Part I. Both programs link to 0x06004000
 |   (sgl.linker's PRELOADER section), so Part I is not loaded beside us but
 |   over us, and there is no way back short of a console reset. There is no
 |   BOOT ROM service for this: SYS_EXECDMP is the crash dumper and SYS_Exit
 |   returns to the shell, which re-boots our own first-read file.
 |
 |   Design: docs/superpowers/specs/2026-08-16-hota-saturn-part-one-chainload-design.md
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef CHAINLOAD_H
#define CHAINLOAD_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CHAINLOAD_IMAGE
 | Description: Part I's program on the disc, written there by
 |   tools/another/fetch.sh from Another-Saturn's own cd/data/0.bin.
 | Author: suinevere
 ----------------------*/
#define CHAINLOAD_IMAGE "ANOTHER.BIN"

/*----------------------
 | chainload_available
 | Description: Reports whether Part I's program is on this disc. One name
 |   lookup, called once before the first still is drawn while the drive is
 |   otherwise idle.
 | Author: suinevere
 | Dependencies: srl.hpp
 | Globals: N/A
 | Params: N/A
 | Returns: non-zero when CHAINLOAD_IMAGE exists
 ----------------------*/
int chainload_available(void);

/*----------------------
 | chainload_run
 | Description: Fades out, loads Part I over this program and jumps to it.
 |   Never returns on success.
 |
 |   Returns only on a staging failure, which is the last point at which
 |   returning is possible: nothing has been overwritten and no hardware has
 |   been torn down yet. Past that point failure is neither recoverable nor
 |   detectable, which is why the quiesce steps are short, ordered, and
 |   independent of each other -- a failure is found by removing them one at a
 |   time rather than by rewriting this.
 | Author: suinevere
 | Dependencies: srl.hpp, disc.h, video.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A on success; returns to the caller only if staging failed
 ----------------------*/
void chainload_run(void);

#ifdef __cplusplus
}
#endif

#endif /* CHAINLOAD_H */
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/system/chainload.cxx`. The trampoline is seven hand-encoded SH-2 instructions because a compiled function cannot be relocated — SH-2 loads 32-bit constants from a PC-relative literal pool, which breaks the moment the code moves. Passing all four addresses in `r4`-`r7` per the SH ABI leaves nothing for a literal pool to hold.

```cpp
/*----------------------
 | chainload.cxx
 | Description: The SRL seam for handing the console to Part I. Carries no
 |   decisions -- bootmenu.c decides whether this runs, which is what keeps
 |   that decision host-testable when none of this is.
 | Author: suinevere
 | Dependencies: srl.hpp, chainload.h, disc.h, video.h
 | Globals: N/A
 ----------------------*/
extern "C" {
#include "chainload.h"
#include "disc.h"
#include "sound.h"
#include "video.h"
}

#include <srl.hpp>
#include <sgl.h>
#include <sega_gfs.h>
#include <sega_sys.h>

/*----------------------
 | CHAINLOAD_ENTRY
 | Description: Where both programs link. sgl.linker places PRELOADER here.
 | Author: suinevere
 ----------------------*/
#define CHAINLOAD_ENTRY 0x06004000u

/*----------------------
 | CHAINLOAD_UNCACHED
 | Description: Added to an address to reach its uncached mirror. The copy
 |   writes Part I through this so the SH-2 instruction cache cannot serve a
 |   stale line of our own code over an address that now holds Part I's, and
 |   the trampoline executes through it for the same reason -- it was written
 |   into LWRAM moments earlier through a cached address.
 | Author: suinevere
 ----------------------*/
#define CHAINLOAD_UNCACHED 0x20000000u

/*----------------------
 | g_trampoline
 | Description: Copy-and-jump, hand-encoded because it must run from LWRAM
 |   while HWRAM is being overwritten, and a compiled function cannot be
 |   relocated -- SH-2 reads 32-bit constants from a PC-relative literal pool
 |   that does not travel with the code. Every address arrives in a register
 |   (r4 src, r5 dst, r6 longword count, r7 entry) so there is no pool at all.
 |
 |       6046  mov.l  @r4+, r0
 |       2502  mov.l  r0, @r5
 |       7504  add    #4, r5
 |       4610  dt     r6
 |       8bfa  bf     -6        ; back to mov.l @r4+, r0
 |       472b  jmp    @r7
 |       0009  nop              ; jmp's delay slot
 | Author: suinevere
 ----------------------*/
static const unsigned short g_trampoline[7] = {
	0x6046, 0x2502, 0x7504, 0x4610, 0x8bfa, 0x472b, 0x0009
};

typedef void (*chainload_fn)(const void *src, void *dst,
                             unsigned long longwords, void *entry);

int chainload_available(void)
{
	SRL::Cd::File image(CHAINLOAD_IMAGE);
	return image.Exists() ? 1 : 0;
}

void chainload_run(void)
{
	SRL::Cd::File image(CHAINLOAD_IMAGE);

	if (!image.Exists())
	{
		return;
	}

	int bytes = (int)image.Size.Bytes;
	unsigned long longwords = ((unsigned long)bytes + 3ul) / 4ul;

	void *staged = SRL::Memory::LowWorkRam::Malloc((size_t)(longwords * 4ul));

	if (staged == 0)
	{
		return;
	}

	void *tramp = SRL::Memory::LowWorkRam::Malloc(sizeof(g_trampoline));

	if (tramp == 0)
	{
		SRL::Memory::LowWorkRam::Free(staged);
		return;
	}

	video_set_fade(0);
	disc_stop_track();

	if (image.LoadBytes(0, bytes, staged) != bytes)
	{
		SRL::Memory::LowWorkRam::Free(tramp);
		SRL::Memory::LowWorkRam::Free(staged);
		return;
	}

	for (unsigned int i = 0; i < sizeof(g_trampoline) / sizeof(g_trampoline[0]); i++)
	{
		((unsigned short *)tramp)[i] = g_trampoline[i];
	}

	GFS_Reset();
	slSlaveFunc(NULL, NULL);
	sound_done();
	SYS_SETSCUIM(0xffffffffu);
	__asm__ __volatile__("ldc %0, sr" :: "r"(0x000000f0u));

	chainload_fn go = (chainload_fn)((unsigned long)tramp | CHAINLOAD_UNCACHED);

	go((const void *)((unsigned long)staged | CHAINLOAD_UNCACHED),
	   (void *)(CHAINLOAD_ENTRY | CHAINLOAD_UNCACHED),
	   longwords,
	   (void *)CHAINLOAD_ENTRY);
}
```

The quiesce line uses the port's own `sound_done()` rather than an SGL call: `sega_snd.h` offers only `SND_StopDsp`/`SND_StopSeq`/`SND_StopPcm`/`SND_StopVlAnl`, none of which is "shut the sound layer down", and `sound_srl.cxx` already owns that teardown for this port. `slSlaveFunc` takes a function pointer *and* a parameter (`sl_def.h:2595`), so both are passed as `NULL`.

- [ ] **Step 3: Wire the call sites**

In `saturn/src/main.c`, add to the `#ifdef HOTA_SATURN` include block:

```c
#include "chainload.h"
```

In `boot_sequence()`, replace the `bootmenu_init` call:

```c
	bootmenu_init(&state, (uint32_t)platform_ticks(),
	              chainload_available(), disc_part2_available());
```

And extend the frame handling, before the existing `if (frame.start_game)`:

```c
		if (frame.start_part1)
		{
			boot_fade_out((int)frame.screen, (int)frame.highlight);
			chainload_run();
		}
```

`chainload_run()` returning means staging failed with nothing torn down, so the loop simply continues and the menu comes back up — the fade is the only thing the player sees, and pressing confirm again retries.

- [ ] **Step 4: Build**

```bash
cd saturn && make all HOTA_PART1=1
```

Expected: clean build. Confirm in `BuildDrop/Heart of the Alien (USA).map` that `chainload_run` is present.

- [ ] **Step 5: Verify on the emulator — this step is the user's**

Hand the built disc over and ask for a Mednafen run of:

1. Boot, wait or skip to the menu, confirm *HEART OF THE ALIEN* → unchanged behaviour, opening cinematic plays.
2. Boot, confirm *OUT OF THIS WORLD* → screen fades to black, then Part I's `OPENING.CPK` movie plays.

Signal 2 is the milestone: the movie playing means the jump landed, SRL came up and the CD block is serving reads. If it does not, diagnose against the spec's risk table — crash immediately after the jump points at the uncached mirror; a hang in the black at the slave SH-2; silence or a hang in Part I's `SND_Init` at the SCSP driver; a failure to open `BANK01` at `GFS_Reset()`. Remove quiesce steps one at a time; do not rewrite the function.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/system/chainload.h saturn/src/system/chainload.cxx saturn/src/main.c
git commit -m "Hand the console to Part I when the boot menu's Out of This World entry is confirmed, staging its program in low work RAM and copying it over ours through the uncached mirror from a seven-instruction trampoline that survives having its own address space overwritten."
```

---

## Task 5: Part I's data step in the setup kit

**Files:**
- Modify: `tools/assets/update.bat` (POSIX half around `:32`, Windows half around `:107`)
- Modify: `tools/assets/README-kit.md`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `update.bat` installs Part I's `bank01..0d` and `memlist.bin` into the same `data/` directory it already injects from. Task 6's smoke test asserts they land on the disc.

- [ ] **Step 1: Extend the POSIX half**

In `tools/assets/update.bat`, replace line 32 (`:; sh ./data.bat "$@"`) with:

```
:; sh ./data.bat "$@"
:;
:; # Part I's data, if this kit or checkout has a way to fetch it. Two
:; # locations because this script runs in both: part1/ in the released kit,
:; # where CI stages Another-Saturn's own script verbatim, and the pinned
:; # checkout in a source tree. Optional by design -- a player who owns the
:; # Sega CD game but not the DOS release still gets a working Part II disc,
:; # and the boot menu leaves OUT OF THIS WORLD unconfirmable rather than
:; # launching into data that is not there.
:; PART1_DATA=
:; if [ -f ./part1/data.bat ]; then PART1_DATA=./part1/data.bat; fi
:; if [ -z "$PART1_DATA" ] && [ -f ../../.another/Another-Saturn/tools/assets/data.bat ]; then PART1_DATA=../../.another/Another-Saturn/tools/assets/data.bat; fi
:; if [ -n "$PART1_DATA" ]; then if sh "$PART1_DATA" "$@"; then echo "Part I: data installed."; else echo "Part I: data unavailable -- OUT OF THIS WORLD will not be playable on this disc."; fi; else echo "Part I: no data step present -- skipping."; fi
```

No `\` continuations and one statement per line, as the file's own comment at `:41` requires.

Explicit `if ... fi` rather than `[ -f X ] && VAR=Y`: line 20 sets `set -eu`, and whether a
false leading test in a bare AND-OR list aborts the script is a portability grey area across
`dash` and `bash`. This script ships to end users, where an abort means no disc at all.

- [ ] **Step 2: Extend the Windows half**

Replace lines 107-108 with:

```
CALL "%~dp0data.bat" %*
IF ERRORLEVEL 1 ( ECHO ERROR: data install failed & EXIT /B 1 )

REM Part I's data. Optional -- its failure must not stop a Part II disc, so
REM this deliberately does not test ERRORLEVEL the way the call above does.
SET "PART1_DATA="
IF EXIST "%~dp0part1\data.bat" SET "PART1_DATA=%~dp0part1\data.bat"
IF NOT DEFINED PART1_DATA IF EXIST "%~dp0..\..\.another\Another-Saturn\tools\assets\data.bat" SET "PART1_DATA=%~dp0..\..\.another\Another-Saturn\tools\assets\data.bat"
IF DEFINED PART1_DATA (
    CALL "%PART1_DATA%" %*
    IF ERRORLEVEL 1 ( ECHO Part I: data unavailable -- OUT OF THIS WORLD will not be playable on this disc. ) ELSE ( ECHO Part I: data installed. )
) ELSE ( ECHO Part I: no data step present -- skipping. )
```

- [ ] **Step 3: Update the kit README**

In `tools/assets/README-kit.md`, retitle and extend. Replace the opening paragraph with:

```markdown
# Heart of the Alien: Parts I and II for Sega Saturn — setup kit

The disc in this kit boots but has no game in it. Neither game's data files are
ours to give you, so `update.bat` takes them from your own copies and folds
them into the disc on your machine.

This disc holds both games the original's menu offers. *Heart of the Alien* is
Part II and comes from the Sega CD disc. *Out of This World* is Part I and comes
from the PC DOS release. They are fetched independently, and if only one
succeeds you get a disc on which only that game's menu entry can be confirmed.
```

Add to the "What's in here" table:

```markdown
| `part1/` | Part I's own data step, from the Another-Saturn project |
```

- [ ] **Step 4: Verify the checkout path**

With `.another/` present from Task 2:

```bash
cd tools/assets && bash update.bat prep
ls ../../saturn/cd/data | grep -iE "^bank|memlist"
```

Expected: `bank01`..`bank0d` and `memlist.bin` listed. Then rename `.another` aside and re-run to confirm the skip path prints `Part I: no data step present -- skipping.` and still exits 0.

- [ ] **Step 5: Commit**

```bash
git add tools/assets/update.bat tools/assets/README-kit.md
git commit -m "Install Part I's data alongside Part II's in one setup run, from the kit's staged copy of Another-Saturn's own script or from a pinned checkout, treating its absence as one unplayable menu entry rather than a failed disc."
```

---

## Task 6: Release workflow

**Files:**
- Create: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: `HOTA_PART1=1` (Task 2), `update.bat`'s Part I step (Task 5).
- Produces: a release asset named `Heart of the Alien - Parts I and II Setup Kit (<ver>).zip`.

- [ ] **Step 1: Write the workflow**

Create `.github/workflows/release.yml`:

```yaml
name: Build & release setup kit

# Tag pushes (v*) build the two-game engine-only disc, package the setup kit,
# and attach it to a release -- creating the release if one does not exist yet.
# Manual runs do the same but upload the kit as a workflow artifact instead.
#
# This workflow never touches either game's data. The disc it ships boots but
# has no resources in it; the end user runs update.bat, which fetches their own
# copies and folds them in. The complete disc is built only by full-image.yml,
# and only as a private artifact.
on:
  push:
    tags: ['v*', '[0-9]*']
  workflow_dispatch:

permissions:
  contents: write

env:
  DISC_NAME: Heart of the Alien (USA)
  KIT_NAME: Heart of the Alien - Parts I and II Setup Kit

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout (with SaturnRingLib submodule)
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install host tools
        run: sudo apt-get update -qq && sudo apt-get install -y -qq xorriso wget unzip zip curl

      # Same cache key Another-Saturn uses, so the SH-2 gcc fetch is shared
      # between the two projects' runs.
      - name: Cache SaturnRingLib toolchain
        id: toolchain
        uses: actions/cache@v4
        with:
          path: |
            SaturnRingLib/Compiler
            SaturnRingLib/tools/bin
          key: srl-toolchain-gcc14.2.0-iso2raw0.2.2

      - name: Fetch SH-2 toolchain + iso2raw
        if: steps.toolchain.outputs.cache-hit != 'true'
        working-directory: SaturnRingLib
        run: |
          set -e
          bash tools/scripts/getcompiler.sh 14.2.0
          bash tools/scripts/getiso2raw.sh v0.2.2

      # HOTA_AUDIO=none because the shipped disc is data-only: the 41 CD-DA
      # tracks come from the player's own rip and are laid by update.bat.
      # HOTA_PART1=1 runs tools/another/fetch.sh, which builds Part I's program
      # at the pinned ref and copies it in as ANOTHER.BIN.
      # HOTA_ASSETS=0 suppresses the makefile's data.bat self-repair, which is
      # right for a checkout and wrong here: it would fetch the Sega CD rip and
      # bake somebody else's data into a disc we then publish.
      - name: Build engine-only two-game disc
        working-directory: saturn
        run: |
          set -e
          export SRL_INSTALL_ROOT=../SaturnRingLib
          make all HOTA_PART1=1 HOTA_AUDIO=none HOTA_ASSETS=0
          ls -la BuildDrop

      # Two games on one disc means two ways to redistribute data that is not
      # ours. Both gated sets are refused, in the skeleton and in the ISO.
      - name: Assert the disc carries no game data
        run: |
          set -e
          if ls saturn/cd/data/bank* saturn/cd/data/BANK* saturn/cd/data/memlist.bin saturn/cd/data/MEMLIST.BIN 2>/dev/null | grep -q .; then
            echo "ERROR: Part I data present in the CD skeleton -- refusing to package"; exit 1
          fi
          for f in END1 END2 END3 END4 GAME2 INTRO1 INTRO2 INTRO3 INTRO4 MAKE2MB MID2 ROOMS1 ROOMS2 ROOMS3 ROOMS4 ROOMS5 ROOMS6 ROOMS7 ROOMS8; do
            if [ -f "saturn/cd/data/$f.BIN" ] || [ -f "saturn/cd/data/$f.bin" ]; then
              echo "ERROR: Part II blob $f present in the CD skeleton -- refusing to package"; exit 1
            fi
          done
          xorriso -indev "saturn/BuildDrop/${DISC_NAME}.iso" -find / 2>/dev/null | tee /tmp/base-listing
          if grep -qiE "BANK|MEMLIST|ROOMS1|GAME2" /tmp/base-listing; then
            echo "ERROR: game data inside the base ISO"; exit 1
          fi
          for f in 0.BIN ANOTHER.BIN OPENING.CPK MENUOOTW.ART; do
            grep -q "'/$f'" /tmp/base-listing || { echo "ERROR: /$f missing -- disc is incomplete"; exit 1; }
          done

      - name: Stage setup kit
        id: kit
        run: |
          set -e
          if [ "${GITHUB_REF_TYPE}" = "tag" ]; then VER="${GITHUB_REF_NAME}"; else VER="${GITHUB_SHA::7}"; fi

          STAGE="$RUNNER_TEMP/kit-stage"
          mkdir -p "$STAGE/${DISC_NAME}" "$STAGE/data" "$STAGE/music" "$STAGE/part1"

          cp "saturn/BuildDrop/${DISC_NAME}.iso" "$STAGE/${DISC_NAME}/${DISC_NAME}.iso"
          cp -r tools/assets/bin tools/assets/lib "$STAGE/"
          cp tools/assets/data.bat tools/assets/update.bat "$STAGE/"
          cp tools/assets/README-kit.md "$STAGE/README.md"
          cp saturn/cd/music/tracklist "$STAGE/music/tracklist"

          # Part I's data step. fetch.sh already staged tools/assets/part1/ with
          # Another-Saturn's data.bat verbatim and a CONFIG.ME pointing at our
          # CD skeleton, so this copies an already-correct directory and only
          # re-aims DATA_DIR at the kit's own layout. Their data.bat resolves
          # DATA_DIR relative to its own directory with no override, which is
          # why the path has to be rewritten per context rather than passed in.
          cp tools/assets/part1/data.bat "$STAGE/part1/data.bat"
          sed 's|^DATA_DIR=.*|DATA_DIR=../data|' tools/assets/part1/CONFIG.ME > "$STAGE/part1/CONFIG.ME"
          grep -q '^DATA_DIR=../data$' "$STAGE/part1/CONFIG.ME"

          # In a checkout CD_DIR points back at the CD skeleton; in the kit
          # there is no checkout, so it becomes the kit root.
          sed 's|^CD_DIR=.*|CD_DIR=.|' tools/assets/CONFIG.ME > "$STAGE/CONFIG.ME"
          grep -q '^CD_DIR=.$' "$STAGE/CONFIG.ME"

          chmod +x "$STAGE"/*.bat "$STAGE"/part1/*.bat "$STAGE"/bin/lin/iso2raw "$STAGE"/bin/mac/*/iso2raw

          echo "ver=$VER"           >> "$GITHUB_OUTPUT"
          echo "stage_dir=$STAGE"   >> "$GITHUB_OUTPUT"

      # Prove the kit works before shipping it, on a copy so downloads and
      # outputs never reach the zip. The only place the Linux path (system
      # xorriso + bin/lin/iso2raw) is exercised, and the only automated proof
      # that both games coexist on one disc.
      - name: Smoke-test the kit
        run: |
          set -e
          TEST="$RUNNER_TEMP/kit-test"
          cp -r "${{ steps.kit.outputs.stage_dir }}" "$TEST"
          (cd "$TEST" && bash update.bat)
          out="$TEST/${DISC_NAME} - Complete"
          test -s "$out/${DISC_NAME} (Track 01).bin"
          grep -q "FILE \"${DISC_NAME} (Track 01).bin\" BINARY" "$out/${DISC_NAME}.cue"
          perl -e 'binmode STDIN; binmode STDOUT; while (read(STDIN,$s,2352)==2352) { print substr($s,16,2048) }' \
            < "$out/${DISC_NAME} (Track 01).bin" > /tmp/deraw.iso
          xorriso -indev /tmp/deraw.iso -find / 2>/dev/null | tee /tmp/full-listing
          for f in 0.BIN ANOTHER.BIN OPENING.CPK BANK01 BANK0D MEMLIST.BIN ROOMS1.BIN GAME2.BIN; do
            grep -q "'/$f'" /tmp/full-listing || { echo "ERROR: /$f missing from the built disc"; exit 1; }
          done
          # IP.BIN is the SEGA boot header; without it byte-for-byte the disc
          # is a coaster, and xorriso rewrites that area unless we restore it.
          cmp -n 32768 "saturn/BuildDrop/${DISC_NAME}.iso" /tmp/deraw.iso
          echo "Kit smoke test passed: both games on one disc."

      - name: Package setup kit
        id: package
        run: |
          set -e
          NAME="${KIT_NAME} (${{ steps.kit.outputs.ver }})"
          (cd "${{ steps.kit.outputs.stage_dir }}" && zip -rq "$RUNNER_TEMP/${NAME}.zip" .)
          echo "artifact_name=${NAME}"             >> "$GITHUB_OUTPUT"
          echo "zip_path=$RUNNER_TEMP/${NAME}.zip" >> "$GITHUB_OUTPUT"

      - name: Upload kit as workflow artifact (manual runs)
        if: github.event_name == 'workflow_dispatch'
        uses: actions/upload-artifact@v4
        with:
          name: ${{ steps.package.outputs.artifact_name }}
          path: ${{ steps.kit.outputs.stage_dir }}

      - name: Attach to GitHub release (tag runs)
        if: github.event_name == 'push' && startsWith(github.ref, 'refs/tags/')
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          set -e
          TAG="${GITHUB_REF_NAME}"
          if ! gh release view "$TAG" >/dev/null 2>&1; then
            printf '%s\n' \
              "Automated Heart of the Alien / Out of This World Saturn build from $TAG." \
              "" \
              "Unzip and run \`update.bat\` (double-click on Windows, or \`bash update.bat\`)." \
              "It fetches both games' original data files and builds a burnable cue/bin." \
              "See README.md in the zip." > /tmp/notes.md
            gh release create "$TAG" --title "$TAG" --notes-file /tmp/notes.md
          fi
          gh release upload "$TAG" "${{ steps.package.outputs.zip_path }}" --clobber
```

The `music/tracklist` copy is not decoration: `update.bat:53` reads `"$MUSIC_DIR/tracklist"` to order the audio tracks, and with `CD_DIR=.` that resolves to `./music/tracklist` inside the kit.

- [ ] **Step 2: Verify with a manual run**

Push the branch and trigger the workflow via `workflow_dispatch`. Expected: green, with `Kit smoke test passed: both games on one disc.` in the log and a downloadable artifact.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "Build and publish a setup kit for both games, staging Part I's data step verbatim from the pinned checkout and refusing to package if either game's non-free data reached the disc."
```

---

## Task 7: Full-image workflow

**Files:**
- Create: `.github/workflows/full-image.yml`

**Interfaces:**
- Consumes: `HOTA_PART1=1` (Task 2).
- Produces: nothing other tasks use.

- [ ] **Step 1: Write the workflow**

Create `.github/workflows/full-image.yml`:

```yaml
name: Build full disc image (with game data)

# Manual only, and ARTIFACT only. This build fetches both games' original data
# files and bakes them into the disc, so the result is not ours to redistribute
# -- it is never attached to a public release. The kit end users get is built by
# release.yml, which ships an engine-only disc and lets them supply their own
# copies.
on:
  workflow_dispatch:

permissions:
  contents: read

env:
  DISC_NAME: Heart of the Alien (USA)

jobs:
  full-image:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install host tools
        run: sudo apt-get update -qq && sudo apt-get install -y -qq xorriso wget unzip zip curl sox

      - name: Cache SaturnRingLib toolchain
        id: toolchain
        uses: actions/cache@v4
        with:
          path: |
            SaturnRingLib/Compiler
            SaturnRingLib/tools/bin
          key: srl-toolchain-gcc14.2.0-iso2raw0.2.2

      - name: Fetch SH-2 toolchain + iso2raw
        if: steps.toolchain.outputs.cache-hit != 'true'
        working-directory: SaturnRingLib
        run: |
          set -e
          bash tools/scripts/getcompiler.sh 14.2.0
          bash tools/scripts/getiso2raw.sh v0.2.2

      # Part I's program first: fetch.sh must have written ANOTHER.BIN into the
      # skeleton before update.bat's part1 step looks for a checkout to take
      # Part I's data from.
      - name: Build Part I's program into the skeleton
        run: |
          set -e
          sh tools/another/fetch.sh

      # Before the build, not after: shared.mk authors the ISO straight out of
      # saturn/cd/data, so both games' files have to be sitting there when
      # xorrisofs runs. No injection step is needed on this path.
      - name: Fetch both games' data into the CD skeleton
        run: |
          set -e
          bash tools/assets/update.bat prep
          ls -la saturn/cd/data

      - name: Build Saturn disc (release)
        working-directory: saturn
        run: |
          set -e
          export SRL_INSTALL_ROOT=../SaturnRingLib
          make all HOTA_PART1=1
          ls -la BuildDrop

      - name: Sanity-check the disc
        run: |
          set -e
          xorriso -indev "saturn/BuildDrop/${DISC_NAME}.iso" -find / 2>/dev/null | tee /tmp/listing
          for f in 0.BIN ANOTHER.BIN OPENING.CPK BANK01 BANK0D MEMLIST.BIN ROOMS1.BIN GAME2.BIN MENUOOTW.ART; do
            grep -q "'/$f'" /tmp/listing || { echo "ERROR: /$f missing from the disc"; exit 1; }
          done
          test -s "saturn/BuildDrop/${DISC_NAME}.bin"
          grep -q 'TRACK 01 MODE1/2352' "saturn/BuildDrop/${DISC_NAME}.cue"

      - name: Stage burnable disc
        run: |
          set -e
          mkdir -p "$RUNNER_TEMP/disc"
          cp "saturn/BuildDrop/${DISC_NAME}".bin "saturn/BuildDrop/${DISC_NAME}".cue "$RUNNER_TEMP/disc/"

      - name: Upload full disc artifact
        uses: actions/upload-artifact@v4
        with:
          name: hota-parts-one-and-two-complete-${{ github.sha }}
          path: ${{ runner.temp }}/disc/
```

- [ ] **Step 2: Verify with a manual run**

Trigger via `workflow_dispatch`. Expected: green, with an artifact containing a cue/bin pair. Download it and confirm it boots in Mednafen to the game-select menu with both entries confirmable.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/full-image.yml
git commit -m "Add a manual, artifact-only build of the complete two-game disc, kept off releases because neither game's data is ours to hand out."
```

---

## Acceptance

Mirrors the spec's acceptance section:

1. `bash saturn/tests/run_tests.sh` passes, including the four new `test_bootmenu.c` cases. — Task 1
2. `make all HOTA_PART1=1` yields an ISO whose root holds `ANOTHER.BIN`, `OPENING.CPK`, our blobs and `.ART`. — Task 2
3. `make all` without `HOTA_PART1` yields today's disc with *OUT OF THIS WORLD* inert. — Task 2 Step 6
4. In Mednafen, confirming *OUT OF THIS WORLD* fades to black and Part I's opening movie plays. — Task 4 Step 5
5. In Mednafen, confirming *HEART OF THE ALIEN* behaves exactly as today. — Task 4 Step 5
6. `release.yml` on a manual run produces a kit whose smoke test finds both games' files at the volume root with IP.BIN intact. — Task 6
7. A kit run with only one rip available emits a disc on which only that game's entry confirms. — Task 5 Step 4 plus Task 1's tests
