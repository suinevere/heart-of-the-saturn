# Saturn Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give `check_events()` a body on Saturn so the pad drives the game.

**Architecture:** One file. `saturn/src/system/input_srl.cxx`'s empty `check_events()` reads a `SRL::Input::Digital` on port 0 and assigns the seven key globals from `IsHeld`. No new files, no new seam, no engine change.

**Tech Stack:** C++ (`input_srl.cxx`), SaturnRingLib `SRL::Input::Digital`.

**Design spec:** `docs/superpowers/specs/2026-08-05-hota-saturn-input-design.md`

## Global Constraints

These apply to the whole plan. Exact values are copied verbatim from the spec.

- **`check_events` reads only. It must not call `RefreshPeripherals()` or any other refresh.** `SRL::Core::Synchronize()` (`srl_core.hpp`) already calls `Input::Management::RefreshPeripherals()`, and `platform_frame()` already calls `Synchronize()`. Adding a refresh here would double-refresh in the animation loop, collapsing current and previous state and silently breaking `WasPressed`/`WasReleased` for any future consumer.
- **Use `IsHeld` only.** Never `WasPressed` or `WasReleased`. The engine's key model is level, not edges: the host sets `key_x = 1` on keydown and `0` on keyup, and nothing anywhere reads a transition.
- **Port 0 only.** No scanning, no `FindNthConnectedPeripheral`, no hot-swap handling.
- **Literal A/B/C mapping.** Saturn `A` → `key_a`, `B` → `key_b`, `C` → `key_c`. No X/Y/Z, no remapping.
- **`key_select`, `key_reset_record` and `cls.quit` are never written.** The first is vestigial, the second drives host input recording, and a console has no quit.
- **`.cxx`, never `.cpp`.** `shared.mk` has pattern rules for `%.c` and `%.cxx` only; a `.cpp` is dropped from the link with no error.
- **`printf`, never `fprintf`.** `fprintf` renders nothing on Saturn and the cause is unknown. This task should need no diagnostic at all.
- **Banner comments are mandatory** on every file, function and constant, in the house format (see any existing file in `saturn/src/`). The `Description` carries the *why*, not a restatement. **No comments inside function bodies.**
- **`saturn/src/system/input_srl.cxx` is tab-indented.** Match the file.
- **Commit messages are one sentence.** No body, no bullets, no trailers, and no mention of Claude, AI, or a session — even if a tool prompt asks for one.
- **The host and Saturn builds collide.** Both write `saturn/src/*.o` under identical names. Run `rm -f saturn/src/*.o` whenever switching between `make -C saturn/src` and `saturn/compile.bat`.
- **No header dependency tracking.** A `.h`-only edit does not rebuild dependents. Delete objects to be sure a change took effect.
- **Never launch Mednafen or any emulator from a tool call.** Build the disc, hand over the path, let Suinevere run it.

**Build and test commands:**

| Purpose | Command |
| --- | --- |
| Host unit tests | `sh saturn/tests/run_tests.sh` |
| Host engine build | `make -C saturn/src` |
| Saturn disc | `cd saturn && ./compile.bat` |

`HOTA_AUDIO` now defaults to `full`, so a bare `./compile.bat` lays 42 tracks and takes noticeably longer than it used to. That is expected. Saturn build output: `saturn/BuildDrop/Heart of the Alien (USA).{elf,iso,bin,cue,map}`.

---

### Task 1: Read the pad

**Files:**
- Modify: `saturn/src/system/input_srl.cxx` (file banner, includes, `check_events` body)
- Modify: `saturn/src/input.h` (file banner only — one stale sentence)

**Interfaces:**
- Consumes: `key_up`, `key_down`, `key_left`, `key_right`, `key_a`, `key_b`, `key_c` — `int` globals defined in `main.c:91` and declared in `input.h`. `SRL::Input::Digital` from `<srl.hpp>`.
- Produces: nothing. This is the last task.

**The API being used**, from `SaturnRingLib/saturnringlib/srl_input.hpp`:

