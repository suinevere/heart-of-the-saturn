---
name: 2026-08-08-saturn-sfx-silent-in-gameplay
description: RESOLVED 2026-08-10 — sound effects were silent in gameplay because the engine's volume was mapped onto slPCMOn's dB attenuation as if it were linear; fixed in 21e24fb and confirmed audible. Kept for the refuted hypotheses and the cycle-costing traps, which are still accurate.
metadata:
  type: project
---

## RESOLVED — 2026-08-10

**The bug is closed.** Effects are audible in gameplay, confirmed by Suinevere.

**Root cause:** not the flush hypothesis this handoff was chasing. `sfx_level_for_volume`
mapped the engine's 0-255 volume onto `slPCMOn`'s `level` as if that field were linear
amplitude, the way SDL_mixer's is. It is dB attenuation, so volume 50 landed near -72 dB
— inaudible. Fixed in `21e24fb`, along with the `slPCMStat` guard and the file-scope
`g_pcm[4]` this handoff describes as in flight; those landed in `76eb7c6` and `06e1960`.

**The open experiment is answered.** The `HOTA_AUDIO=none` disc was finally run on
2026-08-10 as a two-arm differential. Stomps were audible in **both** arms — CD-DA does
not suppress PCM on the shared SCSP. Do not rebuild a data-only disc to test this.

**Still true and worth reading below:** the five refuted hypotheses, "`P` climbing proves
less than it looks", and everything under "Things that will cost you a cycle".

**Left undone:** the fix has no regression test. `sfx_level_for_volume` is `static` in
`sound_srl.cxx`, which pulls in SRL and cannot be host-compiled, so the highest-risk
arithmetic in the feature is provable only by ear. `cdda_classify.c`, `cdtoc.c`,
`discfmt.c` and `sfxconv.c` were each split out of an SRL-dependent file for exactly this
reason; this function did not get that treatment. Extracting it is the outstanding work.

Everything from here down is the original handoff, preserved as written.

---

Repo: `C:\Users\saggl\CLionProjects\heart-of-the-saturn`, branch **`port/sfx`**, stacked on
`port/disc-read-speed` at `21805a5`. Head at handoff: `763a562`.

Everything below assumes you have read, and does not repeat:

- Spec: `docs/superpowers/specs/2026-08-07-hota-saturn-sfx-design.md`
- Plan: `docs/superpowers/plans/2026-08-07-hota-saturn-sfx.md`
- Commits `21805a5..763a562` (14 of them; messages are complete)
- Prior handoff: [[2026-08-06-input-and-endian-sprite-bug]]

## Read this before trusting the execution ledger

The SDD ledger at `.superpowers/sdd/2026-08-07-hota-saturn-sfx/progress.md` has the
blow-by-blow, but **`.superpowers/` is gitignored and vanishes on `git clean -fdx`.**
Everything durable is duplicated below on purpose. Same trap the CD-DA spec hit.

## What shipped and is trustworthy

`saturn/src/sfxconv.{c,h}` — pure C, no SRL, includes only `vm.h` and its own header.
Locates a sample through the three indirections, bounds-checks every one, decodes 8-bit
sign-magnitude to signed through a 256-entry table, pads to `0x900`. Fourteen host tests
in `saturn/tests/test_sfxconv.c`, wired into `run_tests.sh`.

`saturn/src/system/sound_srl.cxx` — 256-slot LWRAM cache, `play_sample`,
`sound_flush_cache`, one-shot exhaustion warning. `.bss` is 2,052 bytes; `sfxconv.o`
contributes 0 (the table is `const`, so `.rodata`).

Verification worth not repeating:

- **Decode table** checked three independent ways, including against `sound.c`'s own
  two-branch form. `0x80 → -128` (sign-magnitude negative zero overflowing a
  `signed char`) is deliberate host parity, pinned by name.
- **`sfxconv_locate`** fuzzed 2,000,000 iterations: 19,652 accepts, zero range
  violations, zero writes to out-params on refusal.
