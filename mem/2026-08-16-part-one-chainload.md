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

Tried and refuted. Every probe into our own `0.bin` matches for exactly 2048 bytes at
non-linear offsets — the states compress memory blocks, so a `find` hits incidental raw
fragments, not RAM. A magic word read out of one is meaningless, and `stage=0` read that way
was an artefact. The on-screen hold exists because this channel does not work.

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