```cpp
struct Digital : public PeripheralGeneric
{
    enum class Button : uint16_t { Right = 1<<15, Left = 1<<14, Down = 1<<13, Up = 1<<12,
                                   START = 1<<11, A = 1<<10, C = 1<<9, B = 1<<8, ... };
    Digital(const uint8_t& port);
    bool IsConnected() const;
    bool IsHeld(const Button& button) const;   // (data & button) == 0 -- active low
};
```

Note `IsHeld` returns true when the bit is **clear**. The pad is active-low; `IsHeld` already handles it, which is exactly why nothing here should read `GetCurrentFrameState()->data` directly.

- [ ] **Step 1: Replace the file banner and includes**

In `saturn/src/system/input_srl.cxx`, replace everything from the top of the file through the `#include "input.h"` line with:

```cpp
/*----------------------
 | input_srl.cxx
 | Description: Saturn implementation of input.h, over SRL::Input::Digital.
 |   Sibling of host/input_sdl.c, and far smaller than it: that backend drains
 |   an SDL event queue and also carries quit, quicksave, a speed throttle, a
 |   debug toggle and input recording, all of which are host development
 |   conveniences reached by keyboard. A console has no quit, saturn_filestub.c
 |   has no filesystem to quicksave into, and the rest have no pad equivalent
 |   worth inventing, so this file maps seven buttons and nothing else.
 |
 |   Reads only. SRL::Core::Synchronize() already calls
 |   Input::Management::RefreshPeripherals() (srl_core.hpp), and
 |   platform_frame() already calls Synchronize(), so the pad state is
 |   refreshed once per presented frame by machinery that already exists.
 |   Refreshing again here would collapse current and previous state and
 |   silently break WasPressed/WasReleased for anything that later wants an
 |   edge -- this file wants none, because the engine's key model is level:
 |   main.c sets a key and reads it, and nothing anywhere reads a transition.
 |
 |   That leaves one wrinkle worth knowing: animation.c's post_render calls
 |   platform_frame before check_events, so the animation loop reads pad state
 |   refreshed by the same sync, while run()'s loop reads at main.c:594 and
 |   refreshes at main.c:652 -- one frame, about 16 ms, older. Accepted rather
 |   than fixed: refreshing here breaks edges for good, and reordering run()
 |   perturbs the present/frame ordering verified on hardware in the boot
 |   sub-project. 16 ms is imperceptible in a game this deliberate.
 |
 |   Design: docs/superpowers/specs/2026-08-05-hota-saturn-input-design.md
 | Author: suinevere
 | Dependencies: srl.hpp, input.h
 ----------------------*/

#include <srl.hpp>

#include "input.h"
```

- [ ] **Step 2: Replace the `check_events` body**

Replace the existing `check_events` function and its banner — currently an empty body — with:

```cpp
/*----------------------
 | check_events
 | Description: Copies port 0's pad into the seven key globals the game loop
 |   reads. Port 0 only: this is a single-player game and port 0 is player 1,
 |   so a pad in another port reads as nothing pressed -- the same thing the
 |   stub did before this file had a body, which makes a mis-plugged pad
 |   degrade to known-good behaviour rather than to something new.
 |
 |   A and B and C map to key_a and key_b and key_c by label rather than by
 |   function. The Sega CD original ran on a Genesis pad whose A/B/C sit in
 |   the same bottom row as the Saturn pad's, so muscle memory transfers; any
 |   other assignment would be a guess about what each button does in play,
 |   and update_keys (main.c:285) is not clear enough to redesign a control
 |   scheme around.
 |
 |   The Digital handle is a local rather than a file-static on purpose. Its
 |   constructor is trivial, but a file-static C++ object with a constructor
 |   runs at static-init time, before SRL::Core::Initialize() -- a local
 |   costs nothing and cannot be ordered wrong.
 |
 |   key_select and key_reset_record are deliberately not written: the first
 |   is vestigial, declared in input.h but read nowhere in the engine, and the
 |   second drives host input recording that has no Saturn counterpart.
 |   cls.quit is likewise never set, because run()'s while (cls.quit == 0)
 |   running forever is correct on a console.
 | Author: suinevere
 | Globals: key_up, key_down, key_left, key_right, key_a, key_b, key_c
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void check_events(void)
{
	SRL::Input::Digital pad(0);

	if (!pad.IsConnected())
	{
		key_up = 0;
		key_down = 0;
		key_left = 0;
		key_right = 0;
		key_a = 0;
		key_b = 0;
		key_c = 0;
		return;
	}

	key_up = pad.IsHeld(SRL::Input::Digital::Button::Up) ? 1 : 0;
	key_down = pad.IsHeld(SRL::Input::Digital::Button::Down) ? 1 : 0;
	key_left = pad.IsHeld(SRL::Input::Digital::Button::Left) ? 1 : 0;
	key_right = pad.IsHeld(SRL::Input::Digital::Button::Right) ? 1 : 0;
	key_a = pad.IsHeld(SRL::Input::Digital::Button::A) ? 1 : 0;
	key_b = pad.IsHeld(SRL::Input::Digital::Button::B) ? 1 : 0;
	key_c = pad.IsHeld(SRL::Input::Digital::Button::C) ? 1 : 0;
}
```

