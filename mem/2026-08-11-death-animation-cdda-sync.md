---
name: 2026-08-11-death-animation-cdda-sync
description: Death-animation audio sync is SOLVED EMPIRICALLY and must not be re-derived — a fixed ~480 ms delay is what lands the splat, not disc_wait_for_music, and four attempts to make it conditional each broke it. The fade that used to supply that delay is gone as of a7a1b4d; the delay is now a hold on the sequence's first frame. Read before touching play_death_animation.
metadata:
  type: project
---

## State

`v0.2` was one file (`saturn/src/animation.c`), cutscenes only. Since then, at Suinevere's
direction, `a7a1b4d` removed the fade from `play_death_animation` and replaced it with a
hold on the sequence's own first frame — the "only untried idea judged timing-neutral"
below, now implemented. **Unverified on hardware**; Suinevere runs the build.

The delay itself is unchanged and must stay that way: `DEATH_HOLD_MS` is
`FADE_HOLD_MS * FADECALC_SEGA_CD_STEPS`, the same 480 ms the fade spent, armed
unconditionally and measured from entry to `play_death_animation`. Everything below about
*why* that number exists still holds — only what is on screen during it changed.
`FADE_HOLD_MS` moved to `main.h` so the two cannot drift.

Earlier in the same session, unrelated and already shipped below `v0.1`: the CD-DA pregap
defect (every audio track carried 150 sectors of pre-INDEX-01 silence into the built disc,
so every cue started 2 s late). Fixed in `cae1eb1` in `tools/extract_disc.c` and
`saturn/host/disc_cue.c`, with `DiscCueTrack.pregap_sectors` added to `discfmt`. Assets in
`saturn/cd/music/` were regenerated and are correct; they are gitignored, so a fresh
checkout must re-run `tools/extract_disc.exe` against the rip.

## The one thing to understand before touching this

The delay that syncs a death animation to its music is **the fade**, not the wait.

Measured on hardware (trace instrumentation, since removed — see "Re-instrumenting"):

```
MUS loop 2        room track, looping
MUS one 30        death cue
DTH 2             first death segment
DTH wait 16 fade 467
DTH 3             second death segment
DTH wait 17 fade 467
MUS stop
```

`disc_wait_for_music()` returns in **16 ms**. It polls `GetTotalVolume()`, and the room's
own track is still sounding while the drive seeks to the death cue, so the level never
falls to zero and nothing distinguishes "track 30 started" from "track 2 still going".
The whole usable delay is `fade_out_finish()`'s 467 ms, spent blind.

Total is ~483 ms and it **works** — the splat lands. Treat that number as the fixed point.
It was arrived at by accident and is not derivable from anything currently observable.

## What the script actually says (read from ROOMS*.BIN, 2026-08-11)

A two-segment death is **two adjacent `0x21` opcodes**. Disassembled from the room files
rather than inferred:

```
1a 83     MUSIC one-shot track 30      the death sting; the only disc work in a death
0b 07     PALETTE 7
21 22     DEATH raw 0x22 -> index 2    segment A   (imm8 % 32 in decode.c)
21 63     DEATH raw 0x63 -> index 3    segment B
0c        DESTROY_TASKS
```

Exactly three multi-segment sites exist, and all three are this shape, all cueing track 30:
`ROOMS1.BIN` `0x0379e4` and `0x038edd` (segments 2,3) and `ROOMS3.BIN` `0x035962`
(segments 0,1). Every other death is a single `0x21`. Segment B is never preceded by a
music opcode; index 3 also appears alone elsewhere (`ROOMS3.BIN` `0x037864`), so the
indices are not inherently "first" and "second".

Consequences, and they are the whole answer to "can the mid-death fade be skipped":

- **The gap between the segments is one opcode fetch.** Nothing runs between them — no
  read, no cue, no seek. Segment B's fade masks no disc work whatever. It is pure delay.
- **Segment B's 480 ms is still load-bearing**, because the splat is in segment B and
  deleting the delay moved it 480 ms early (approach 2 below).
- **But the splat's absolute time depends only on the *sum* of the delays before it, not
  on where they sit.** `T0 + 480 + len(A) + 480 + N/15` is unchanged if the second 480 ms
  is moved in front of segment A. That is the escape route the four failed attempts did
  not have: they deleted the time, which is why they failed.
- **A lookahead is deterministic where the earlier gates were not.** `decode.c` can peek
  `get_byte(script_ptr + pc) == 0x21` after consuming the operand and know, with no race
  and no runtime signal, that another segment follows. Every failed approach gated on
  something observable at runtime (audio level, elapsed time, a cue flag); this is a static
  property of the script bytes.

The disassembler used is throwaway (operand widths derived from `decode.c`'s switch,
self-synchronising decode to tell code from data). It is not in the repo.

