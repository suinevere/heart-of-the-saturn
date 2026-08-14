---
name: 2026-08-14-subtitle-and-save-menus
description: Sub-title and save menus shipped on port/subtitle-and-save-menus and partly hardware-verified; the six requirements from the second emulator run are all implemented at ac41645 and none of them has run on hardware.
metadata:
  type: project
---

Sub-title menu, save/load slot list, pause menu, attract loop and the password-screen
suppression are implemented on branch **`port/subtitle-and-save-menus`**. Two squashes and
two rounds of hardware findings. Read the documents rather than re-deriving them:

- Spec: `docs/superpowers/specs/2026-08-14-hota-saturn-subtitle-and-save-menus-design.md`
- Plan: `docs/superpowers/plans/2026-08-14-hota-saturn-subtitle-and-save-menus.md`
- Execution ledger, every ruling and every deferred minor:
  `.superpowers/sdd/2026-08-14-hota-saturn-subtitle-and-save-menus/progress.md` (gitignored)
- Round-1 fix brief and report: same directory, `fix-brief-fades-and-death.md` and
  `fix-fades-report.md`

## Open requirements from the second emulator run

**STALE (2026-08-14): all six are implemented at `ac41645`. None has run on hardware.**
The list is kept because it is what that commit has to be checked against.

1. The main menu fades out going into the intro movie.
2. The intro movie fades in.
3. `START GAME` drops the player into the new game with a fade in.
4. Skipping at any point in the startup sequence fades out and back in to the title card.
5. **A death holds its last frame** and the Load Game card overlays it. The player is not
   dropped at the main menu straight after a death.
6. Only closing the Load Game box drops the player at the main menu.

The ambiguity in 5 was settled by asking: the player saw the **load screen over the
sub-title card**, so the gate's `MENU_GATE_LOAD` routing was already right and only the
presentation was wrong. `play_death_animation` no longer fades a terminal death out, the
gate opens `menu_art_begin(0)` for a death arrival so the card composites over the held
frame, and reaching `MENU_TITLE` promotes the menu to exclusive — which is 6.

The scope of 4 was settled the same way: the intro movie only. A button during the four
opening stills still cuts straight to the game-select menu.

See `## What ac41645 changed` below before touching any of it.

## What ac41645 changed

Four mechanisms, none of them verified anywhere but a `-fsyntax-only` pass:

- **A second fade layer.** The boot and menu screens are VDP1 sprites with their own CRAM
  banks, and `video_set_fade` reaches only the VDP2 palette the engine's bitmap draws
  through — it dims none of them. `boot_art_fade` and `menu_art_fade` are the sprite side,
  scaling each bank from a kept copy of its authored palette through the same
  `fadecalc_scale` at CRAM's own 5 bits. A transition that has both layers on screen has to
  call both, which is what `menu_fade_out` does. Each entry is scaled as a copy rather than
  rebuilt through `FromRGB555`, so the opacity bit survives; index 0 is transparent and an
  entry rebuilt opaque would paint every glyph cell's blank columns black mid-fade.
- **Cutscenes now ramp back up.** `copy_to_screen`'s single write of
  `FADECALC_LEVEL_NORMAL` became the death path's ramp, armed on the first frame with new
  pixels and pumped from the frames after it. That introduced a window a fade out can now
  start in, so `fade_pump` gained `g_fadeCeiling` and never writes a level brighter than
  what was on screen when it was armed — a cutscene skipped inside its first half second
  used to be brightened back toward full before being taken down.
- **`screen_arm_fade_in`**, the ramped form of `screen_arm_fade_restore`, pumped from
  `update_screen`. Every gate exit uses it. It declines to ramp a screen that is already at
  normal, which is the case a death arrival would otherwise black in order to lift.
- **`menu_run` gained `fade` and a one-way promotion to exclusive.** `exclusive` is now the
  mode the menu *opens* in; reaching `MENU_TITLE` takes the screen and blacks NBG0 under it.

The risk worth watching: the deferred ramp is pumped only by `update_screen`, so a room
whose opening does long non-drawing work will hold the fade in part-way up and then jump.

## What is verified and what is not