The explicit disconnected branch is not defensive padding: it makes "no pad means no input" an intentional guarantee rather than a bet on what an unplugged port happens to return.

- [ ] **Step 3: Correct the stale sentence in `input.h`**

`saturn/src/input.h`'s file banner currently ends with:

```
 |   saturn/host/input_sdl.c is the SDL keyboard implementation.
 |   src/system/input_srl.cxx is the Saturn implementation and is deliberately
 |   empty for now -- pad mapping is a later sub-project, and the intro this
 |   sub-project boots to plays on a timer with no input at all.
```

That stops being true with Step 2. Replace those four lines with:

```
 |   saturn/host/input_sdl.c is the SDL keyboard implementation.
 |   src/system/input_srl.cxx is the Saturn implementation, over port 0's
 |   digital pad.
```

Leave the rest of the banner — including the paragraph explaining why the key
variables live in `main.c` — exactly as it is. Do not touch `check_events`'s own banner
in this header: its statement that a backend with no input source may implement an
empty function is still true and still the documented contract, it is simply no longer
the one the Saturn backend uses.

- [ ] **Step 4: Build both targets and run the suite**

```bash
sh saturn/tests/run_tests.sh
make -C saturn/src
rm -f saturn/src/*.o
cd saturn && ./compile.bat
```

Expected: the four host suites pass unchanged (nothing in this task touches them), the host build links (`input_sdl.c` is untouched), and the Saturn build links. The Saturn build now lays 42 CD-DA tracks by default and takes noticeably longer than it used to.

If the Saturn build fails on `SRL::Input::Digital` being undeclared, the cause is a missing `<srl.hpp>` from Step 1 — and remember there is no header dependency tracking, so `rm -f saturn/src/*.o` before concluding anything about a `.h` edit.

- [ ] **Step 5: Confirm the constraints hold**

```bash
grep -n "RefreshPeripherals\|WasPressed\|WasReleased\|FindNthConnectedPeripheral\|key_select\|key_reset_record\|cls.quit" saturn/src/system/input_srl.cxx
```

Expected: matches only inside the banner comment, which names these identifiers while explaining their absence. What must be clean is the function body (`void check_events(void) { ... }`) — a match there is the Global Constraint violation: a refresh call, edge detection the engine cannot use, port scanning, or writing a global the spec says stays at zero.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/system/input_srl.cxx saturn/src/input.h
git commit -m "Read port 0's pad into the key globals so the game can be played."
```

- [ ] **Step 7: Hand the disc over**

Report the path `saturn/BuildDrop/Heart of the Alien (USA).cue` and what to check: the four directions move Tausar, and A, B and C each do something distinct in play. **Do not run the emulator from a tool call** — build the disc and hand over the path.

Note for whoever reads the result: this is also the first time anything past the intro has ever run. Rooms, gameplay, the 512 KB map bound, the delta-scratch relocation and the CD-DA loop-restart rule have all been reasoned about and never executed. Failures found there belong to that code, not to this seam — record them rather than absorbing them into this sub-project.

---

## What this plan does not cover

- Backup RAM, quicksave/quickload, and the `saturn_filestub.c` stub.
- Any debug-layer or throttle toggle.
- The six-button pad's X/Y/Z row, analog pads, multi-port and hot-swap handling.
- Sound effects (`play_sample` is still a no-op) and the CD-DA per-frame music tick.