**Shipped in `b1855f7`, unverified on hardware.** `decode.c`'s `0x21` peeks the next opcode
and passes `chained` to `play_death_animation`; a chained first segment fades via
`fade_out_begin_spans(2)` — same eight steps, each held twice as long — and the second
segment does not fade or wait at all. `g_deathContinues` carries that across the two calls
and is cleared unless the sequence finished cleanly, so an abort cannot leave a later
death with no fade and no music wait.

## 960 ms, confirmed from both sides

> **STALE (2026-08-14).** Two things below no longer describe the tree.
> `DEATH_CHAINED_HOLD_MS` reads **200**, not 120 — 1600 ms over eight steps, not 960 —
> and has since `3b10479`, so the bracketing recorded here was against a value that was
> never committed. And `DEATH_LAST_FRAME_MIN_MS`, `hold_last_frame` and
> `death_button_down` are **deleted**: a terminal death now fades to black and sets
> `death_played` instead of holding its last frame for a button press. The floor existed
> because a button held from the death itself would dismiss the shot instantly, and a
> fade listens for no button. See `DEATH_CHAINED_HOLD_MS`'s banner in
> `saturn/src/animation.c` and the fix report under
> `.superpowers/sdd/2026-08-14-hota-saturn-subtitle-and-save-menus/`. Everything else in
> this file — the delay being unconditional, the four failed attempts to make it
> conditional, the fade in — still stands.

The front delay is **960 ms** (`DEATH_CHAINED_HOLD_MS` 120 × 8 steps) — the same total the
two separate fades used to add up to, and the number this file has protected throughout.
It has now been bracketed by eye rather than only defended:

- **1200 ms** — too long.
- **600 ms** — too large a gap before the splat. Tried on the reasoning that the
  open-ended hold on the last frame had removed the need for a head start (the cue no
  longer races the end of the scene, since the hold runs before the script reaches
  `DESTROY_TASKS` and the music stop). That reasoning is still sound for the *sting*; it
  did not survive contact with the *splat*. Rejected.

So the tuning range around 960 is narrow in both directions. Treat it as measured.

Knobs, all in `saturn/src/animation.c`:

> **STALE (2026-08-14).** Two of the three entries below no longer describe the tree.
> `DEATH_CHAINED_HOLD_MS` has only ever been **200** — `git log -S` finds one commit
> touching it, `3b10479`, and it reads 200 there — so the shipped hold is **1600 ms**, not
> 960, and the 120/960 figures below were never true of any shipped revision. That puts the
> real value above the 1200 ms this file records as already too long, which is unresolved
> and wants an eye on a two-segment death. `DEATH_LAST_FRAME_MIN_MS` is **deleted**: a
> terminal death now fades to black and opens the load screen instead of holding for a
> press, so a floor against a held button has nothing left to protect.

- `DEATH_CHAINED_HOLD_MS` (120, ×8 = 960 ms) — the single fade in front of a chained
  death. Also the delay. Bracketed above; do not wander from it without a reason.
- `DEATH_FADE_IN_HOLD_MS` (60, ×8 = 480 ms) — the fade **in**, deaths only. Runs across
  the sequence's opening frames rather than in front of them, so it costs no time and
  moves no frame. Free to tune.
- `DEATH_LAST_FRAME_MIN_MS` (400) — how long the final picture is held before a button
  press is listened for at all. Deaths are reached mid-struggle with a button already
  down, so the floor stops the press that caused the death dismissing the shot it made.
  Free to tune.

The fade in is the one place this port deliberately departs from the Sega CD, which sets
the palette back to full brightness in a single write and never walks the ladder upward.
`main.h`'s fade banner says so and now records the exception. Cutscenes are unchanged.

What to watch for on hardware, in order:

1. **The splat.** Should be unchanged; the sum of the delays ahead of it is identical. If
   it moved, the "only the sum matters" model is wrong and that is new information worth
   more than the cosmetic win — revert rather than tune.
2. **The first segment against its music.** It now starts ~480 ms later than it used to.
   This is the one real risk and it is not measurable from here: if segment A has an
   audible beat of its own, it will be late.
3. **The fade itself.** 960 ms is closer to the original's ~2 s than 480 was, but it was
   never seen at this length; `FADE_HOLD_MS` still scales both.

## Four approaches that broke it, and why

All four tried to skip the fade on the second death segment (it reads as a fade out
dropped into a running video). Each is recorded because each looked correct in advance.

1. **Gate on `disc_music_is_pending()`** (audio level) — races the seek; the outgoing
   track answers "already playing", so the first segment intermittently lost fade *and*
   wait.
2. **Gate on a `note_music_cue()` flag set by opcode `0x1a`** — the flag mechanism is
   sound (the trace confirms `MUS one 30` does precede `DTH 2`), but it removed the
   second segment's delay, and **the splat is in the second segment**. This is the
   strongest evidence available about where the splat lives.
3. **Defer the fade until the wait had waited ≥33 ms** — the wait is 16 ms, so the fade
   never armed at all and the delay went to zero. Threshold was longer than the thing it
   measured.
4. **Replace fades with a plain pause** — removed the delay entirely, same failure.

