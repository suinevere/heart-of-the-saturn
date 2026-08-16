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
