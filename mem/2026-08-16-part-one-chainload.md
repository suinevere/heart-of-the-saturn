---
name: 2026-08-16-part-one-chainload
description: Part I chain-load shipped on port/subtitle-and-save-menus and failing on hardware at or after the quiesce; staging is proven good, a diagnostic build holds the failing stage on screen, and Another-Saturn must be tagged before fetch.sh resolves at all.
metadata:
  type: project
---

The boot menu's *OUT OF THIS WORLD* entry chain-loads Another-Saturn's program over ours.
Implemented, reviewed and squashed on **`port/subtitle-and-save-menus`**; **not working on
hardware**. Read the documents rather than re-deriving them:

- Spec, including the amendment that replaced building Part I with downloading it:
  `docs/superpowers/specs/2026-08-16-hota-saturn-part-one-chainload-design.md`
- Plan: `docs/superpowers/plans/2026-08-16-hota-saturn-part-one-chainload.md`
- Execution ledger — every ruling, every deferred minor, both fix waves:
  `.superpowers/sdd/2026-08-16-hota-saturn-part-one-chainload/progress.md` (gitignored)
- Per-task reports incl. the hand-back symptom→cause table: same directory, `task-4-report.md`
- Feature squashed to `3813cee`; `origin/main` still holds the 18 individual commits.

## Where it actually is

Staging is **proven good on hardware**. The last run reached the `staged, jumping` print,
and the diagnostics read `bytes 216856`, `s106`, `ss2048` — file, download, sector rounding
and `LoadBytes` are all correct. The failure is in the six quiesce statements or the jump.

`chainload_run` records progress in `g_chainDiag[6]` behind magic `0x5a17c0de`, and
`chainload_hold()` now parks that on screen for ten seconds on **every** exit path.
Stage numbers: 1 opened, 2 sized, 3 allocating, 4 trampoline alloc, 5 about to read,
6 read returned, 7 quiescing, 8 about to jump, 100 menu reached but `chainload_run`
never entered.

**Stage 7 vs 8 is the whole question.** 7 means a quiesce call hangs; 8 means the
trampoline or the jump is at fault. The six quiesce statements were written independent
and consecutive precisely so they can be removed one at a time — do that, do not rewrite
the function.

**Superseded, 2026-08-16.** The hold could never answer that: it was placed *after* the
quiesce, and its `boot_art_present` is `SRL::Core::Synchronize`, a wait on a vblank the
quiesce has just masked at the SCU and at the SH-2 — so it hung on its first present,
before the line that lights the screen, and the copy and the jump have never once run.
`4076770` replaces it with a `printf`, which `SRL::Debug::Print` puts straight into VDP2
VRAM and needs no vblank. **`chainload_hold` may only be called while interrupts live.**
If `quiesced, jumping` now appears and the screen stays black, the quiesce is clean and
the fault is the trampoline or the jump.

Two things checked against the toolchain rather than assumed, so the quiesce is not the
next suspect: SGL's own entry sets `SR` to `0xf0` itself (`SGL_Start`, `LIBSGL.A` section
`SLSTART`), so masking to level 15 is the state it expects; and `slInitSystem` writes the
SCU mask from its own `_IntPrioMask` table and drops `SR` back at the end, so
`SYS_SETSCUIM(0xffffffff)` is recoverable.

`quiesced, jumping` then printed on hardware, so the whole of `chainload_run` runs. The
stale-instruction-cache worry this note first raised is **not** a hazard: `SGL_Start`
(`LIBSGL.A`, section `SLSTART`) writes `0x11` to `0xfffffe92` itself — `mov #17, r0;
mov.b r0, @(2,r1)` with `r1` at `0xfffffe90` — so Part I purges and re-enables the cache
before `main`, and the only code reached before that is `PreLoader` and the ctors.

## The actual reason the entry did nothing: the disc has no Part I data

`Engine::init` calls `Resource::readEntries`, which opens `memlist.bin` and calls
`error()` when it is missing; `error()` ends in `exit(-1)` (`util.cxx:37-45`). Part I's
banks are named lower-case by `sprintf("bank%02x")` in its `bank.cxx`, and its
`sat_cd_open` upper-cases and appends the ISO9660 trailing dot.

