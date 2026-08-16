---
name: 2026-08-16-part-one-chainload
description: The boot menu's Part I chain-load, working on hardware. What broke it was machine state below the entry point that the copy cannot reach, fixed on both sides; how to read a Mednafen save state, which is the tool that found all of it.
metadata:
  type: project
---

The boot menu's *OUT OF THIS WORLD* entry chain-loads Another-Saturn's program over ours.
**Working on hardware.** Read the documents rather than re-deriving them:

- Spec: `docs/superpowers/specs/2026-08-16-hota-saturn-part-one-chainload-design.md`
- Plan: `docs/superpowers/plans/2026-08-16-hota-saturn-part-one-chainload.md`
- Execution ledger: `.superpowers/sdd/2026-08-16-hota-saturn-part-one-chainload/progress.md` (gitignored)

## What the chain-load does

`chainload_run` (`saturn/src/system/chainload.cxx`) stages `ANOTHER.BIN` into Low Work RAM,
copies it over ourselves at `0x06004000` through the uncached mirror using a hand-encoded
trampoline that runs from LWRAM, and jumps. None of that was ever wrong. Every failure in
this feature came from **machine state the copy cannot reach**, and there is exactly one
place it lives: `0x06000000`-`0x06004000`, the BIOS work area below the entry point.

`smpsys.c` — the IP, in `SaturnRingLib/modules/sgl/IP/` — is the authority on what a program
at `0x06004000` may assume. It is worth reading before touching this.

## The four things that were actually wrong

1. **The diagnostic hold was itself the hang.** `chainload_hold` sat after the quiesce and
   its `boot_art_present` is `SRL::Core::Synchronize`, a wait on a vblank the quiesce had
   just masked. The copy and the jump had never run at all. `4076770`.
2. **The disc carried Part I's program but never its data.** `tools/another/fetch.sh`
   excludes it by design; `tools/assets/part1/data.bat` installs `bank01`..`bank0d` and
   `memlist.bin`, and had never been run. Part I reached `Resource::readEntries`, called
   `error()`, and `exit(-1)`. Not reproducible from a clean checkout — a fresh clone must
   run that step.
3. **The slave processor never stopped.** `slSlaveOffWait` reaches `slRequestCommand`, which
   takes a semaphore first and returns having done nothing when it cannot have it — and SGL
   polls the pads through that same SMPC port every frame. `9952568` writes `COMREG`
   (`0x2010001f`) directly with the `SF` (`0x20100063`) handshake and checks the result.
4. **Interrupt hooks pointing into the overwritten window.** Two separate tables, and this
   was the whole fight:
   - the BIOS user-interrupt hooks at `0x06000900 + vector * 4`, cleared by `79cf69d`
   - the FRT overflow handler, which `SRL::Timer::Init` writes **straight into the SH-2
     vector table** at `VBR + 0x66 * 4`, so `SYS_SETUINT` never reaches it. Fixed upstream.

   Masking interrupts before the jump does not cover either: **Part I lifts the mask itself**,
   inside `slInitSystem`, and only re-hooks afterwards.

## Where the fix lives now

Part I sanitises its own entry. `sat_boot_sanitize` in Another-Saturn's
`saturn/src/system/saturn_platform.cxx` runs before `SRL::Core::Initialize` and reasserts the
IP hand-off: FRT interrupts off first, slave off, SCU DSP and SH-2 DMAC quiet, BIOS hooks
released, SCU mask closed. See that project's `mem/chain-loadable-boot.md`.

That is the right side for it. This side was reconstructing Part I's requirements by
inspection, one save state per build cycle; Part I knows them, can test them in its own tree,
and the fix makes its published artifact bootable from any host. What cannot move there is
anything that must happen *before* the copy — halting the slave — which stays here.

## Reading a Mednafen save state