- **Both bounds mutation-tested.** `>` → `>=` and a one-byte loosening are caught (3
  failures each); deleting the negative guard now segfaults the suite. Before that was
  pinned, all three deletions left the suite green — the upper bound was pinned to the
  byte and the lower bound was completely unguarded.
- **`slPCMOn` streams from LWRAM** — proven on hardware by the Task 1 spike.
- **`long` is 32 bits on both toolchains** (verified, not assumed, including
  `sh-elf-gcc-14.2.0`). The bounds check is safe because of its explicit `offset < 0`
  test and its subtraction-form comparison, not because of any widening.

## The bug

**Sound effects are silent during gameplay. They work in the opening scenes, in sync
with the video.** Confirmed by Suinevere on hardware, 2026-08-08.

Two diagnostic builds (`248a668`, `763a562`) put counters on screen. Gameplay reading:
`c`, `P` and `p` all climbing at the same rate; `L`, `A`, `R` all zero.

Refuted by evidence, do not revisit:

1. **Bounds check too strict.** Host oracle: linked `sfxconv` into the host build and
   printed its verdict beside `sound.c`'s own unchecked walk. Every sample in rooms 2
   and 8 agreed exactly. Arguments are normal too — channels 0-2, volumes 127 and 50.
2. **Engine never calls `play_sample`.** `c` and `p` climb 1:1 in gameplay.
3. **Channels stuck busy.** `R` is zero.
4. **LWRAM exhaustion.** `A` is zero.
5. **Location failing.** `L` is zero.

**`P` climbing proves less than it looks.** SRL's `IPcmFile::PlayOnChannel`
(`srl_sound.hpp:463-481`) calls `slPCMOn` and returns `true` unconditionally —
`slPCMOn`'s `int8_t` status is discarded. `P` counts "the channel was free and we issued
the call", not "the sample is playing".

## Current hypothesis and the fix in flight

The one event that happens before gameplay and never before the opening is
`sound_flush_cache`, which calls `slPCMOff` on all four channels at the first room load.
Every counter reading is consistent with `slPCMOff` leaving the channels unusable and
nothing restoring them.

Three compounding defects, all being fixed together:

1. `slPCMOff` is called on channels that are not playing — in the flush and before every
   play.
2. `PlayOnChannel` rewrites only `mode`, `pitch`, `level`, `pan`. It never rewrites
   `PCM.channel` (the SCSP slot), set once in SRL's static initialiser. Nothing
   re-initialises the struct after `slPCMOff` touches it.
3. `slPCMOn`'s status is discarded, so failure is unobservable.

**A fix was dispatched to a subagent and had not reported when this handoff was
written.** It abandons `MemPcm`/`PlayOnChannel`, adds a file-scope `PCM g_pcm[4]`,
guards `slPCMOff` behind `slPCMStat`, rewrites every field of the struct on every play
(`pitch = 0x69CE` for 8 kHz, derived in its banner), captures `slPCMOn`'s return into
`g_lastRc`, and surfaces `rc` on the diagnostic line. **Check `git log` first — it may
have landed.** If it did not, the instruction is reconstructible from the above.

If the fix does not work, `rc` names the reason in the same run. Do not ship a blind
second attempt; this bug was invisible only because three separate return values were
being thrown away.

## Also unresolved, deliberately not folded in

**CD-DA arrives 1-3 s late**, a noticeable gap when gameplay starts. The CD-DA spec
documents ~0.5 s from a near-full-stroke seek; this is worse. Pre-existing, different
code, its own sub-project. Recorded rather than absorbed, per the last handoff's
instruction.

A no-music disc (`HOTA_AUDIO=none`, 1 track, 9.4 MB) was built to test whether CD-DA
suppresses PCM on the shared SCSP. **It was never tested** — the flush hypothesis
overtook it. Still a live experiment if the fix fails.

## Things that will cost you a cycle if you do not know them