**`tools/another/fetch.sh` deliberately never installs them** — it says so in its own
header, and only *stages* the data step into `tools/assets/part1/`. That step had never
been run, so `saturn/cd/data` carried `ANOTHER.BIN` and `OPENING.CPK` and nothing else of
Part I's. So the chain-load handed the console to a program that initialises SRL, blanks
the screen, and exits before drawing anything.

The 14 files (`bank01`..`bank0d`, `memlist.bin`) are now in `saturn/cd/data`, extracted
from the user's own `Another World (USA) - Complete` disc rather than by downloading
`GAME_URL`. All 14 are covered by `.gitignore`'s `BANK*`/`bank*`/`*.bin` rules, so they
stay off the repository, and nothing in `clean` removes them — `part1_clean` deletes only
`ANOTHER.BIN` and `OPENING.CPK`. They are **not reproducible from a clean checkout**: a
fresh clone must run `tools/assets/part1/data.bat`, which the build never calls because it
downloads gated data. `HOTA_PART1=1` will otherwise author a disc whose menu entry loads a
program that exits on the missing `memlist.bin`, exactly as above.

Installing them broke the build once, fixed in `3a56cde`: `tools/assets/data.bat` counts
Heart of the Alien's 19 blobs as `*.bin` minus `0.bin`, and `memlist.bin` matched, giving
"expected 19 data blobs, installed 20". `ANOTHER.BIN` matches the same glob and only
escaped because `part1_clean` deletes it before each count. Both are excluded by name now,
in both halves of the polyglot. The same miscount also defeated the already-installed
guard at the top of that script, so every build was re-extracting the whole rip.

## Mednafen save states ARE readable — the earlier note was wrong

`mcs/*.mc?` is **gzip**. `gzip.decompress` gives an `MDFNSVST` blob: 32-byte header, a
302x240x3 RGB preview, then sections of `char name[32]` + `uint32 size` + entries of
`uint8 namelen` + name + `uint32 size` + data. `MAIN/WorkRAMH` and `MAIN/WorkRAML` are flat
1 MB each and **byte-swapped** (`b[i^1]`); `SH2-M`/`SH2-S` carry `PC`, `R` (r0-r15) and
`CtrlRegs` = SR, GBR, VBR; `SMPC` carries `SlaveSH2On`. The previous session's byte-search
hit `CDB/Buffers->Data`, the CD block's buffer, which is why matches looked non-linear.
This channel answers everything the on-screen diagnostics were being built to answer —
use it before adding another printf. Parser: see this session's scratchpad, ~40 lines.

## What the states proved

- HWRAM at `0x06004000` is a **100.0000% byte match for Part I's image**. The staging, the
  trampoline and the jump are all correct and always were.
- Part I's `PreLoader`, its constructors and `SGL_Start` all run: `GBR = 0x060ffc00` and
  `SR = 0x000000f0` are exactly what `SGL_Start` sets.
- The master then sits at `PC = 0x06000956`, which is `ldc r0,sr; bf -2` at `0x0600094e` —
  the BIOS hang stub wired to vectors 4, 6, 9 and 10 (illegal instruction, slot illegal,
  CPU address error, DMA address error). The stacked frame at `R15` gives faulting
  `PC = 0x06000348`, which holds `0xffff`. Part I took a CPU exception.

## The cause: the slave processor was never actually halted

`SMPC/SlaveSH2On` reads `01` in **every** state, before and after the jump, across four
runs; the slave PC walks from `0x06025bba` to `0x0602613a`, straight through the copy.

`slSlaveOffWait()` is `slRequestCommand(SMPC_SSHOFF, SMPC_WAIT)`, and `_slRequestCommand`
opens with a semaphore acquire and `cmp/eq #0,r0; bf` to its exit — it returns -1 having
done nothing when it cannot have it, and SGL reads the pads through that same SMPC port
every frame. A fifth silent failure, same shape as the four below.

Two things settle that leaving it halted is correct: `slInitSystem` only ever requests
commands 11, 13 and 14, never 1 (`SSHON`), so SGL assumes the boot left the slave running
and never starts it; and Part I references the slave nowhere. Also note the BIOS slave
entry at `0x06000250` held `0x06014450`, inside the overwritten window, so restarting it
would have been worse than leaving it down.

`9952568` replaces the call with `chainload_slave_off`, which writes `SMPC_COMREG`
(`0x2010001f`) directly with the documented `SF` (`0x20100063`) handshake, bounds both
waits, and **returns a result the caller checks** — a refusal now returns to the menu
rather than jumping into a guaranteed crash. It runs before `GFS_Reset` so that return is
still safe.

