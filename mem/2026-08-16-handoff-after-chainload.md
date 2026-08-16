---
name: 2026-08-16-handoff-after-chainload
description: Session handoff for 2026-08-16 — the Part I chain-load now works on hardware and is squashed onto main; what is left open, what state the two repositories are in, and which skills the next session should invoke.
metadata:
  type: project
---

The Part I chain-load works on hardware and is merged. **The technical account is
[[2026-08-16-part-one-chainload]] — read that first; this file does not repeat it.** It
covers what was actually wrong, where the fix now lives, how to read a Mednafen save state,
and the traps that cost build cycles.

## Repository state

- `heart-of-the-saturn` `main` is `40c03b8`, one squashed commit, **force-pushed**. The
  22-commit history is preserved on `origin/port/subtitle-and-save-menus` (`02cf89a`) for
  comparison; the trees were verified identical before the push.
- `Another-Saturn` `main` is `96e5c58`, released as **v0.0.2**. Our disc resolves
  `releases/latest`, so bumping Part I means cutting a tag there, not changing anything here.
  Its own note is that project's `mem/chain-loadable-boot.md`.
- Uncommitted and deliberately left alone in the working tree: `.idea/vcs.xml`,
  `.idea/runConfigurations/run_with_mednafen.xml`, `saturn/compile.bat`. None is touched by
  the branch.

## Open

- **Deferred by the user mid-session, not investigated:** after a death, the last frame held
  behind the load menu is the password/checkpoint screen. It should be the in-game frame, or
  failing that a clean fade to black. The relevant seam is the `0x21` opcode handler in
  `saturn/src/decode.c` and `death_played` / `next_script`, whose banners in
  `saturn/src/main.c` claim the surviving task never draws — evidently it does. Start there.
- **Part I's bank data is not reproducible from a clean checkout.** `saturn/cd/data` holds
  `bank01`..`bank0d` and `memlist.bin`, gitignored and surviving `clean`, but the build never
  installs them: `tools/assets/part1/data.bat` downloads gated data and is a manual step. A
  fresh clone with `HOTA_PART1=1` authors a disc whose menu entry loads a program that exits.
- **The save-state parser is ephemeral.** It lived in this session's scratchpad only. Its
  format is documented in [[2026-08-16-part-one-chainload]] and is ~40 lines to rebuild, but
  it earned its keep several times over and arguably belongs in `tools/`.
- Acceptance criteria 4-7 in the chain-load spec have now been met on hardware but were never
  formally re-walked against the document.

## How this session actually made progress

Worth repeating because it inverted the cost of every step: reading the emulator's save state
answered in one pass what on-screen diagnostics could not answer in a build cycle. Registers,
both work RAM banks, the SH-2 vector tables, the SMPC and a screenshot are all in there. Two
of my own conclusions were wrong until checked that way — that the interrupt-hook fix had
failed, and that a correct binary was a stale build — so prefer the state over inference, and
prefer diffing an artifact over searching it for constants.

## Suggested skills

- **`superpowers:systematic-debugging`** for the death-frame bug. The Iron Law earned its
  place here: every fix that stuck came from reading (the SGL disassembly, `smpsys.c`, the
  save state), and every speculative one cost the user a full rebuild-and-run.
- **`superpowers:verification-before-completion`** before claiming anything works. Nothing in
  this feature is compiled or run by an agent — the SH-2 toolchain and Mednafen are the
  user's and *they run every build*. Never invoke `make`, `compile.bat` or the emulator.
  `bash saturn/tests/run_tests.sh` is host gcc and is fine, and a single-translation-unit
  `sh-elf-gcc -fsyntax-only` with the SDK include set catches compile errors without a build.
- **`superpowers:brainstorming`** before any new feature work, per the usual cadence.

Related: [[2026-08-16-part-one-chainload]], [[user-runs-the-build-and-emulator]],
[[hota-port-reference-repos]].