A fifth attempt, at the *detector* rather than the gate, also failed: waiting for
`CDC_STAT_FAD` to land inside the requested track's TOC range still returned in 16 ms,
because the CD block reports the play command's target address as soon as it accepts the
command, before audio flows. **There is currently no known signal that detects the
track switch.** Do not spend another cycle looking for one without new information.

The safe decomposition, if this is ever revisited: **never make the delay conditional.**
Vary only what is on screen during it. A wrong guess then costs a fade that should not
have played, never synchronisation.

## What is still cosmetically wrong (deliberately left)

- ~~A visible fade out mid-death, on the second segment.~~ Fixed in `a7a1b4d`. Both "fade"
  and "frozen outgoing scene" (`scene_hold`) had been tried and rejected as jarring; the
  third option — draw the sequence's **first frame** and hold *that* for the delay, then
  run at 15 fps — is what shipped. The splat at frame N still lands at `delay + N/15`, so
  sync cannot move. Hooks `post_render` rather than `copy_to_screen`, because
  `copy_to_screen` writes the frame and `post_render` is the call that presents it.
- Rejected on analysis: chopping ~500 ms off each audio file. It trades a fixed delay for
  a fixed head start against a seek measured at 167–500 ms, touches all 41 tracks
  including ones with no problem, and discards real audio.
- Rejected on analysis: halving the animation frame rate. The discrepancy is a fixed
  offset, not a drift; a rate change stretches everything to fix one moment.

## Cutscenes (`play_animation`) — done at v0.2

Symptom was "fade one frame, pause, drop to black, flash the last frame, then play". Two
causes, both fixed in `f9f1703`: the fade could not advance across the read (a GFS request
is uninterruptible and an aligned animation is only two of them), and brightness was
restored while the framebuffer still held the outgoing scene.

The fade now completes before the read; brightness returns in `copy_to_screen` on the
first frame with new pixels. `g_restorePending` is set only by `play_animation`, so the
death path never enters that branch.

**Do not "unify" the two paths.** They wait on different things — a seek yields every
frame, a read yields twice — and the comments at both call sites say so.

## Re-instrumenting

The trace was removed for `v0.2`. To rebuild it: `LOG()` is compiled out (`ENABLE_DEBUG`
is not defined in `saturn/makefile`), so use bare `printf`, which reaches SRL's debug text
layer because `saturn/src/system/stdio.h` shadows the SDK's do-nothing dummy. The removed
version added a `TRACE` macro in `debug.h`, call sites at opcodes `0x1a` and `0x21` in
`decode.c`, and timers around the wait/fade in `play_death_animation`. It is recoverable
from `4db24f6` on branch `fade-experiments-2` if that branch still exists.

## Loose ends

- Branches `fade-experiments` and `fade-experiments-2` hold the discarded work and are the
  only copy. Suinevere was offered deletion and has not answered.
- `saturn/tests/run_tests_fadecalc.exe` is untracked and not covered by `.gitignore`
  alongside the other `run_tests_*.exe`.
- `decode.c` subtracts 1 from the raw script operand before `disc_play_track`, so operand
  1 maps to engine index 0 and is refused. Flagged in `disc_srl.cxx`'s banner as
  unreconciled; the trace shows `MUS one 30` and `MUS loop 2` both playing, so it is not
  obviously live, but it has never been checked.
- The fade machinery (`fadecalc.c/.h`, `video_set_fade`, `fade_out_begin/finish`, the
  `disc_set_tick` hook) is now reached by `play_animation` only; the death path uses
  `FADECALC_SEGA_CD_STEPS` for sizing and `video_set_fade` not at all. `fadecalc` has host
  tests in `saturn/tests/test_fadecalc.c`.

## Process note

This session regressed a working build four times by changing behaviour Suinevere had
already confirmed, on theories about a script that had never been traced. The trace took
one commit and answered three questions that six speculative commits had not. When a
behaviour is confirmed working, treat it as a fixed point and instrument before changing
it — and change one variable per build, since bundling the step-count change with a gating
change made two rounds impossible to isolate.

## Suggested skills

- `superpowers:systematic-debugging` — before any further change here. Phase 1 is the step
  that was skipped repeatedly.
- `superpowers:verification-before-completion` — Suinevere runs the build and emulator;
  nothing in this area is verifiable from a tool call, so claims must be marked unverified
  until reported back.
- `superpowers:test-driven-development` — only the pure arithmetic is host-testable
  (`fadecalc`, `discfmt`, `cdtoc`, `cdda_classify`, `sfxconv`). Anything new that can be
  pushed into that shape should be.
- `superpowers:requesting-code-review` — before tagging another release from this area.

Related: [[hota-cdda-seek-delay-and-loop-restart]],
[[hota-original-hides-cdda-latency-behind-fades]], [[srl-toc-and-resume-are-broken]],
[[hota-saturn-disc-reads-are-five-times-too-slow]], [[user-runs-the-build-and-emulator]]