- **The host and Saturn builds clobber each other's object files.** `saturn/makefile`
  uses `%.o : %.c` with no object directory, so SH-2 objects land in `saturn/src/` under
  the names the host `Makefile` gives its x86 objects. Always clean when switching:
  `make -C saturn/src clean && make -C saturn/src`, or
  `cd saturn && ./compile.bat clean && ./compile.bat`. Symptom: `relocations in generic
  ELF (EM: 42)`.
- **`compile.bat` must run through PowerShell, not the Bash tool.** Line 1 is a POSIX
  shortcut Bash executes directly, skipping the batch half that puts the cross-compiler
  on `PATH`.
- **`SRL::Debug::Print` supports only `%c %s %0Nd %d %x %u %f`** — no width or flag
  syntax. `%-4d` prints `4d` literally. This cost a whole diagnostic build. Verifying a
  printf-like function's *signature* says nothing about its *specifiers*; read
  `snprintfEx`'s switch in `srl_string.hpp`.
- **Suinevere runs the emulator, never a tool call.** A subagent attempted to force-kill
  their Mednafen to unblock a build and was correctly blocked. Do not retry that.
- **Room 9 does not exist.** `load_room` substitutes one digit into `ROOMS0.BIN`; there
  is no `ROOMS9.BIN`. Only rooms 2 and 8 could be driven to fire the SFX opcode
  headless, and both figures are lower bounds — their runs stalled on input gates. Worst
  measured room is 20,804 bytes against an 81,916-byte LWRAM budget, but **there is no
  measured ceiling anywhere in the game.**

## Open work

1. **Land the fix, hand Suinevere a full-music disc** (~42 tracks, ~443 MB — confirm from
   the `.cue`, the last build was the 1-track no-music variant).
2. **Revert the diagnostic instrumentation** (`248a668`, `763a562`, plus the fix's
   counters) once the bug is closed. Commit messages already say they are temporary.
3. **Task 7** — write the spike answer, the corrected room coverage, the measured `.bss`
   figures and the hardware outcome into the spec's Risks and Acceptance sections, the
   way the CD-DA spec does.
4. **Final whole-branch review** — deliberately not run yet, because a failed hardware
   check can still change code. Three deferred items to triage: the negative-guard
   regression is caught as SIGSEGV rather than a clean assertion (fixing it means
   exposing `in_map`); room 8's stall is inferred, not trace-inspected; the cache is
   unbounded within the pool (~35 samples at the 2,304-byte floor, 15-20 realistically).
5. **The branch question, asked four times and never answered.** `port/sfx` stacks on
   `port/disc-read-speed`, which is itself awaiting emulator confirmation. Rebasing onto
   `main` gets more expensive with every commit.

## Two spec claims that are now known to be overconfident

Task 7 should record these as corrections, not quietly fix them:

- "Padding is harmless, because `play_sample` force-stops the channel before every
  play." Only true if stopping is synchronous — the assumption now under suspicion.
- "Silent failure is acceptable because it matches the host." The host prints to
  `stderr`. On Saturn every failure path is invisible, which is why a working feature
  and a totally broken one looked identical from the outside.

## Suggested skills

- **`superpowers:systematic-debugging`** — already in progress on this bug. Phase 1 is
  done and five hypotheses are refuted; resume at Phase 3/4, and do not skip the failing
  test before the fix.
- **`superpowers:subagent-driven-development`** — the plan is being executed under it.
  The ledger, task briefs and review packages all live in
  `.superpowers/sdd/2026-08-07-hota-saturn-sfx/`.
- **`superpowers:verification-before-completion`** — this sub-project produced several
  claims that did not survive checking, including a room-coverage figure that reached a
  source-file banner before anyone checked it.
- **`superpowers:requesting-code-review`** — for the final whole-branch review, on the
  most capable model.

Related: [[2026-08-06-input-and-endian-sprite-bug]], [[hota-cdda-seek-delay-and-loop-restart]],
[[srl-toc-and-resume-are-broken]], [[user-runs-the-emulator]],
[[hota-port-reference-repos]].
