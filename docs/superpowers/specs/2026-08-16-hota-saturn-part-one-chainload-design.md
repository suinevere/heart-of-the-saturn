# Heart of The Alien → Sega Saturn — Part I Chain-load Design Spec

**Date:** 2026-08-16
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)
**Part I source:** [suinevere/Another-Saturn](https://github.com/suinevere/Another-Saturn)

## Amendment — Part I is downloaded, not built

Everything below describing `fetch.sh` as cloning Another-Saturn at a pinned SHA and
building it is superseded. Another-Saturn now publishes `0.bin`, `OPENING.CPK`, `data.bat`
and `CONFIG.ME` as release assets with a `SHA256SUMS` beside them, and `fetch.sh`
downloads those instead.

The reasoning that changed: the two programs share no linkage. One overwrites the other at
`0x06004000` and jumps to it, so building them with one toolchain was never a requirement —
it only ever cost us a second SaturnRingLib checkout, needed because `shared.mk` compiles
`preloader.cxx` to `modules/sgl/SRC/preloader.o` *inside the SDK tree* and our
`SGL_MAX_POLYGONS = 256` against Part I's `1500` would share one object make declines to
rebuild. Taking the artifact makes that hazard cease to exist rather than be managed.

Superseded by this: the `convert_binary` section, the `COMPILER_DIR` and separate-SDK
decision, `PART1_REPO`/`PART1_REF`/`PART1_DIR`, and the risk-table row about a stale pin.
Everything downstream is unchanged — `part1/` staging, both workflows and `chainload.cxx`
consume `ANOTHER.BIN` by name and never cared how it arrived, which is why this was a
rewrite of one script rather than of the feature.

One property was genuinely lost. A git SHA is immutable; a release asset is not —
Another-Saturn's workflow uploads with `--clobber`, so an asset can be replaced under a tag
that never moved. `SHA256SUMS` is what closes that, and `fetch.sh` verifies against it
rather than trusting the URL.

## Goal

Make the disc's own title true. It is called *Heart of the Alien - Out of This World
Parts I and II*, its boot menu already offers both entries, and only one of them does
anything.

Today `bootmenu.h:52` documents the gap outright: *OUT OF THIS WORLD "can be highlighted
but never confirmed -- this engine carries no Part I data, and the entry exists because
the original's menu does."* Another-Saturn is a working Saturn port of that game. This
spec puts its program on our disc and makes that entry launch it.

Done means: a release disc carries both games; selecting *OUT OF THIS WORLD* fades out
and hands the console to Another-Saturn; selecting *HEART OF THE ALIEN* is unchanged; and
one setup kit fetches both games' non-free data, with either game's absence disabling
only its own entry.

## Scope

In:

- Chain-loading Another-Saturn's program over ours from the boot menu.
- A fetch script that builds Part I from a pinned upstream ref into our CD skeleton.
- Per-game availability probing, so a disc missing either game's data still boots to a
  menu that says so.
- A merged setup kit that runs both projects' data steps and injects once.
- This repository's first two CI workflows.

Out:

- Any change to Another-Saturn. The glue is one-directional; that repo does not learn
  our name. Its program is consumed exactly as its own build emits it.
- Returning from Part I to Part II. The handoff is one-way; coming back is a console
  reset.
- Merging the two engines into one binary, or running them concurrently.
- Replacing *OUT OF THIS WORLD* with a different label. `MENUOOTW.ART` is the original's
  own pixels and stays.

## What the hardware and both builds already fix

### Both programs link to the same address, so this is an overwrite

`sgl.linker:4` places `PRELOADER` at `0x06004000` and both projects use it unmodified.
Part I cannot be loaded beside us; it is loaded *over* us. Everything else in the handoff
design follows from that one fact.

### There is no BOOT ROM service that runs another program

`sega_sys.h` exposes the BOOT ROM's service table. `SYS_EXECDMP` is the crash dumper.
`SYS_Exit` returns to the BIOS shell, which re-boots the disc's first-read file — us —
so it is a loop, not an exit. SMPC `SMPC_SYSRES` has the same problem. The handoff is
ours to write.

### SRL can read straight to an address

`srl_cd.hpp:399`, `Cd::File::LoadBytes(sectorOffset, size, destination)`. This port
already owns a whole-sector read path over it in `disc_srl.cxx`, built for exactly this
kind of bulk transfer.

### `shared.mk` authors the volume root from a directory

`shared.mk:277` passes `$(ASSETS_DIR)` — all of `saturn/cd/data` — to `xorrisofs`.
Anything dropped in that directory reaches the disc root. Getting Part I onto the disc is
a file-copy problem, not a build-system problem.

### `convert_binary` is the narrow build target

`shared.mk:264` defines `convert_binary : compile_objects`, whose recipe objcopies the
ELF to `./cd/data/0.bin`. Building Part I stops there: no `xorrisofs`, no `iso2raw`, no
music pass, no second disc image.

### The boot art is committed; the game data is not

All eleven `.ART` files are tracked. A disc with zero game data still reaches the opening
stills and the game-select menu and can still draw them. That is what makes per-game
degradation possible rather than theoretical.

### `disc_open` already counts what is missing

`disc_srl.cxx:613` computes `missingCount` and `sizeBadCount` and reports both at `:639`
before failing. A non-fatal availability probe is a refactor of existing arithmetic.

### Another-Saturn ships its opening movie

`saturn/cd/data/OPENING.CPK` is tracked in that repo — an 11,162,812-byte blob committed
in `60887fb`, with its `.gitignore:84` noting the exception deliberately. It is not
derived data we regenerate; it is a file we copy. No ffmpeg dependency enters this
project.

### The injection layer is already shared

`tools/assets/lib/inject.sh` and `inject.ps1` are **byte-identical** between the two
repositories. `diff` is clean on both. The merged kit ships one copy and neither project
forks it.

### The two configurations differ enough to matter

```
                 ours   Another-Saturn
SRL_MAX_TEXTURES  128        100
SGL_MAX_VERTICES  256       2500
SGL_MAX_POLYGONS  256       1500
```

`shared.mk:22` compiles `preloader.cxx` to `modules/sgl/SRC/preloader.o` **inside the SDK
tree**, not the project tree. Two configurations writing one object path, with make's
timestamp check declining to rebuild it, would silently link one game's work-area sizing
into the other's image. This is why Part I gets its own SaturnRingLib checkout.

## Architecture

```
  saturn/src/bootmenu.{h,c}        pure, host-tested
      part1_available ─┐
      part2_available ─┴─> which entries confirm

  saturn/src/system/chainload.{h,cxx}      SRL seam, not testable off-target
      chainload_available()  probe for ANOTHER.BIN
      chainload_run()        never returns

  tools/another/fetch.sh + CONFIG.ME       build-time glue
      clone @ PART1_REF -> make convert_binary -> copy allowlist

  .github/workflows/release.yml            kit assembly + smoke test
  .github/workflows/full-image.yml         manual, artifact-only
```

The split is the same one this port already uses for `bootmenu` and `saturn_bootart`, and
for the same reason: everything decidable is decided in pure C that host gcc can test,
and the hardware seam is left with no decisions in it at all.

## Components

### `bootmenu.{h,c}` — the pure state machine

Two new fields on `bootmenu_state`, `part1_available` and `part2_available`, set by a new
parameter pair on `bootmenu_init`. One new field on `boot_frame`, `start_part1`, beside
the existing `start_game`.

The confirm gate at `bootmenu.c:95` currently reads:

```c
&& highlight_before_move == (int)BOOT_ENTRY_HEART_OF_THE_ALIEN
```

It becomes two arms, each gated on its own availability flag. Highlighting is unchanged —
an unavailable entry still lights, exactly as *OUT OF THIS WORLD* does today; only
confirming is refused. That keeps the menu looking like the original's rather than
growing a greyed-out state the capture has no art for.

No SRL, no stdio, no engine headers. Unchanged constraint, and it is what keeps this file
in `run_tests.sh`.

### `chainload.{h,cxx}` — the SRL seam

Lives in `saturn/src/system/` alongside `disc_srl.cxx` and `saturn_bootart.cxx`, the other
files that exist only to touch hardware on SRL's behalf.

```c
int  chainload_available(void);   /* one GFS open, probing for ANOTHER.BIN */
void chainload_run(void);         /* never returns */
```

`chainload_available()` is called next to `boot_art_load()` in `boot_sequence()`, and its
result is passed into `bootmenu_init`. `chainload_run()` is called when
`frame.start_part1` is set.

Part II's counterpart, `disc_part2_available()`, is added to `disc.h` and implemented in
`disc_srl.cxx` beside the loop it reuses. It belongs there rather than in `chainload.h`
because it is a question about our own data, not about Part I, and `disc_srl.cxx` is
already the only file that knows the blob list.

`chainload_run()`, in order:

1. `video_set_fade(0)`, `boot_art_release()`, `disc_stop_track()`. The handoff sits behind
   black, the same way every other drive-latency wait in this port does.
2. Allocate the staging buffer from LWRAM and read `ANOTHER.BIN` into it whole-sector.
3. Quiesce: `GFS_Reset()`, halt the slave SH-2, stop the SCSP driver,
   `SYS_SETSCUIM(0xFFFFFFFF)`.
4. Copy a small position-independent trampoline into LWRAM and jump to it. It cannot live
   in HWRAM, because HWRAM is what gets overwritten.
5. Trampoline: mask interrupts in `SR`, copy the staged image to `0x06004000` **through
   the uncached mirror at `0x2600_0000`** so the SH-2 instruction cache cannot serve stale
   Heart of the Alien code, then jump to `0x06004000`.

Another-Saturn's own `PreLoader()` then zeroes `.bss`, re-initialises SRL memory and runs
its constructors. It does not need to know it was chain-loaded, which is what keeps that
repository untouched.

**The quiesce steps in stage 3 are deliberately independent of each other.** A failure is
diagnosed by removing them one at a time, not by rewriting the function. That property is
worth preserving through any later edit.

### `tools/another/fetch.sh` and `CONFIG.ME`

Plain `sh`, invoked as `sh ../tools/another/fetch.sh`, following `tools/build.sh`'s
precedent — the makefile already calls its helpers through `sh`, so the polyglot `.bat`
treatment the asset kit needs does not apply here.

`CONFIG.ME` holds `PART1_REPO` and `PART1_REF` (a pinned commit SHA), matching the
`KEY=VALUE` convention `tools/assets/CONFIG.ME` established.

1. Clone or update `.another/Another-Saturn` at `PART1_REF`, `--depth 1`, recursing
   submodules. Shallow matters: the tracked `OPENING.CPK` is 11 MB and a full history
   would carry every revision of it.
2. `make convert_binary` in its `saturn/`, with `COMPILER_DIR` passed on the command line
   pointing at our already-fetched `SaturnRingLib/Compiler`. The SH-2 toolchain downloads
   once for both games. Its `data.bat` is **not** run — no gated data enters the engine
   build, the same discipline its own `release.yml` keeps.
3. Copy an explicit allowlist into `saturn/cd/data/`:

   | From `.another/Another-Saturn/saturn/cd/data/` | To |
   |---|---|
   | `0.bin` | `ANOTHER.BIN` |
   | `OPENING.CPK` | `OPENING.CPK` |

   An allowlist, not a `cp -r`, so what Part I contributes to our disc is stated rather
   than inherited. `REFAUD.CPK` is excluded: it is SRL's `SKYBL.CPK` staged as a reference
   encode, reachable only through a compile switch at `opening.cxx:63`, and it is 7.7 MB.
   `OPENING.BIN` is excluded: `git ls-tree` shows `OPENING.CPK` as the only tracked file
   in that directory, so both are untracked local scratch rather than part of the project.
   `SDDRVS.*`, `BOOTSND.MAP` and the `.TXT` files are excluded because `shared.mk` writes
   our own.

### `saturn/makefile`

```make
HOTA_PART1 ?= 0
all: part1_assets
```

Declared **before** the include, for the reason the file already documents for
`game_assets`: make records prerequisites in the order it first sees them, and
`shared.mk`'s own `all:` lines would otherwise schedule the fetch after the build that
consumes it. `part1_assets` is a no-op at `HOTA_PART1=0`. A `clean: part1_clean` arm goes
after the include, removing `cd/data/ANOTHER.BIN` and `cd/data/OPENING.CPK`.

### `.gitignore`

Additions: `saturn/cd/data/BANK*`, `saturn/cd/data/bank*`, `saturn/cd/data/OPENING.CPK`,
`/.another/`. `ANOTHER.BIN` and `MEMLIST.BIN` are already covered by the existing
`saturn/cd/data/*.BIN`; `memlist.bin` by `*.bin`. Both cases are listed for `bank*` for
the same reason the existing entries list both — git's ignore matching is case-sensitive
wherever `core.ignorecase` is false, such as CI on Linux.

### The merged setup kit

```
Heart of the Alien - Parts I and II Setup Kit.zip
    update.bat                 ours, extended to call both data steps
    data.bat                   ours          (Part II: Sega CD rip)
    CONFIG.ME                  ours
    part1/
        data.bat               theirs, verbatim from .another/ @ PART1_REF
        CONFIG.ME              theirs, DATA_DIR rewritten to ../data
    lib/  bin/                 one shared copy
    data/                      both games' files land here
    Heart of the Alien (USA)/
        Heart of the Alien (USA).iso    engine-only; already holds
                                        ANOTHER.BIN and OPENING.CPK
    README.md
```

The two `data.bat` scripts are genuinely different work — ours (227 lines) drives
`extract_disc` over a Sega CD rip, theirs (98) unzips a DOS archive and installs
`bank01..0d` plus `memlist.bin`. They stay separate. Ours is not rewritten to understand
theirs, and theirs is not forked into this repository; CI stages it verbatim from the
pinned checkout, so it tracks upstream by construction.

`update.bat` gains one step and keeps **one** injection: Part II data, then Part I data,
then inject `data/` once, then lay the CD-DA tracks and write the cue. The injection stage
is already game-agnostic — it injects whatever is in `data/` — so both games reach the
volume root in a single `xorriso` commit with IP.BIN held across it.

### `.github/workflows/release.yml`

This repository has no CI today. Modelled on Another-Saturn's, with the toolchain cache
keyed identically so the SH-2 gcc fetch is shared between the two projects' runs.

Tag (`v*`, `[0-9]*`) or manual. Checkout recursive → install host tools → cache/fetch
toolchain → build with `HOTA_PART1=1` and `HOTA_AUDIO=none` → assert no leaked data →
stage kit → smoke-test → package → attach on tag, artifact on manual.

`HOTA_AUDIO=none` because the shipped base ISO is data-only; the 41 CD-DA tracks come from
the player's own rip and are laid by `update.bat`, as today.

### `.github/workflows/full-image.yml`

Manual only, artifact only, both data sets baked in, never attached to a release. Directly
parallel to Another-Saturn's file of the same name and for the same reason: the result is
not ours to redistribute.

## Data and control flow

```
power on
  └─ 0.BIN (Heart of the Alien)
       initialize()
       boot_sequence()
           boot_art_load()
           chainload_available()   ─┐
           disc_part2_available()  ─┴─> bootmenu_init(..., p1, p2)
           ┌─ four opening stills, CD-DA track 3
           └─ game-select menu
                 OUT OF THIS WORLD   confirm && part1_available
                     └─> chainload_run()
                             fade to black, stop CD-DA
                             read ANOTHER.BIN -> LWRAM
                             quiesce GFS / slave / SCSP / SCU
                             trampoline in LWRAM
                             LWRAM -> 0x2600_4000 (uncached)
                             jump 0x06004000
                                 └─> Another-Saturn PreLoader()
                                       zero .bss, SRL init, ctors
                                       openingPlay() -> OPENING.CPK
                                       (never returns to us)
                 HEART OF THE ALIEN  confirm && part2_available
                     └─> return; run()      unchanged
```

## Disc layout

The volume root, after the change:

| Source | Files |
|---|---|
| `shared.mk` writes these itself, unchanged | `0.BIN` (ours), `ABS/BIB/CPY.TXT`, `SDDRVS.DAT`, `SDDRVS.TSK`, `BOOTSND.MAP` |
| Ours, existing | 19 game blobs, 11 `.ART` |
| Part I program | `ANOTHER.BIN` |
| Part I free asset | `OPENING.CPK` |
| Part I gated data | `BANK01`..`BANK0D`, `MEMLIST.BIN` |

No name collisions: we have no `OPENING.*`, no `BANK*`, no `MEMLIST.BIN`; they have none
of our blob or `.ART` names. Roughly 51 entries against `SRL_MAX_CD_FILES = 256`. Roughly
450 MB against a 700 MB disc — 429 MB of that is our CD-DA, and Part I's whole
contribution is about 21 MB.

## Memory

Part I's image is 222,288 bytes today and is staged in LWRAM before the copy, so the
staging buffer must be sized from the file rather than from a constant. The trampoline is
a few dozen bytes, also in LWRAM.

The one thing to confirm during implementation is how much LWRAM is free at the moment
`boot_sequence()` runs — `vm.c` and `game2bin.c` take large LWRAM allocations under
`HOTA_SATURN`, and whether they are live this early decides whether the staging buffer
needs one of them freed first. Freeing is always safe here: nothing in this path ever
returns to the game.

## Availability probing

`chainload_available()` is one GFS open for `ANOTHER.BIN`. Part II's probe reuses
`disc_srl.cxx`'s existing per-blob loop, split so the counting can be asked for without
the failure being taken.

Neither probe is on a hot path — both run once, before the first still is drawn, while the
drive is otherwise idle.

## Error handling

| Condition | Behaviour |
|---|---|
| `ANOTHER.BIN` absent | *OUT OF THIS WORLD* lights but does not confirm — today's behaviour exactly |
| Part I banks absent, program present | Part I boots, plays its opening, and hits its own missing-data path. Not ours to handle |
| Part II blobs absent | *HEART OF THE ALIEN* lights but does not confirm, instead of `disc_open` failing |
| Neither game's data present | Menu draws from the committed `.ART`, neither entry confirms |
| `ANOTHER.BIN` read fails mid-transfer | Abort before quiescing anything, restore the fade, return to the menu. Nothing has been overwritten yet, so this is recoverable — it is the last point at which that is true |
| Anything after the jump | Not recoverable and not detectable. The design's answer is to keep stage 3 short and ordered, not to try to unwind it |
| Both rips missing at kit time | `update.bat` reports each skip and still emits a disc; it boots to a menu that offers nothing, which is honest rather than broken |

## Testing

**Host unit tests.** `test_bootmenu.c` grows cases for the two flags: confirm on Part I
with and without `part1_available`, the same for Part II, both absent, and cursor movement
unaffected in every combination. The existing timing and `BOOT_MUSIC_INDEX` assertions are
untouched. `run_tests.sh` gains no new binary — these are cases in a suite that exists.

**`chainload_srl.cxx` is not host-testable at all.** It is SH-2 teardown and a jump. No
test structure improves that, which is the whole reason both availability flags live in
`bootmenu.c` instead.

**CI proves the disc, not the handoff.** The `release.yml` smoke test runs `update.bat` on
a copy of the staged kit, de-raws the finished MODE1/2352 track, and requires
`ANOTHER.BIN`, `OPENING.CPK`, `BANK01`, `BANK0D`, `MEMLIST.BIN` and our own blobs all at
the volume root, plus IP.BIN byte-identical across the rebuild. That is the first
automated proof that both games coexist on one disc.

**Mednafen proves the handoff, and that is the user's to run.** Signals, in the order the
risk register predicts:

| Symptom | Suspect |
|---|---|
| Crash immediately after the jump | Stale code in I-cache; the uncached-mirror copy is not doing its job |
| Hang during the black, before Part I draws | Slave SH-2 executing freed HWRAM mid-copy |
| Part I boots silent, or hangs at its `SND_Init` | SCSP driver state carried over |
| Part I comes up but cannot open `BANK01` | `GFS_Reset()` insufficient |
| Stall on Part I's first disc read | Part I meeting a 41-track TOC it has never seen |

The first signal is free: Part I opens with `OPENING.CPK`. If that movie starts, the jump
landed, SRL came up and the CD block is serving reads — three of those five rows cleared
in one observation, before Part I draws a frame of its own game.

## Build and test

```
cd saturn && bash compile.bat            # Part II only, entry inert
cd saturn && make all HOTA_PART1=1       # both games
bash tests/run_tests.sh                  # host suites
```

## Build order

Chosen so the risky work happens against a disc that is already trusted:

1. `bootmenu` availability flags and their host tests. Pure C, no hardware, no disc.
2. `fetch.sh`, `CONFIG.ME`, the makefile wiring and `.gitignore`. Verifiable by listing the
   built ISO; still no runtime risk.
3. `chainload.{h,cxx}` and the `boot_sequence()` call site. The emulator work.
4. The merged kit and the two workflows.

Steps 1 and 2 are independently useful: after step 2 the disc carries both games with the
entry still inert, which is a shippable state if step 3 needs more than one sitting.

## Decisions worth naming

### The entry keeps the original's label

`MENUOOTW.ART` is cropped out of a screengrab of the Sega CD menu by `mkbootart.py`, and
every other piece of boot art comes from the same capture. Relabelling it *ANOTHER SATURN*
would mean hand-authoring art that the capture cannot supply, and turning a pure
derivation into one with an exception. The disc is called *Parts I and II*; keeping the
original's own words is the smaller change and the more faithful one.

### Heart of the Alien boots, rather than a third launcher program

A small launcher as `0.BIN` would spare both engines from unwinding their own hardware
state, which is the genuinely hard part of this work. It was not chosen because the boot
sequence, its art loading and its CD-DA already live in `main.c` and `saturn_bootart.cxx`,
tuned against a capture; moving them into a third build target to gain a cleaner teardown
trades certain rework for uncertain benefit. If the teardown proves unrecoverable, this is
the fallback, and the intermediate step is cheaper: move the game-select menu earlier so
less of the engine is up when the jump happens.

### Part I gets its own SaturnRingLib checkout

Sharing ours would put two configurations on one `preloader.o` path with make declining to
rebuild it. See the configuration table above. The second checkout is gitignored and
borrows our `Compiler/` through `COMPILER_DIR`, so the cost is source, not toolchain.

### The copy goes through the uncached mirror

Writing the new program through `0x2600_0000` rather than `0x0600_0000` means the SH-2
instruction cache cannot hold a line of Heart of the Alien code over an address that now
contains Another World. Purging the cache explicitly would also work; the mirror is fewer
moving parts and cannot be half-done.

### Both games ship their opening art in the base ISO

`ANOTHER.BIN` is our build output. `OPENING.CPK` is a committed file that Another-Saturn's
own `release.yml` already ships inside its public engine-only disc. Including both follows
upstream's existing call rather than making a new one about what is redistributable.

### The gated-data assertion covers both games

Another-Saturn's release workflow greps its listing for `BANK`. Ours must refuse on either
game's gated set — `bank*`, `BANK*`, `MEMLIST.BIN` **and** the 19 blob names — checked
both in `saturn/cd/data` and inside the built ISO. Two games on one disc means two ways to
accidentally redistribute someone else's data, and the inherited check would catch one.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| SCSP driver state survives into Part I's `SND_Init` | Stop the driver in stage 3. Most likely single failure; first thing to bisect |
| `GFS` cannot be re-initialised in a running session | `GFS_Reset()` before the jump. Unverified |
| Slave SH-2 still running during the copy | Halt it in stage 3 |
| I-cache serves stale code at `0x06004000` | Copy through the uncached mirror |
| Part I stalls on a 41-track TOC | Unknown. `mem/srl-toc-and-resume-are-broken.md` already records that SRL's TOC handling is wrong, so this is a known-bad area meeting a new input |
| Insufficient free LWRAM for the staging buffer | Free the engine's own LWRAM allocations first; nothing returns to the game |
| Upstream drifts and `PART1_REF` goes stale | The ref is pinned and bumped deliberately. CI builds what the pin says, so a broken upstream cannot break our release without a commit here |
| `preloader.o` collision between the two configurations | Separate SDK checkouts |

## Deferred

- Returning to the boot menu from Part I.
- Sharing save slots or a backup-RAM namespace between the two games.
- A launcher program as `0.BIN`, held as the fallback described above.
- Any `SGL_MAX_POLYGONS` / texture-slot reconciliation between the two builds. They are
  separate binaries with separate work areas and need none.

## Out of scope

- Changes to Another-Saturn of any kind.
- The Sega CD version of *Out of This World*. Part I here is the DOS release as
  Another-Saturn ports it, which is a different game from the one the original disc's
  Part I holds.
- Replacing the boot menu's two-entry geometry, which is measured from the capture.

## Acceptance

1. `bash tests/run_tests.sh` passes, including new `test_bootmenu.c` cases for both
   availability flags.
2. `make all HOTA_PART1=1` produces a `BuildDrop` ISO whose root listing contains
   `ANOTHER.BIN`, `OPENING.CPK` and our own blobs and `.ART`.
3. `make all` with `HOTA_PART1` unset produces today's disc, byte-comparable in structure,
   with *OUT OF THIS WORLD* inert.
4. In Mednafen: selecting *OUT OF THIS WORLD* fades to black and Part I's opening movie
   plays.
5. In Mednafen: selecting *HEART OF THE ALIEN* behaves exactly as it does today.
6. `release.yml` on a manual run produces a kit whose smoke test finds both games' files
   at the volume root with IP.BIN intact, and whose gated-data assertion passes.
7. A kit run with only one rip available emits a disc on which that game's entry confirms
   and the other's does not.