This is the tool that found all of it, and the earlier note saying it does not work was
wrong. `SaturnRingLib/emulators/mednafen/mcs/*.mc?` is **gzip**. Inflated, it is `MDFNSVST`:
a 32-byte header, a 302x240x3 RGB preview (a screenshot — extractable to PNG, and "all
pixels black" is itself a finding), then sections of `char name[32]` + `uint32 size`, each
holding entries of `uint8 namelen` + name + `uint32 size` + data.

- `MAIN/WorkRAMH` and `MAIN/WorkRAML` are flat 1 MB, **byte-swapped** (`b[i^1]`)
- `SH2-M`/`SH2-S` carry `PC`, `R` (r0-r15), `CtrlRegs` = SR, GBR, VBR, `SysRegs` = MACH,
  MACL, PR, plus `IPRA`/`IPRB`/`VCRA`-`VCRD`/`CCR`
- `SMPC/SlaveSH2On` says whether the slave is running

Mednafen's `PC` is the fetch stage, **4 bytes ahead** of the executing instruction. A master
stopped at `0x06000956` is spinning in the BIOS's `ldc r0,sr; bf -2` stub at `0x0600094e`,
which serves vectors 4, 6, 9 and 10 — illegal instruction and address error. The stacked
frame at `R15` then gives the faulting PC and SR, which is how each of these was located.

The previous session's byte-search hit `CDB/Buffers->Data`, the CD block's sector buffer,
which is why matches looked non-linear. Parse the container; do not grep it.

## Two traps that cost runs, both now closed

**The asset cache hid a republished release.** `fetch.sh` skipped any file already present
and the makefile passes no force flag, so a rebuilt binary stayed invisible — the release
workflow uploads with `--clobber`, so a tag is not a version. `0e4c600` re-fetches
`SHA256SUMS` every run and re-downloads only what fails its digest.

**Do not verify a fetched binary by searching it for constants.** `0xfffffe10` reaches a
register through `mov.w` on a *sign-extended 16-bit* pool literal (`fe10`), so a four-byte
search cannot hit it, and an unchanged file size proves nothing because padding absorbs small
growth. Diff the fetched image against the previous one instead — a save state holds it at
`WorkRAMH + 0x4000`. Differences outside the runtime-written window mean a different build.

## Four earlier traps that all failed silently

- **`sound_done()` was declared but not linked.** `makefile:103` filters `src/sound.c` out of
  `SOURCES`. Verifying a symbol's *declaration* proves nothing; check it reaches the SH-2
  link. Now `sound_flush_cache()`.
- **`slSlaveFunc(NULL, NULL)` does not halt the slave.** It registers a function for the
  slave to run. Right signature, wrong semantics — and `slSlaveOffWait` then had its own
  failure, above.
- **`game2bin_alloc()` in `initialize()` starved the chain-load.** `GAME2BIN_SIZE` plus
  `MEMORY_SIZE` is 933,888 of LWRAM's 1,048,576. Moved to `load_part2_data()`.
- **`boot_art_load` hides NBG0, the layer `printf` reaches.** Every diagnostic that seam
  emitted landed on a hidden layer.

## State of the tree

- Diagnostic scaffolding deleted in `e1d1d36`: `g_chainDiag`, `chainload_hold`, the
  `printf`s, the stage-100 write. The staging checks remain — they still return to the menu.
- Part I's data lives in `saturn/cd/data` (gitignored) and survives `clean`; `part1_clean`
  removes only `ANOTHER.BIN` and `OPENING.CPK`.
- `tools/assets/data.bat` excludes `ANOTHER.BIN` and `memlist.bin` from its 19-blob count
  (`3a56cde`) — both match the glob and either one fails the build.
- Another-Saturn is pinned by release, not by ref. Bumping it means re-checking the copy
  allowlist against the new tree, not against the spec.

Related: [[2026-08-14-subtitle-and-save-menus]] for the boot menu and fade layer this builds
on, [[2026-08-13-boot-sequence-build]] for the game-select card and its art pipeline.