## The real cause: BIOS state below the entry point, which the copy cannot reach

The copy rewrites `0x06004000` upward. **`0x06000000`-`0x06004000` is the BIOS work area
and is never rewritten**, so Part I inherits ours. A menu-time state has 22 longwords in
there pointing into the window about to be overwritten; six are the BIOS user-interrupt
hook table at `0x06000a00`:

| slot | vector | held |
|---|---|---|
| `0x06000a00` | `0x40` vblank-in | `0x0602cbf6` |
| `0x06000a04` | `0x41` vblank-out | `0x0602cd4e` |
| `0x06000a1c` | `0x47` system manager | `0x0602d384` |
| `0x06000a24` | `0x49` level-2 DMA end | `0x0602cbe4` |
| `0x06000a28` | `0x4a` level-1 DMA end | `0x0602cbd2` |
| `0x06000a2c` | `0x4b` level-0 DMA end | `0x0602cbc0` |

Masking interrupts before the jump does not cover this, because **Part I lifts the mask
itself**: `slInitSystem` sets SR back partway through its own run (`jsr @r13` with `r4 = 0`)
and only re-hooks afterwards; SRL's `slIntFunction` is later still. The first vblank in that
window is dispatched into Part I's data. That is why the faulting PC differs run to run
(`0x06000348` once, `0x00000002` the next, `GBR` clobbered to `0x00018405`) while the
landing spot is always the BIOS illegal-instruction stub.

`smpsys.c:156-157` does precisely this cleanup for its own two hooks before jumping to
`APP_ENTRY`, commented "hook re-initialisation". `79cf69d` clears `0x40`-`0x5f` through
`SYS_SETUINT`/`SYS_SETSINT`.

**`79cf69d` works.** Confirmed by reading the bytes each hook points at: in Part I's image
every one starts `2f 06` (`mov.l r0,@-r15`, a handler prologue); in ours they are mid-function
debris. The six slots now hold **Part I's own handlers**, so it reaches `slInitInterrupt`
inside `slInitSystem`. Do not compare a pre-jump table against a post-jump one to decide
this — that comparison is meaningless and cost an hour here.

## Where it stands, and the next fault

Part I still ends in the same BIOS stub, but later and from a different cause: the stacked
frame gives faulting `PC = 0x00000002` with `SR = 0x000000f0`. Interrupts are still masked,
so this is *inside* `slInitSystem`, before it lifts the mask — a call through a pointer
holding 2. Ruled out by reading the state:

- every BIOS pointer `slInitSystem` calls through is intact — `*(0x06000280) = 0x06000810`,
  `*(0x06000320) = 0x060006b0`, `*(0x06000340) = 0x060007b0`, `*(0x06000344) = 0x060007c0`
- no slot in the hook table `0x06000900`-`0x06000b00` holds a small value
- the master's own vector table `0x40`-`0x4b` is byte-identical to its pre-jump contents
- `*(0x06000324) = 0` (clock mode), so `SYS_CHGSYSCK` is skipped and does not reset the SMPC
- the 79 stale-looking pointers in `0x060fb800`-`0x060ffc00` are all **below** the stack
  pointer — dead frames, not live state. So is `SYS_SETSINT`: it writes through the *calling*
  CPU's VBR, but the vector table is provably unchanged, so it is inert here, not harmful.

**Replaying the IP is not available.** It is still resident at `0x06002000` (header intact,
app entry `0x06004000` at `+0xf0`, code from `0x06002100`), but `smpsys.c` keeps `sequence`,
`vramptr`, `cramptr` and `vbIcnt` as statics that have already run to completion, and
nothing re-zeroes them. Re-entering it would clear the wrong quarters of VDP2 VRAM. It would
have to be re-read from the disc's first sectors first, which GFS cannot do by LBA.

## The recommendation

Every failure this session has been **inherited machine state**, found one at a time from
save states at a build cycle each. That is the wrong end of the problem: we are
reconstructing Part I's requirements by inspection when Part I knows them. The durable fix
is a sanitise step at the top of Another-Saturn's own `sat_boot_init`, before
`Core::Initialize` — it is in time because we hand over with interrupts fully masked and
Part I does not lift the mask until `slInitSystem`, it is testable in that project's own
tree, and it makes the published artifact bootable from any host rather than only this one.
What cannot move there is anything that must happen *before* the copy, i.e. halting the
slave.

