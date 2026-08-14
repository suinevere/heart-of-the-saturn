---
name: 2026-08-14-backup-ram-saves
description: Backup RAM saves shipped and hardware-verified at a8e70b9 on port/boot-sequence; read before starting the save/load and pause menu spec, which inherits three traps recorded here.
metadata:
  type: project
---

Backup RAM saves are done and **verified on hardware** — save, power cycle, reload,
and both game state and the CD-DA track return. Squashed to `a8e70b9` on
`port/boot-sequence`. The 20-commit execution history is preserved locally at
`backup/pre-squash-saves` and nowhere else.

**`origin/port/boot-sequence` still holds the unsquashed 20 commits.** Local and
remote have diverged; updating origin needs a force-push, which has not been done.
Decide that before any other push to this branch.

Design and plan, both committed — read these rather than re-deriving:

- `docs/superpowers/specs/2026-08-13-hota-saturn-backup-saves-design.md`
- `docs/superpowers/plans/2026-08-13-hota-saturn-backup-saves.md`

## This is spec one of two

> **STALE (2026-08-14).** The follow-on spec described below is written, implemented,
> reviewed and partly hardware-verified — see [[2026-08-14-subtitle-and-save-menus]]. The
> three traps this section names all held and are worth keeping; what is stale is only the
> claim that the work is unstarted. One prediction in it was wrong in an instructive way:
> the menus did **not** need Another-Saturn's screen-format workarounds, because drawing
> them as VDP1 sprites over NBG0 sidesteps the 320x200-versus-304x192 mismatch entirely.

The follow-on spec — the sub-title menu on `BOOTTITL.ART`, the slot list, the
confirm prompt and the pause screen — was scoped in the same conversation and is
**not written yet**. It lifts from `..\Another-Saturn\saturn\src\`:
`menu_state.{h,cxx}` (pure logic, host-testable, copies nearly verbatim),
`menu_art.h` / `menu_draw.h` / `menu_input.h`, and `menu.cxx` (engine-touching,
needs rewriting). Its `savedata.cxx` chapter-name table has no counterpart here:
HOTA has `current_room`, a byte with no name table anywhere in the engine, so the
header stores the id and the menu decides how a row renders it.

One substrate mismatch to plan around: Another-Saturn's menus composite into a
320x200 4bpp page, while this engine's screen is 304x192 Paletted256 on NBG0
(`video_srl.cxx`) and the boot art is VDP1 sprites drawn with NBG0 switched off
(`saturn_bootart.h`).

## Three traps the menu work will hit

**The Saturn makefile globs `src/**/*.c`.** A task that adds a caller without its
callee breaks the link for every task in between — this cost a resequencing
mid-execution when `savedata.c` called `sat_bup_*` four tasks before
`saturn_backup.cxx` defined it. Everything compiled; only the link failed.

**`SRL::Input::Digital::Button` mixes conventions in one enum.** `Right`, `Up`,
`Down`, `Left`, `A`, `B`, `C` are mixed case; `START` is uppercase
(`srl_input.hpp:710`). The mixed-case `Start` at lines 318 and 465 belongs to
other peripheral types. A grep for the identifier does not show which type owns
it, and getting this wrong cost a build.

**Deleting the debug chord can delete the proven save point.** `saturn_save_poll`
sits in `main.c` between `check_events()` and the VM task loop's first
`toggle_aux(0)`, which is the only place `quicksave`'s "no active thread"
precondition holds. Keep the call site and swap the chord for the menu hook; do
not remove the whole `#ifdef` block. Also do not carry the chord's edge-detection
shape forward — going from Start+B to Start+A+B without releasing fires a save.

## Worth knowing

`quicksave()`/`quickload()`/`quicksave_sprites()`/`quickload_sprites()` are
byte-for-byte unmodified and must stay that way; the payload is a contiguous
prefix of the host port's own `quicksave` file, so saves are portable both ways.

`savegame_read` deliberately does **not** promise an untouched payload on
failure — the RLE decoder streams, so a corrupt slot can partly fill the staging
buffer. The engine is protected by the caller checking the return code before
calling `quickload()`, not by the buffer being clean. See `savegame.h`'s banner.

Sixteen Minor findings were reviewed and consciously left. The ones that will
matter to the menu: `savedata_probe` does not require `entry.size >=
SAVE_HEADER_SIZE` before parsing, so a tiny file starting with `HOTA` yields
`SLOT_OK` with a garbage room and date; `savedata_probe` needs a caller-supplied
`SAVE_MAX_BYTES` scratch buffer and there is no accessor for `s_work` yet; and
`sat_bup_probe`, `sat_bup_delete`, `savedata_probe`, `savedata_pick_default_device`
and `savedata_date_split` still have no caller outside tests.

Also fixed along the way, unrelated to saves: the host build had failed at link
since `3b10479` because `saturn/src/Makefile`'s explicit `OBJS` list never gained
`fadecalc.o`. That list does not glob — new host-side files must be added by hand.

Related: [[hota-port-reference-repos]], [[srl-toc-and-resume-are-broken]],
[[hota-cdda-seek-delay-and-loop-restart]], [[2026-08-13-boot-sequence-build]].
