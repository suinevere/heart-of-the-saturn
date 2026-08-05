# Heart of The Alien → Sega Saturn — Input Design Spec

**Date:** 2026-08-05
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)

## Goal

Make the game playable on Saturn.

The boot-and-video sub-project left `check_events()` as an empty function in
`saturn/src/system/input_srl.cxx`, with `input.h`'s contract explicitly naming that as
the supported way to defer input. This sub-project gives it a body: read the pad on
port 0 and drive the seven key globals the engine already reads.

Done means: the four directions move Tausar, and A, B and C each do something distinct
in play.

This is also what unblocks everything else. Nothing past the intro has ever run —
rooms, gameplay, the 512 KB map bound, the delta-scratch relocation, and the CD-DA
loop-restart rule have all been reasoned about and never executed. None of that can be
tested until something can drive the game.

## Scope

**Pad mapping only.** The host backend `saturn/host/input_sdl.c` also handles quit,
quicksave/quickload, a speed throttle, a debug-layer toggle, sprite toggles, fullscreen
and input recording. None of those are in scope:

- There is no quit on a console.
- Quicksave needs a real backup-RAM implementation behind it; `saturn_filestub.c` is a
  stub, and that is its own sub-project.
- The throttle, sprite toggles, fullscreen and debug toggle are host development
  conveniences reached by keyboard.

## Two things that are already solved

**Peripheral refresh is free.** `SRL::Core::Synchronize()` (`srl_core.hpp:125`) calls
`SRL::Input::Management::RefreshPeripherals()`, and `platform_frame()` already calls
`Synchronize()`. The Saturn `check_events` is a pure read: no polling, no event queue,
no per-frame plumbing to add.

**The engine's key model is level, not edges.** The host sets `key_x = 1` on keydown
and `0` on keyup; nothing anywhere reads a press *transition*. `SRL::Input::Digital`'s
`IsHeld` maps onto that exactly, so `WasPressed` and `WasReleased` are not needed.

## Architecture

One file changes. No new files, no new seam, no engine change, and `input.h` itself is
untouched — the contract it already documents simply stops being the one in use.

```
engine game loop (main.c:594, animation.c:85)
  |
  |  check_events()          <- input.h, unchanged
  v
input_srl.cxx                reads port 0, assigns seven globals
  |
  v
SRL::Input::Digital::IsHeld  (state refreshed by Core::Synchronize)
```

## Components

### `check_events()` — the whole sub-project

1. Construct a `SRL::Input::Digital` for port 0.
2. If it is not connected, zero all seven globals and return.
3. Otherwise assign each global from `IsHeld`: `Up`, `Down`, `Left`, `Right` to the
   four directions, and `A`, `B`, `C` to `key_a`, `key_b`, `key_c`.

**Port 0 only.** Single-player game; port 0 is player 1. An unplugged pad, or one in
the wrong port, leaves every key at zero — which is exactly the stubbed behaviour that
has been running since the boot sub-project, so it degrades to something already known
good rather than to something new.

**Literal A/B/C.** The Sega CD original used a Genesis pad's A/B/C, and the Saturn
pad's A/B/C occupy the same bottom row, so muscle memory from the original transfers.
Any other assignment would be a guess about what each button does in play, and the
engine code does not clearly justify one — `update_keys` (`main.c:285`) shows `key_c`
sharing a flag bit with `key_up` and `key_a` setting the classic action bit, but that
is not enough to redesign a control scheme around. Revisit only if play proves it
uncomfortable.

### Three things the banner must say

Each would otherwise be a surprise to the next reader:

- **The pad is active-low.** `IsHeld` is `(data & button) == 0` — a set bit means *not*
  pressed. `IsHeld` hides that, but anyone reading `GetCurrentFrameState()->data`
  directly will get it exactly backwards.
- **The `Digital` object is a local, not a file-static.** Its constructor is trivial,
  but a file-static C++ object with a constructor runs at static-init time, before
  `SRL::Core::Initialize()`. A local costs nothing and cannot be ordered wrong.
- **Nothing here refreshes.** See below.