## Four traps that all failed silently

- **`sound_done()` is declared but not linked.** `makefile:103` filters `src/sound.c` out of
  `SOURCES`; `sound_srl.cxx:72-75` says so outright. Verifying a symbol's *declaration*
  proves nothing here — check it reaches the SH-2 link. Now `sound_flush_cache()`.
- **`slSlaveFunc(NULL, NULL)` does not halt the slave.** It *registers a function for the
  slave to run*. The halt is `slSlaveOffWait()` (`sl_def.h:2333`, SMPC `SSHOFF`). Same
  class of error as above: right signature, wrong semantics.
- **`game2bin_alloc()` in `initialize()` starved the chain-load.** `GAME2BIN_SIZE` (409,600)
  plus `MEMORY_SIZE` (524,288) is 933,888 of LWRAM's 1,048,576, and every engine `malloc`
  lands there (`saturn_compat.cxx:259`). That left ~82 KB against the 217,088 staging needs.
  Moved to `load_part2_data()`; see its banner in `main.c`.
- **`boot_art_load` hides NBG0, which is the layer `printf` reaches** through
  `saturn_compat.cxx`'s `diag_write`. Every diagnostic this seam emitted landed on a hidden
  layer. `SRL::Debug::Print` also never clears the rest of a row, so a short line leaves the
  tail of a longer one behind it — stray digits on screen are debris, not values. Pad to
  `DIAG_COLS` (40).

## Do not read Mednafen save states by byte-searching

**Stale — the conclusion drawn here was wrong. See "Mednafen save states ARE readable"
above.** Byte-searching a state does fail, and the 2048-byte non-linear matches were real,
but they were the CD block's sector buffer, not evidence that the states are unreadable.
Parse the container instead. The diagnostic scaffolding this section was used to justify
cost several rebuild-and-run cycles that one parser would have replaced.

## Blocking, before any of the above can be re-run end to end

`tools/another/fetch.sh` now downloads from
`https://github.com/suinevere/Another-Saturn/releases/latest/download` and verifies against
the published `SHA256SUMS`. **Another-Saturn has no tag and no release**, so a clean
`HOTA_PART1=1` build 404s. Two commits on its `feature/publish-program-assets` branch
(`604c077`, `c645002`) add the assets — `0.bin`, `OPENING.CPK`, `data.bat`, `CONFIG.ME`,
`SHA256SUMS` — to both its pipelines. **Merge that branch and cut a tag first.** A local
`.another/` cache currently masks this.

## Still to do once it works

- Delete the diagnostic scaffolding: `g_chainDiag`, `chainload_hold`, `CHAINLOAD_HOLD_MS`,
  the `printf`s, and the stage-100 write in `chainload_available`. All marked
  `DIAGNOSTIC BUILD ONLY`.
- Acceptance criteria 4-7 in the spec have never passed; 1-3 and 6 are structural only.
- Deferred minors are triaged in the ledger — the final review ruled `saturn/makefile`'s
  stale `saturn_audio.cxx` reference fix-now (done) and the rest acceptable.
- Bumping the Part I pin: the spec's file inventory is a snapshot of one upstream commit.
  The `REFAUD.CPK` compile switch it cites was already removed at Another-Saturn's HEAD.
  Re-check the copy allowlist against the new tree, not against the spec.

## Suggested skills

- **`superpowers:systematic-debugging`** for the stage 7/8 split. The Iron Law matters here:
  three of the four traps above were found by reading rather than guessing, and every
  speculative fix in this session cost a full rebuild-and-run cycle from the user.
- **`superpowers:verification-before-completion`** before claiming any of this works. Nothing
  in this feature has ever been compiled or run by an agent; the SH-2 toolchain and Mednafen
  are the user's, and *they run every build* — never invoke `make`, `compile.bat` or the
  emulator. `bash saturn/tests/run_tests.sh` is host gcc and is fine.
- **`superpowers:subagent-driven-development`** only if a new plan is written; the existing
  one is fully executed.

Related: [[2026-08-14-subtitle-and-save-menus]] for the boot menu and fade layer this builds
on, [[2026-08-13-boot-sequence-build]] for the game-select card and its art pipeline.
