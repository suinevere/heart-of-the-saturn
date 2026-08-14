---
name: 2026-08-13-death-fade-shape-and-history-squash
description: The port/sfx branch was squashed to one commit and force-pushed over origin/main, so the 67-commit history now exists only in the local branch pre-squash-845ffac. Everything in that commit is unverified on hardware. Death-timing findings are in the death-animation handoff, not here.
metadata:
  type: project
---

## Read this first

Two things this session produced that nothing else records. Everything else is in
[[2026-08-11-death-animation-cdda-sync]], which was rewritten as the work happened — the
death timing model, the room-script disassembly, the knobs and their bracketing all live
there. Do not re-derive any of it here.

## The granular history has exactly one copy, and it is local

`main`, `origin/main` and `port/sfx` are all `6c88e69`, a single squashed commit whose tree
is `48839cc8`. `origin/main` was force-pushed over the 67-commit history that used to be
there.

**That history now survives only in the local branch `pre-squash-845ffac` (`845ffac`).**
It is not on any remote. Deleting that branch destroys the per-change record of the whole
sound-effects, disc-read, CD-DA and fade sub-projects — 67 commit messages that were
written to carry their reasoning. Push it somewhere before touching it:

```
git push origin pre-squash-845ffac
```

All three refs have the identical tree, verified by SHA rather than by diff, so nothing is
at risk content-wise. Only the reasoning is.

Branch layout is otherwise untidy and was not cleaned up: `main` tracks a `origin/main2`
that is gone, `port/sfx` tracks `origin/port/sfxbusted` (ahead 1, behind 70), and
`master`, `backup-before-reword`, `port/disc-read-speed` and `port/bincue-disc-backend`
are all stale. `fade-experiments` and `fade-experiments-2` hold discarded work and are
still the only copy of it; Suinevere has been offered their deletion twice and has not
answered.

## Nothing in that commit has run on hardware

Every death-path change was written, syntax-checked with the host `gcc -fsyntax-only`, and
committed. None of it has been through `compile.bat` or Mednafen — Suinevere runs both, and
nothing on this path is verifiable from a tool call. See
[[user-runs-the-build-and-emulator]].

The one exception is the fade *shape*, which was iterated against real runs: 1200 ms was
rejected as too long and 600 ms as too large a gap before the splat, which is how 960
ended up bracketed rather than merely defended. The fade **in**, the button-dismissed last
frame, and the `DEATH_LAST_FRAME_MIN_MS` floor have not been seen running.

> **STALE (2026-08-14).** The button-dismissed last frame and its
> `DEATH_LAST_FRAME_MIN_MS` floor are **deleted** — a terminal death now fades to black
> and sets `death_played`, which routes it to the save menu. They were never seen
> running and now never will be. The 960 in the paragraph above is also stale:
> `DEATH_CHAINED_HOLD_MS` reads 200, so the shipped hold is 1600 ms. See that constant's
> banner in `saturn/src/animation.c`.

## Loose ends

- `saturn/tests/run_tests_fadecalc.exe` is untracked and still not in `.gitignore`
  alongside its six siblings at lines 37-48. Offered, not answered.
- The throwaway room-script disassembler lives in this session's scratchpad
  (`disasm.py`, `deaths.py`) and will be lost with it. It is what found the three
  multi-segment death sites. Rebuilding it is a morning's work: operand widths derive
  mechanically from `decode.c`'s switch plus the helpers it calls, and code is told from
  data by decoding forward from many start offsets and keeping the ones that agree.
  Worth moving into `tools/` if the script is ever interesting again.
- Working tree is dirty and was left that way: `.idea/runConfigurations/run_with_mednafen.xml`,
  `.idea/vcs.xml`, the `SaturnRingLib` submodule pointer, plus untracked `alien.exe`.

## Suggested skills

- `superpowers:verification-before-completion` — the whole death path is unverified.
  Nothing here may be reported as working until Suinevere reports back from hardware.
- `superpowers:systematic-debugging` — before any further change to the death timing.
  Phase 1 is the step this area has historically skipped, at a cost of four regressions.
- `superpowers:test-driven-development` — only the pure arithmetic is host-testable
  (`fadecalc`, `discfmt`, `cdtoc`, `cdda_classify`, `sfxconv`). Anything new that can be
  pushed into that shape should be.
- `superpowers:finishing-a-development-branch` — if the branch tangle above gets cleaned
  up, rather than deleting refs by hand.

Related: [[2026-08-11-death-animation-cdda-sync]], [[2026-08-08-saturn-sfx-silent-in-gameplay]],
[[user-runs-the-build-and-emulator]], [[hota-original-hides-cdda-latency-behind-fades]]