### Input freshness: one frame stale in the main loop, by decision

`animation.c`'s `post_render` runs `platform_frame()` and then `check_events()`, so the
animation loop reads pad state refreshed by the same sync — its banner already says so.

`run()`'s loop does the opposite: `check_events()` at `main.c:594`, `platform_frame()`
at `main.c:652`. So the main game loop reads state refreshed at the end of the
*previous* iteration — at most one frame, roughly 16 ms, old.

**This is accepted, not fixed.** The two alternatives are both worse:

- Calling `RefreshPeripherals()` inside `check_events` makes it fresh everywhere, but
  double-refreshes in the animation loop, collapsing current and previous state and
  silently breaking `WasPressed`/`WasReleased` for any future consumer. It works today
  only because nothing uses edges.
- Reordering `run()`'s loop edits engine code and perturbs the present/frame ordering
  that was established and verified on hardware in the boot sub-project, to buy one
  frame.

16 ms is imperceptible in a game this deliberate. The banner states the staleness so it
is not rediscovered as "the controls feel laggy".

## What stays at zero

- **`key_select`** is vestigial: declared in `input.h`, never written by the host
  backend, never read by the engine. Left alone rather than wired to a button that
  nothing would observe.
- **`key_reset_record`** drives host input recording, which has no Saturn counterpart.
- **`cls.quit`** is never set. A console has no quit, and `run()`'s
  `while (cls.quit == 0)` running forever is the correct behaviour.

## Testing

**No host unit test earns its place here, and that is a claim worth arguing with rather
than assuming.**

The CD-DA sub-project extracted two pure modules (`cdtoc`, `cdda_classify`) because
both held real logic — bit-level decoding and a three-way classification — and both
shipped wrong the first time, caught by their tests. Nothing comparable exists here:
`check_events` is one connected-check and seven assignments, with no arithmetic, no
state machine and no derived values.

A pure mapper taking the raw pad word could be host-tested, but it would mean
re-implementing `IsHeld`'s bit test in order to test it — testing SRL, not this port.
And the one error actually available here, wiring `Button::Left` to `key_right`, is
precisely the error a test written from the same understanding would reproduce rather
than catch.

Verification is the emulator, where it is conclusive in seconds.

## Build and test

No build change. `saturn/makefile` globs `find src/ -name '*.cxx'`, and
`input_srl.cxx` is already in the build.

1. `sh saturn/tests/run_tests.sh` — the four existing suites, unaffected, must still
   pass.
2. `make -C saturn/src` — the host build, unaffected (`input_sdl.c` untouched).
3. `cd saturn && ./compile.bat` — links, now defaulting to the music disc.
4. Suinevere on the emulator: the four directions move Tausar; A, B and C each do
   something distinct.

## Risks

**Wrong button-to-global wiring is the only real failure mode**, and it is
self-evident in seconds of play — press left, walk left. There is no silent-failure
path here of the kind that made the CD-DA work delicate.

**A disconnected pad reads as no input, not as garbage** — the explicit `IsConnected`
check makes that intentional rather than relying on what an unplugged port happens to
return.

**Everything past the intro is unknown territory.** This sub-project makes the game
drivable, so it is also the first time rooms, gameplay, the 512 KB map bound and the
delta-scratch relocation get exercised at all. Failures found there are that code's,
not this seam's, but they will surface during this sub-project's verification and
should be recorded rather than absorbed into it.

## Out of scope

- Backup RAM, quicksave/quickload, and the `saturn_filestub.c` stub.
- Any debug-layer or throttle toggle.
- The six-button pad's X/Y/Z row, and analog pads.
- Multi-port or hot-swap handling.
- Sound effects (`play_sample` is still a no-op) and the CD-DA per-frame tick.

## Acceptance

1. `sh saturn/tests/run_tests.sh` still passes.
2. `make -C saturn/src` still builds.
3. `./compile.bat` links and produces a disc.
4. On the emulator: the four directions move Tausar, and A, B and C each do something
   distinct in play — confirmed by Suinevere.
5. `check_events` reads port 0 only, uses `IsHeld` only, and calls no refresh.