Hardware-verified: the cutscene fade-out. Everything else in this branch has been read and
reviewed but not seen running.

Still unverified and named in the spec's Risks table: whether **every single-segment death is
terminal**. The disassembly proves it only for the three two-segment deaths. If a scripted
death was meant to resume the story it now opens the load screen — visible immediately, one
line to revert in `decode.c`'s `0x21` case.

## The three findings that cost the most to reach

**A death never changes rooms, so the room-7 gate could never see one.** `load_room` has
three callers and one is script-reachable; the death script is `21 [21] 0c` and
`destroy_tasks` kills only a *range* of tasks, leaving a survivor to draw the password
screen in the same room. `decode.c`'s only script route to `next_script = 7` is the ending.
The hook is now the death opcode, keyed on `!chained`, which is a static property of the
script bytes. This is why the first attempt shipped a feature that did nothing.

**No game code restored screen brightness before this branch.** `fade_out_*` and
`g_restorePending` had no callers outside `animation.c`, because the animation player always
restored before returning. Ending cutscenes black moved that ownership, and every handoff
needs an arm: `screen.c`'s `update_screen` is the game's only draw choke point and the exact
analogue of `copy_to_screen`. The arm must fire on the frame with **new pixels**, never on a
frame count — a room load takes an unknown number of frames and restoring early re-shows the
stale framebuffer, which is the flash this work removed.

**`decode.c`'s mid-game pair hands to room 6, not the gate.** Missed by the fix brief. Without
an arm there the entire back half of the game runs behind a black screen. Found only because
a by-hand trace of every handoff path was a required deliverable; both reviewers then
re-derived it independently. If a future change adds a path that leaves the screen faded,
trace it the same way — no test in this repo can catch a missing arm.

## Traps and corrections worth carrying

- **`sgl.h` defines `pal` as a macro** (`COL_32K` → `(2 - 1)`). Any identifier named `pal` in
  a translation unit that includes `<srl.hpp>` fails to compile.
- **The build carries `-Wno-strict-aliasing` but not `-fno-strict-aliasing`**, so pointer
  punning is still acted on by the optimiser at `-O2`. The project's flags also carry `-W`
  but **not `-Wall`**; adding it explicitly to a syntax check found real undefined behaviour
  this branch would otherwise have shipped. Do it on every `-fsyntax-only` pass.
- **VDP1 texture ids start at 0**, so a `>= 0` "already loaded" guard over a zero-initialised
  array skips the entire first load. `g_glyph`/`g_panel` are explicitly `-1`.
- **`DEATH_CHAINED_HOLD_MS` has only ever been 200**, verified with `git log -S`; the shipped
  chained hold is **1600 ms**, not the 960 its comment claimed. That is above the 1200 ms
  `[[2026-08-11-death-animation-cdda-sync]]` records as already too long. Unresolved — watch
  a two-segment death's splat. The prose is corrected; the constant is untouched.
- `mem/2026-08-11-death-animation-cdda-sync.md` and
  `mem/2026-08-13-death-fade-shape-and-history-squash.md` carry `STALE (2026-08-14)` markers
  where they describe the deleted last-frame hold as current.

## Branch and push state

`port/subtitle-and-save-menus`, **7 ahead of `origin/main` and 16 behind**, with an identical
tree at the squash point — the divergence is entirely the two history rewrites. Publishing
needs a **force-push, which has not been authorised**. Pre-squash history is preserved at
`backup/pre-squash-menus` (`4da43ac`) and `backup/pre-squash-docs` (`82e11c0`).

Local `main` at `260c5df` is stale and tracks a branch that is gone; all five of its unique
commits exist in squashed form on this line. `origin/port/boot-sequence` still holds its own
20 unsquashed commits from the previous session — the same undecided force-push.

The host `alien` build has not been run this session by standing convention; nothing in this
branch compiles into it except the engine files, and the host arm of the death hook is
guarded.

Related: [[2026-08-14-backup-ram-saves]], [[2026-08-13-boot-sequence-build]],
[[2026-08-11-death-animation-cdda-sync]], [[2026-08-13-death-fade-shape-and-history-squash]].
