---
name: 2026-08-06-input-and-endian-sprite-bug
description: Session handoff at 94aee4b — CD-DA and input sub-projects shipped and verified; a big-endian type-mismatch bug in the sprite free list found and fixed, awaiting the emulator run that confirms it.
metadata:
  type: project
---

Repo: `C:\Users\saggl\CLionProjects\heart-of-the-saturn`, branch `main`.
Head at handoff: `94aee4b`.

Everything below assumes you have read, and does not repeat:

- CD-DA spec/plan: `docs/superpowers/specs/2026-08-04-hota-saturn-cdda-design.md`,
  `docs/superpowers/plans/2026-08-04-hota-saturn-cdda.md`
- Input spec/plan: `docs/superpowers/specs/2026-08-05-hota-saturn-input-design.md`,
  `docs/superpowers/plans/2026-08-05-hota-saturn-input.md`
- Commits `3051d5c..94aee4b`. Both specs carry a filled-in Acceptance section
  recording what was verified and what was not; the CD-DA spec also has a
  "Known gaps left behind" section that replaced its deleted execution ledger.

## Where things stand

Two sub-projects shipped since the boot-and-video work, both reviewed and both
confirmed by Suinevere on the emulator:

- **CD-DA music** (`3051d5c..2b530c4`). Music plays under all four intro
  animations and survives the whole-file load that precedes each.
- **Input** (`3893b43..9e7525f`). Port 0's pad drives the seven key globals.
  Confirmed working: the intro skips on A, and a probe showed `data=fbff` on A.

`HOTA_AUDIO` now defaults to `full` (`0bb099e`), so a bare `./compile.bat` lays 42
tracks and produces a ~443 MB disc.

## The one thing most worth knowing

Getting past the intro exposed a bug that had been latent since the port began,
and it is the reason to distrust "the host works, so the engine is fine":

```c
sprites.c   int  first_sprite, sprite_count, last_sprite;      /* definitions */
decode.c    extern char first_sprite, last_sprite, sprite_count;  /* was here */
```

The linker matches names, not types. `decode.c` read the first byte of a 4-byte
`int` — the *low* byte on a little-endian host, which for values 0..63 is the
right answer, and the *high* byte on big-endian SH-2, which is always 0. So
`last_sprite` read 0, sprites loaded into slot 0 instead of the free-list head,
and `sprite_count` read 0 forever, which made `add_sprite` take its
first-sprite branch every time.

Fixed in `94aee4b` by moving the declaration into `sprites.h`, which both files
already include, so the compiler now checks it against the definition. Changing
`char` to `int` in place would have fixed the symptom and left the trap armed.

**Swept for siblings:** every other cross-file global `decode.c` declares
(`task_pc`, `new_task_pc`, `enabled_tasks`, `new_enabled_tasks`, `current_room`,
`next_script`, `script_ptr`) matches its definition. One near-miss remains —
`variables` and `auxvars` are declared `unsigned short` against `short`
definitions. Same size, so no endianness exposure and identical behaviour on
both platforms, but it is the same class of defect and worth tidying.

## What is unverified right now

**`94aee4b` has not been run.** The disc is built at
`saturn/BuildDrop/Heart of the Alien (USA).cue`. On the code-entry screen the
highlight should sit over the first letter and move with the d-pad. If it does
not, the fix is wrong and the search resumes — do not layer a second fix on top.

## Process lesson from this session

Three wrong theories cost three rounds: stale input state, uninitialised VM
memory, and endianness in the VM accessors (which are correctly byte-assembled).
What solved it was one probe printing `load_sprite`'s inputs and outputs:
Saturn showed `e=0` where the host showed `e=1`, with the resource pointers
`tbl` and `a4` identical — which localised the fault to the free list and
exonerated the whole data path in a single line.

Two techniques worth reusing:

- **The host build is a headless oracle.** `SDL_VIDEODRIVER=dummy
  SDL_AUDIODRIVER=dummy timeout 15 ./saturn/src/alien.exe --debug --room 7`
  runs without a display and prints `LOG` output. `--room N` jumps straight to a
  script, so ground truth for any screen is obtainable without an emulator and
  without costing Suinevere a round trip. Put probes in shared engine code and
  both platforms print the same line.
- **Instrument before theorising.** Same lesson as the first-boot panic; it has
  now cost two sessions.

## Open items, none blocking

- **`-DBYPASS_PROTECTION` is dead** (`saturn/makefile:74`). No source in the repo
  reads it and the `engine.cxx` its comment cites does not exist here — that
  comment describes a different codebase. The code-entry screen is the original
  copy protection, reached deliberately by `main.c:571-576` (`next_script = 7`)
  and unplayable without a code. If skipping it is wanted, implementing the flag
  for real means setting `next_script` past 7 on Saturn.
- **`HOTA_AUDIO=none` does not shrink an existing disc.** After a `full` build it
  leaves the 42-track bin/cue in place while updating the data track, so the
  disc stays ~443 MB and `disc_open` still reports 42 audio tracks. Verify the
  artifact, not the build log.
- **`alien.exe` is untracked at the repo root** and `.gitignore` names test
  binaries individually, so it will keep showing in `git status`.
- The VM memory map is now zeroed at allocation (`b775148`). That was chased as a
  suspect for this bug and was not the cause, but it is a real difference from
  the host's zeroed `.bss` and the fix stands on its own merits.

## Suggested skills

- **`superpowers:systematic-debugging`** — for the next bug, and read the process
  lesson above first. The four-phase discipline is what eventually worked; the
  wasted rounds were all Phase 1 skipped.
- **`superpowers:brainstorming`** then **`superpowers:writing-plans`** — required
  before any new sub-project. Suinevere's cadence is strictly spec → plan → code,
  both date-prefixed under `docs/superpowers/`.
- **`superpowers:subagent-driven-development`** — for executing a written plan.
  Reviewer dispatches work now; three sub-projects have run this way.
- **`superpowers:verification-before-completion`** — this project has repeatedly
  produced claims that did not survive checking, including several of the
  controller's own numbers and one disc described from a build log rather than
  the artifact.

## What to do next

1. Confirm `94aee4b` on the emulator.
2. If confirmed, gameplay is reachable for the first time. Everything past the
   intro is unexercised: the 512 KB map bound, the delta-scratch relocation, and
   the CD-DA loop rule where a file read restarts a looping track instead of
   resuming it — see [[hota-cdda-seek-delay-and-loop-restart]]. Expect more
   first-contact bugs of the kind above and record them rather than absorbing
   them into whatever sub-project is running.
3. Sound effects are the last stubbed seam: `play_sample` and
   `sound_flush_cache` are no-ops in `sound_srl.cxx`. The CD-DA spec's
   "Deferred / stubbed" section describes the shape of that work.

Related: [[srl-toc-and-resume-are-broken]],
[[srl-peripherals-array-reads-connected]],
[[hota-cdda-seek-delay-and-loop-restart]], [[hota-port-reference-repos]].
