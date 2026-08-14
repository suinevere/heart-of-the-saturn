# Heart of The Alien → Sega Saturn — Sub-title and Save Menus Design Spec

**Date:** 2026-08-14
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)

## Goal

Give the port a front end for its own game, and a way in and out of a save without a
password.

Today, selecting *HEART OF THE ALIEN* from the boot menu drops into `run()`, which plays
the four-file opening cinematic and then loads room 7 — the Sega CD's password entry
screen. Backup RAM saves exist and are hardware-verified, but the only way to reach them
is a development chord in `main.c`.

Done means: selecting *HEART OF THE ALIEN* plays the opening cinematic; skipping it or
letting it finish lands on a sub-title menu over the title card offering *START GAME* and
*LOAD GAME*; the player can save and load from a pause menu during play; the password
screen is never shown, and wherever the game would have asked for it, the load screen
appears instead.

## Scope

**The menu layer, its two hooks into `main.c`'s `run()`, and one flag set in `decode.c`.**
`quicksave`, `quickload`, `quicksave_sprites` and `quickload_sprites` stay byte-for-byte
unmodified, as [the saves spec][saves] requires, and nothing in `video_srl.cxx`,
`sound_srl.cxx` or the renderer changes. `decode.c`'s only change is one assignment on the
ending path, argued under "Gate modes" below.

The boot sequence — four stills and the *OUT OF THIS WORLD* / *HEART OF THE ALIEN* select
— is untouched and keeps running before any of this.

Explicitly not in scope, each argued in [Out of scope](#out-of-scope): removing
`ROOMS7.BIN` from the disc, a chapter-name table for `current_room`, an options screen,
and any change to the host (SDL) build beyond keeping it linking and its tests passing.

[saves]: 2026-08-13-hota-saturn-backup-saves-design.md

## What the engine already does, measured

Every claim in this section is read out of the tree, and two of them removed work the
first draft of this design assumed.

### Room 7 is the password screen, and `run()` is the only door to it

`run()` (`main.c:775-798`) opens with:

```c
if (next_script == 0)
{
        play_intro();          /* INTRO1..4.BIN, tracks 31..34 */
        next_script = 7;
}
...
if (next_script != 0)
{
        current_room = next_script;
        reset_sprite_list();
        init_tasks();
        load_room(current_room);
        next_script = 0;
}
```

`decode.c:1853-1862` confirms what room 7 is: after the four ending animations it sets
`next_script = 7` under the comment *"return to password selection"*. No other file
assigns `next_script`, and `load_room` is reached from exactly two places — this block and
`sprite_test()`.

**One check at the top of that loop therefore suppresses the password screen entirely**,
whatever routes to it. `ROOMS7.BIN` stays on the disc and in `disc_manifest.h`; it simply
stops being reachable.

### The opening cinematic is already fully skippable

`play_intro()` is `play_anm(anm_files, 4, 0)` (`main.c:719-722`). With `skippable == 0`,
`play_anm` breaks out of the **whole** four-file sequence the moment `play_animation`
reports a keypress (`main.c:697-701`). So "skipping it drops the player at the sub-title
menu" needs no new code — it is what the existing return path does, once the gate is in
place behind it.

### `quickload` loads its own room

`quickload()` (`main.c:355-407`) reads `current_room` and calls `load_room` and
`load_room_screen` itself, then restores 256 main variables, 64 aux banks, the task
program counters and the sprite list.

**Consequence for the gate:** after a successful load the existing `if (next_script != 0)`
block must be *skipped*, not run. Running it would call `reset_sprite_list()` and
`init_tasks()` over freshly restored state and load the room a second time. The gate
returns 0 for this case and a room number for every other.

### The death route into room 7 is unproven

`decode.c`'s `0x19` handler — the `load screen` opcode, `decode.c:1819-1868` — computes
`next_script = (imm16 / 10) % 10 + 1` for `imm16` in 17000..17100, and then **hijacks**
the computed 7 into the mid-game animations (`MAKE2MB.BIN`, `MID2.BIN`) with
`next_script = 6`. So no room script can reach room 7 through that opcode.

That means the observed "password screen after death" is either routed some other way, or
is something drawn inside the room script rather than room 7. The gate is written to be
**trigger-agnostic** — it fires on any arrival at room 7 and cannot care what caused it —
so it costs nothing if deaths turn out not to route there. Confirming which it is is the
first item on the emulator run; if deaths do not pass through room 7, a second hook after
`play_death_animation` is a follow-up, not a change to this design.

### NBG0, VDP1 and the priority that makes the pause menu cheap

`saturn_bootart.h`'s banner records the ordering fact this design leans on: VDP1 sprites
draw **in front of** NBG0, which is exactly why `boot_art_load()` has to call
`NBG0::ScrollDisable()`.

So a pause menu drawn as VDP1 sprites sits over the game's last presented frame with no
copy, no freeze buffer, and no luminance remap — and, decisively, **without touching any
engine state**, so resuming is provably a no-op. Another-Saturn needed `menuFreezeRemap`
only because its menus composite into the same page the game draws into. This port does
not have that problem and must not inherit that code.

### Two SRL facts that settled two open questions

- **`Scene2D::DrawSprite` takes a `SRL::CRAM::Palette*` override per call**
  (`srl_scene2d.hpp:552,701`). The dim and selected text ramps are therefore two CRAM
  banks over **one** font texture set, not two copies of the glyphs.
- **`SRL_MAX_TEXTURES` is 100** (`saturn/makefile:20`), and boot art holds 7 of them
  permanently. A 64-glyph font plus three panels needs 67 more, for 74 in total. That
  fits, but with a margin too thin to add a screen to later, so it is raised to **128**.

`Scene2D::DrawSprite` also accepts a runtime `scale`, which would let one small texture
serve every panel size. It is not used: building a `Vector2D` scale at runtime means
constructing `Fxp` from non-constant values, and `Fxp`'s floating-point constructor is
`consteval` — the trap that already cost this port a build and forced `bootArtSprite` to
take `int16_t`. Baked panels have no such failure mode.

## Architecture

| File | Language | Responsibility |
|---|---|---|
| `saturn/src/menus/menu_state.{h,c}` | C | Screen transitions for the sub-title, pause, slot-list and confirm screens. No drawing, no backup RAM, no engine references |
| `saturn/src/menus/menu_layout.{h,c}` | C | `MenuState` + `SlotInfo[]` → an ordered list of draw items, plus the slot-row string builder |
| `saturn/src/menus/menu_clock.{h,c}` | C | Sub-title screen timing: CD-DA volume ramp, the 40 s music cycle, the 15 s attract trigger |
| `saturn/src/menus/menu.{h,c}` | C | The driver. Edge detection, `menu_gate()`, `menu_pause_poll()`, and turning actions into `saturn_saveslot_*`, `disc_*` and `menu_art_*` calls |
| `saturn/src/system/saturn_menuart.{h,cxx}` | C++ | The SRL seam: loads font and panel `.ART`, draws an item list as VDP1 sprites, the lightning palette flash, NBG0 enable/disable |
| `tools/mkmenuart.py` | Python | Builds `MENUFONT.ART` and three panel `.ART` files, palettes cut from `HEART OF THE ALIEN003.tga` |
| `saturn/src/main.c` | C | Two call sites, one `#include`, and the definition of `ending_played` |
| `saturn/src/decode.c` | C | One `extern` and one assignment, on the ending path only |

The split is the same one `bootmenu.c` / `saturn_bootart.cxx` already draws, for the same
reason: everything above `menu.c` is arithmetic over an elapsed millisecond count, an edge
mask and a `SlotInfo` array, so `saturn/tests/run_tests.sh` compiles it with host gcc and
tests it. Nothing in `menus/` includes `srl.hpp`, `sega_bup.h` or any engine header beyond
`savedata.h`.

`menus/` is a new subdirectory. `saturn/makefile:77-78` globs `find src/ -name '*.c'`
recursively, so it is picked up with no makefile edit. `saturn/src/Makefile`'s host `OBJS`
list does **not** glob and must be edited by hand — that omission is what left the host
build broken from `3b10479` until the `fadecalc.o` fix.

## Components

### `menu_state.{h,c}` — the pure state machine

Ported near-verbatim from `..\Another-Saturn\saturn\src\menu_state.cxx`, converted to C
and retargeted at this repo's `savedata.h`. Its `MenuScreen`, `MenuAction`, `MenuInput`
and `MenuState` carry over unchanged in meaning; `MENU_TITLE` is this port's sub-title
screen.

Transitions, all inherited:

- **Sub-title** — two rows. Up or down flips the cursor; confirm on row 0 returns
  `MENU_ACT_START_GAME`, on row 1 opens the slot list in load mode with
  `returnScreen = MENU_TITLE`.
- **Pause** — four rows: resume, save game, load game, return to title. Cancel *and* the
  pause button both resume from any cursor position. Rows 1 and 2 open the slot list with
  `returnScreen = MENU_PAUSE` and `saving` set accordingly; row 3 raises the confirm
  prompt with `pending = MENU_ACT_RETURN_TO_TITLE`.
- **Slots** — up/down wrap over `SAVE_NUM_SLOTS` (3); left/right toggle device but only
  when `cartPresent`, returning `MENU_ACT_RESCAN_SLOTS`; cancel returns to
  `returnScreen`; confirm saves an empty slot straight away, raises the overwrite prompt
  on an occupied one, and loads only a `SLOT_OK` slot.
- **Confirm** — `confirmYes` starts false every time the screen is entered, so a stray
  confirm cannot destroy a save. Left/right flip it; cancel and a "no" back out to the
  screen that asked.

One addition this port needs: cancel out of the slot list when `returnScreen` is
`MENU_TITLE` must land on the sub-title menu even when the slot list was opened by the
gate after a death. The gate sets `returnScreen = MENU_TITLE` for exactly that reason, so
no new state is required — but it is a behaviour the tests must pin, because it is the
only thing standing between a player with no saves and a reset button.

`returnScreen` stays private by convention, as its banner says.

### `menu_layout.{h,c}` — the pure drawing plan

```c
typedef enum { MENU_ITEM_PANEL, MENU_ITEM_GLYPH } MenuItemKind;

typedef struct {
        unsigned char kind;   /* MenuItemKind */
        unsigned char id;     /* panel index, or the glyph's ASCII code */
        signed short  x, y;   /* pixels, top-left, in the 320x224 frame */
        unsigned char ramp;   /* MENU_RAMP_DIM or MENU_RAMP_SEL */
} MenuItem;

int menu_layout_build(const MenuState *st, const char *status,
                      MenuItem *out, int cap);
```

Returns the item count, or `cap` clamped — never overruns. Spaces emit no item, which is
what holds the command-list cost down. This is the module the host tests aim at: asserting
that the slot cursor row renders in the selected ramp, that an empty slot renders
`- EMPTY -`, that a damaged one renders `- DAMAGED -`, and that a 28-character row does
not run past the panel is worth far more than asserting nibble packing.

The slot-row builder is hand-rolled string appending, not `snprintf`. Another-Saturn's
`menu.cxx:160` names the reason: `sprintf` pulls stdio into a translation unit that must
not have it. A row reads:

```
SLOT 1  ROOM 12  08/14 02:31
SLOT 2  - EMPTY -
SLOT 3  - DAMAGED -
```

`ROOM nn` rather than a chapter name: `current_room` is a byte and this engine carries no
name table for it anywhere. See [Out of scope](#out-of-scope).

Screen geometry is Another-Saturn's, unchanged, so the menus are recognisably the same
ones:

| Screen | Panel | Rows |
|---|---|---|
| Pause | 168x96 at (80,48) | 4, 16 px pitch from y=60 |
| Slots | 272x168 at (24,16) | 3, 16 px pitch from y=72; status at y=136; `A SELECT   B BACK` at y=160 |
| Confirm | 240x72 at (40,64) | prompt at y=76..92, `YES` / `NO` at y=116 |

The sub-title screen has no panel: `START GAME` at **(90, 152)** and `LOAD GAME` at
(90, 168), straight over the title card. Those coordinates are the requested "180 left,
320 down" halved out of the 640x480 grab — `mkbootart.py` already establishes that the
grabs are exact 2x pixel-doubles of 320x240 — less the 8-row overscan `ACTIVE_Y` strips.
Both are named constants so a nudge after the first emulator run is a one-line change.

### `menu_clock.{h,c}` — the sub-title screen's timing

Deliberately the same shape as `bootmenu.c`: unsigned differences over a millisecond
count, so it is correct across the counter's wrap, and it returns decisions rather than
performing them.

```c
typedef struct {
        unsigned char music_volume;   /* 0..MENU_VOLUME_MAX */
        int           music_restart;
        int           launch_attract;
} menu_clock_frame;

void menu_clock_enter(menu_clock_state *st, uint32_t now_ms);
void menu_clock_step(menu_clock_state *st, uint32_t now_ms, int had_input,
                     menu_clock_frame *out);
```

Constants, each named and each with the reasoning in its banner:

| Constant | Value | Why |
|---|---|---|
| `MENU_MUSIC_INDEX` | 2 | Engine music index; `discfmt_cue_track_for_music` maps it to cue track 3. Same track the boot menu plays, and `test_bootmenu.c` already pins the mapping |
| `MENU_MUSIC_CYCLE_MS` | 40000 | Fade completes at 40 s, then the track restarts from zero |
| `MENU_FADE_MS` | 1000 | `SND_SetCdDaLev` takes 0..7, so a fade is an eight-step staircase; a longer one adds time to notice each step, not resolution |
| `MENU_IDLE_MS` | 15000 | No input on the sub-title screen for this long launches the attract cinematic |
| `MENU_VOLUME_MAX` | 7 | Full CD-DA volume |

Two behaviours worth naming because they are decisions, not consequences:

**The idle timer runs only on the sub-title screen itself.** It does not run inside the
slot list or a confirm prompt. A player reading three slot rows is not idle, and tearing
the screen down under them to play a cinematic would be a bug that looks like a feature.

**The 40 s cycle is only reachable while the player is interacting.** Any input resets the
idle timer, and 15 s of none launches the attract instead. That is coherent rather than
contradictory: the cycle exists so that a player who *is* pressing things never hears the
track run into its second minute, and the attract covers everyone else.

Entering the sub-title screen — at boot, after the attract cinematic, or from *Return to
Title* — restarts the track from zero with a fade in. `menu_clock_enter` is the single
place that resets both timers, which is the fix for the boot menu's known gap where the
40 s cap has no reset except the attract replay.

### `menu.{h,c}` — the driver

Two entry points, both called from `run()` and both outside the task loop.

```c
int  menu_gate(void);        /* room to load, or 0 if state was restored */
void menu_pause_poll(void);  /* opens the pause menu when Start is pressed */
```

`menu_gate()` holds one static, the gate mode:

| Mode | Set by | Opens on |
|---|---|---|
| `GATE_SUBTITLE` | power-on; *Return to Title*; `ending_played` | The sub-title menu |
| `GATE_LOAD` | any successful start or load | The slot list in load mode, `returnScreen = MENU_TITLE` |

`ending_played` is consumed rather than merely read: the gate clears it as it acts on it, so
the arrival *after* the ending — a death in the new game the player then starts — goes back
to the load screen like any other.

So the post-intro arrival gets the sub-title menu, every later arrival gets the load
screen, and cancelling out of that load screen lands on the sub-title menu rather than
nowhere.

**The arrival after the ending credits gets the sub-title menu.** Finishing the game is not
a failure, and dropping a player who has just watched the credits onto a list of saves to
reload reads as one. `decode.c:1853-1862` is the one place in the program that knows the
ending just played — it runs `END1`..`END4.BIN` and then sets `next_script = 7` under the
comment *"return to password selection"* — so it sets `ending_played` in the same breath,
and the gate consumes that flag to select `GATE_SUBTITLE` for that one arrival.

This is the only engine behaviour change in the feature and it is deliberately the
smallest one available: a plain `int` defined beside `next_script` in `main.c`, assigned
unconditionally in `decode.c`, read only on Saturn. No `#ifdef` in `decode.c`, no Saturn
header included there, and the host build compiles the assignment harmlessly because
nothing reads it. A hook that called into `menus/` instead would have needed both.

Both entry points run their own inner loop of `check_events()` → edge detection →
`menu_state_step` → `menu_layout_build` → `menu_art_draw` → `menu_art_present`. Neither
ever calls the VM. `cls.quit` breaks out of both.

Input mapping, taking the enum trap in [the saves handoff][saves-mem] seriously —
`SRL::Input::Digital::Button` spells `START` uppercase while its neighbours are mixed
case, and getting it wrong costs a build:

| Menu signal | Pad |
|---|---|
| up / down / left / right | D-pad |
| confirm | A |
| cancel | B |
| pause | Start |

Edge detection is a plain `current & ~previous` over a held mask, exactly as
`boot_sequence()` does it. It is **not** the debug chord's shape: that one fires a save
when the player goes from Start+B to Start+A+B without releasing, and carrying that
forward would carry the bug forward.

[saves-mem]: ../../../mem/2026-08-14-backup-ram-saves.md

### `saturn_menuart.{h,cxx}` — the SRL seam

```c
int  menu_art_load(void);                   /* 1 on success, 0 on any failure */
void menu_art_begin(int exclusive);         /* exclusive: hide NBG0, draw title card */
void menu_art_draw(const MenuItem *items, int count);
void menu_art_present(void);
void menu_art_end(void);                    /* restore NBG0 */
```

`exclusive` is the whole difference between the two contexts. The sub-title menu and the
gate's load screen pass 1: NBG0 off, `BOOTTITL.ART` drawn as the backdrop, lightning
running. The pause menu passes 0: NBG0 stays on showing the game's last frame, no
backdrop sprite, no lightning, and the panel and text draw over it.

The backdrop reuses `saturn_bootart.cxx`'s already-resident `BOOTTITL.ART` texture rather
than loading a second copy. That means one small addition to `saturn_bootart.h` — an
accessor returning the title texture id — which is preferable to 35 KB of duplicate sprite
VRAM (320x224 at 4bpp) in a bump allocator that cannot free.

Sort values sit in front of the boot art's: `MENU_ART_Z_BACK` 480 for the backdrop, 470
for panels, 460 for glyphs. `slSetSprite` sorts far to near, so smaller draws on top. That
ordering is unverified until the emulator run, the same way `BOOT_ART_Z_BACK` was, and is
a one-line fix if inverted.

**Lightning** is `menu_art_flash()`, folded into `menu_art_present`: on a random schedule
it reloads the title card's CRAM bank with its `HighColor` channels scaled up for a few
frames, then restores them. No new art, no new VRAM, and it cannot desynchronise from the
backdrop because it *is* the backdrop's palette. `CRAM::Palette::Load` is a raw
`slDMACopy`, so the big-endian bytes land verbatim — the same property the boot art
relies on.

### `tools/mkmenuart.py`

Imports `read_tga`, `active`, `pad_to`, `high_color`, `reduce_palette` and `write_art`
from `mkbootart.py` so the `.ART` format is single-sourced. Emits:

| File | Size | Contents |
|---|---|---|
| `MENUFONT.ART` | 8x512 | 64 glyphs, ASCII `0x20`–`0x5F`, 8x8 each, `Paletted16` |
| `MENUPANP.ART` | 168x96 | Pause panel: border and fill |
| `MENUPANS.ART` | 272x168 | Slot-list panel |
| `MENUPANC.ART` | 240x72 | Confirm panel |

Uppercase only, because every string in these menus is uppercase — the same 64-glyph range
Another-Saturn's font covers.

Both text ramps and both panel colours are **derived from `HEART OF THE ALIEN003.tga`**,
not hand-picked: the tool reads the title card, reduces it the way `mkbootart.py` does,
picks the dim ramp from its darker blues and the selected ramp from a brightened copy of
the same hues, and writes them as two CRAM banks. A re-grab of the TGA re-keys the menus
automatically, which is what "keyed to the muted blue tones" has to mean if it is to stay
true.

`verify()` fails the build on any width that is not a multiple of 8. `CMDSIZE` stores
horizontal size in 8-dot units; a 274-wide band was read as 272 last time and the picture
sheared two pixels further right on every line. It also fails on a set origin byte in the
TGA header — a flipped `header[17]` draws every sprite upside down and reports six clean
files, which is a check `mkbootart.py`'s `verify()` was missing.

### `main.c` — the two call sites

```c
        if (next_script == PASSWORD_ROOM)      /* 7 */
        {
                next_script = menu_gate();
        }

        if (next_script != 0)
        {
                ... existing block, unchanged ...
        }

        check_events();

#ifdef HOTA_SATURN
        menu_pause_poll();
#endif
```

The `#ifdef HOTA_SATURN` block that holds `saturn_save_poll()` is **kept and repurposed,
not deleted**. Its position — between `check_events()` and the task loop's first
`toggle_aux(0)` — is the only place in the program where `quicksave`'s and `quickload`'s
"no active thread" precondition holds, and it is the save point that has been verified on
hardware. `menu_pause_poll` replaces the chord's body at that same call site;
`s_saveDevice` and `input_debug_chord` go with the chord.

The gate sits one statement earlier, still outside the task loop, so the same precondition
holds for a load performed from it.

## Data and control flow

```
power on
  └─ boot_sequence()            unchanged: 4 stills, OOTW / HOTA select
       └─ run()
            ├─ play_intro()     INTRO1..4.BIN, tracks 31..34, skippable
            ├─ next_script = 7
            │
            └─ loop ──► next_script == 7 ?
                          │   (ending_played forces GATE_SUBTITLE, then clears)
                          │
                          ├─ GATE_SUBTITLE ─► sub-title menu ──┬─ START GAME ─► room 1
                          │                    ▲               ├─ LOAD GAME ──► slots
                          │                    │               └─ 15 s idle ──► play_intro()
                          │                    └───────────────────────────────────┘
                          │
                          └─ GATE_LOAD ─────► slots (load) ────┬─ load ok ────► 0
                                                               └─ cancel ────► sub-title
                        │
                        ▼
                     gameplay ──► Start ──► pause ─┬─ resume
                                                   ├─ save/load ─► slots
                                                   └─ return to title ─► confirm ─► 7
```

**Start a new game.** `menu_gate()` returns `MENU_START_ROOM` (1). The existing block
loads it exactly as it loads any room. One named constant, one line to change if the first
emulator run shows the game starts elsewhere.

**Load a save.** `menu.c` calls `saturn_saveslot_load(device, slot)`. On `SAT_BUP_OK` the
gate sets mode `GATE_LOAD` and returns 0, so the existing block is skipped and the state
`quickload()` restored survives. On any error the slot list stays up with the error string
on its status line — `saturn_saveslot_load` documents the running game as untouched on
every failure.

**Save.** `menu_pause_poll` calls `saturn_saveslot_save(device, slot)` at the same frame
boundary the verified chord used.

**Attract.** 15 s idle on the sub-title screen: `menu_art_end()` restores NBG0,
`disc_stop_track()`, `play_intro()`, then `menu_art_begin(1)` and `menu_clock_enter()` to
re-enter with track 03 from zero. The cinematic needs NBG0 and the disc, so the overlay
must come down first — that ordering is the reason `menu_art_begin`/`menu_art_end` are
separate from `menu_art_load`.

## Memory

**HWRAM heap.** New object files push `__heap_start` up, which is what turned a 366-byte
shortfall into a black screen once. The margin is now 138,898 bytes since the work-area
fix took `SGL_MAX_POLYGONS`/`SGL_MAX_VERTICES` to 256 and moved `__heap_end` to
`0x060e2000`. This feature's objects are far inside that. Nothing here allocates from the
heap at runtime.

**LWRAM.** `saturn_lwram_alloc`/`saturn_lwram_free` is a real allocator, not a bump
allocator — `saturn_bootart.cxx` takes 36,864 bytes for its staging buffer and frees it on
every exit path, success included. `menu_art_load` follows exactly that pattern with a
buffer sized by its largest file, the 272x168 panel at 22,856 bytes, so nothing is held
across frames. `menu.c` allocates `savedata_probe`'s `SAVE_MAX_BYTES` scratch on entry to a
menu and frees it on exit, for the same reason. Peak simultaneous LWRAM is therefore one
of the two, never both.

**Sprite VRAM.** 4 KB of font (64 glyphs x 32 bytes, plus alignment), 40 KB of panels.
Against boot art's ~185 KB of 512 KB. Loaded once and never released: VDP1's allocator is
a bump allocator with no free, so an attract loop that reloaded would run 512 KB dry in a
few passes.

**Texture slots.** `SRL_MAX_TEXTURES` 100 → **128**. Boot art holds 7, this holds 67.

**CRAM banks.** Boot art holds 7 `Paletted16` banks of 128. This adds two — the dim and
selected text ramps — shared by every glyph through `DrawSprite`'s palette override.

**Command list.** The slot list is the worst case: one backdrop, one panel, and roughly
130 glyphs once spaces are dropped. `SGL_MAX_POLYGONS` is 256, so it fits with margin, and
`menu_layout_build`'s `cap` is set to make an overrun impossible rather than merely
unlikely. Raising the ceiling costs 84 bytes of heap per polygon against 138 KB spare if a
later screen needs it — but note `saturn/makefile`'s constants only take effect because
`all` runs `clean-preserve-audio` first; a bare `make build` relinks a stale `workarea.o`
and changes nothing.

## Assets

`HEART OF THE ALIEN003.tga` is already the source of `BOOTTITL.ART`
(`tools/mkbootart.py:59`), 320x224 at 16 colours, and is already loaded as a VDP1 texture
by `boot_art_load()`. The sub-title menu's background needs no new asset — it needs an
accessor.

New on the disc: `MENUFONT.ART`, `MENUPANP.ART`, `MENUPANS.ART`, `MENUPANC.ART`.

`.ART`, not `.BIN`, for the reason recorded in the boot sequence work: `.gitignore` carries
`saturn/cd/data/*.bin`, git matches it case-insensitively on Windows, and `.BIN` assets
silently fail to commit.

## Decisions worth naming

### The gate keys on the room, and takes one hint

Keying on arrival at room 7 is what makes the gate cover every route into the password
screen, including the death route this design cannot see in the code. The mode is then set
by events the driver already owns — a start, a load, a *Return to Title*.

The ending is the one case the driver cannot infer, and it is also the one case where
getting it wrong is rude: a player who has just watched the credits should not be handed a
list of saves to reload. So `decode.c` sets a flag on the path that already knows, and the
gate consumes it. That is a hint, not a second mode — the flag selects an existing mode for
one arrival and clears itself.

The alternative considered and rejected was inferring it from `current_room`. It does not
work: `current_room` holds whichever room ran the opcode that started the ending, not the
ending itself, so the value differs by playthrough and means nothing at the gate.

### The pause menu never copies the frame

Drawing over a live NBG0 costs nothing and cannot perturb engine state. The alternative —
Another-Saturn's `menuFreezeRemap` over a page buffer — would need a spare 304x192 buffer,
and this engine has only three distinct screen buffers, all owned by the VM
(`screen.c:234-240`, where `screens[1]` and `screens[2]` are the same pointer). Borrowing
one would risk corrupting state on resume, and allocating a fourth would cost 58 KB of
LWRAM for a picture that is already on screen.

### Text is one sprite per glyph, and spaces are free

The obvious objection is command-list cost. Measured against the actual strings it is
~130 for the widest screen against a 256 ceiling, and dropping spaces is most of why. The
alternative — compositing text into a page and uploading it as a texture each frame —
would need a per-frame 35 KB DMA into a texture the allocator cannot re-issue.

### Both ramps are CRAM banks, not two fonts

`DrawSprite`'s palette override makes this free. Two texture sets would have doubled the
glyph count against a 100-slot table and doubled the VRAM for no benefit.

### Panels are baked, not scaled

`DrawSprite` accepts a runtime scale, so one 8x8 texture could serve all three panels. It
would also require constructing an `Fxp` scale from runtime values, and `Fxp`'s
floating-point constructor is `consteval` — a function parameter is never a constant
expression. Three baked panels cost 40 KB of a 512 KB budget and have no such edge.

### The ramps are derived from the title card, not chosen

Hand-picked colours drift the moment the art is re-grabbed. `mkmenuart.py` reading
`HEART OF THE ALIEN003.tga` and deriving both ramps from its measured palette is the only
form of "keyed to the muted blue tones" that stays true without a human re-checking it.

### Lightning is a palette flash

Another-Saturn's lightning is three bolt shapes cut from its own artwork; this port has no
equivalent art and inventing some would be a second art pipeline for a decoration. Scaling
the backdrop's own CRAM entries reads as a flash behind the scene, costs nothing, and has
exactly one failure mode — the restore — which is a single write.

### `ROOM nn`, not a chapter name

`current_room` is a byte and this engine has no name table for it anywhere; the header
stores the id and the menu decides how to render it, which was the plan recorded in the
saves handoff. Inventing names is a content decision, not a port decision.

## Error handling

**Art that will not load must never brick the disc.** `menu_gate()` calls
`menu_art_load()` once; on failure it returns `MENU_START_ROOM` immediately and the game
boots with no menu. `boot_sequence()` already follows this rule and it is the reason a
build without the TGAs boots into the game instead of hanging on black.

**A failed load leaves the game running.** `saturn_saveslot_load` guarantees it. The slot
list stays up with the error on its status line.

**Save and load errors are named, not numbers.** `menu_layout.c` carries the string table
lifted from `..\Another-Saturn\saturn\src\menu.cxx:195-212`: `SAVE STATE TOO LARGE`,
`NO BACKUP DEVICE`, `BACKUP RAM UNFORMATTED`, `CARTRIDGE UNFORMATTED`,
`CARTRIDGE WRITE PROTECTED`, `NOT ENOUGH SPACE`, `SAVE NOT FOUND`, `SLOT ALREADY IN USE`,
`SAVE DATA DAMAGED`, `SAVE FAILED`.

**A tiny file that starts with `HOTA` probes as `SLOT_OK`.** `savedata_probe` does not
require `entry.size >= SAVE_HEADER_SIZE` before parsing, which is a known Minor from the
saves review. The slot list will show a garbage room and date for such a slot, and loading
it fails cleanly through `savegame_read`. Fixing the probe is a one-line guard and belongs
in this work rather than being carried forward again.

**No cart, no device toggle.** `cartPresent` gates left/right in `stepSlots`, so a device
the machine does not have cannot be selected.

## Testing

Three host suites under `saturn/tests/`, added to `run_tests.sh`. They do **not** go into
`saturn/src/Makefile`'s `OBJS`: the gate and the pause poll are `#ifdef HOTA_SATURN`, so
the host `alien` binary never references `menus/` and never needs to compile it.
`run_tests.sh` compiles the `.c` files directly, the way it already does for `bootmenu.c`.
The `OBJS` warning still stands for any file that does become host-side — that list does
not glob, which is what left the host build broken from `3b10479`.

- `test_menu_state.c` — every transition, both cancel paths out of the slot list, and
  specifically that a cancel out of the gate's post-death load screen lands on the
  sub-title menu.
- `test_menu_layout.c` — cursor row renders selected and every other row dim; empty,
  damaged and old-version rows; a full row fits the panel; the item count never exceeds
  `cap`; spaces emit nothing.
- `test_menu_clock.c` — the volume staircase, the restart at 40 s, the fade in on entry,
  the idle trigger at 15 s and its reset on input, and correctness across the millisecond
  counter's wrap.

**Every suite is mutation-tested before it is trusted.** Reverse the confirm condition,
gut the fade ramp, delete the idle-expiry block, make `menu_layout_build` ignore the
cursor — each must turn a test red. Three "checks" in the boot sequence plan verified
nothing and each looked like coverage; this is the countermeasure that caught them.

`saturn_menuart.cxx` and `menu.c` get `-fsyntax-only` with the flags `make -n` prints and
nothing more. Nothing automated reaches VDP1, CRAM or the disc. **The emulator run is the
real test**, and its first item is the death route: play to a death and observe whether
the load screen appears.

## Build and test

- `sh saturn/tests/run_tests.sh` from the repo root — existing suites plus three.
- `python tools/mkmenuart.py` — regenerates the four `.ART` files; `verify()` fails the
  build on a bad width or a flipped origin.
- Saturn build: the human runs it. From PowerShell, `cmd.exe /c ".\compile.bat debug"` in
  `saturn`. `make -n` is permitted here for reading flags; real `make`, `compile.bat` and
  Mednafen are not.
- Syntax checks: `../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe` with the flags
  `make -n src/<file>.o` prints. The compiler is not on `PATH`.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Deaths do not route through room 7, so the load screen never appears after one | The gate is trigger-agnostic and loses nothing if so. First item on the emulator run; a second hook after `play_death_animation` is a follow-up |
| `MENU_START_ROOM` is not room 1 | One named constant, one line. Nothing else depends on the value |
| Sprite Z ordering inverted, so text draws behind the panel | Same unverified assumption `BOOT_ART_Z_BACK` carried; swap two constants |
| Command list overruns 256 on the slot list | `menu_layout_build` clamps to `cap`; `cap` is set below the ceiling, so an overrun is impossible rather than unlikely |
| A task adds a caller before its callee and the link breaks for every task in between | `saturn/makefile` globs `src/**/*.c`; the plan lands caller and callee in the same task. This cost a mid-execution resequencing on the saves work |
| `Button::START` mistyped as `Button::Start` | Documented in the saves handoff as a build-costing trap; it is the only uppercase member of that enum |
| The debug chord's call site is deleted along with the chord | The `#ifdef` block is kept and its body replaced. That position is the only place `quickload`'s precondition holds |
| Host build breaks because `saturn/src/Makefile`'s `OBJS` was not updated | Nothing here is host-side, so nothing is added to it. If that changes, note the list does not glob — it is why the host build was broken from `3b10479` |
| `ending_played` is read but never cleared, so every later death lands on the sub-title menu | The gate consumes it in the same statement that acts on it. The emulator run checks both halves: credits then sub-title menu, then a death in that new game landing back on the load screen |
| `play_intro` is `static` in `main.c`, so `menu.c` cannot call it for the attract loop | It loses `static` and gains a declaration in `main.h`. The alternative — a re-entrant gate that returns "play the cinematic and call me again" — buys nothing and complicates the one loop that must stay readable |
| `SRL_MAX_TEXTURES` raised without a clean rebuild | `saturn/makefile` constants only take effect through `clean-preserve-audio`; a bare `make build` relinks stale objects silently |

## Deferred and stubbed

**Half-transparent panels.** VDP1 supports half-transparency and it would read better over
a live game frame than a solid fill. The panels ship opaque; turning the bit on is a
per-sprite attribute change with no structural consequence, and it is not worth risking on
a first pass through an unverified drawing path.

**A second hook after `play_death_animation`.** Only built if the emulator run shows deaths
do not reach room 7.

**`sat_bup_delete` still has no caller.** A "delete save" row is not in these menus. It was
already noted as an unused entry point in the saves review and stays that way.

**Attract-loop cinematic replay is unbudgeted.** `play_intro()` reads four `.BIN` files and
plays four CD-DA tracks; nothing here measures how long the tear-down and re-entry take.
If the seam between the sub-title menu and the cinematic clicks or flashes, the fix is the
one the boot menu already needed — a fade under the transition — and it is a follow-up.

**Naming.** `menus/` is C, so its functions are `snake_case` (`menu_state_step`,
`menu_layout_build`, `menu_clock_step`) even though the types carried over from
Another-Saturn's C++ keep their `MenuState` / `MenuAction` spelling. That mix is
deliberate: renaming the types would make the diff against the source file unreadable.

## Out of scope

**Removing `ROOMS7.BIN` from the disc.** It would free 160,826 bytes on a disc with room
to spare, and would require regenerating the hardcoded offset table in `disc_manifest.h`
— which is a cached ISO9660 directory, so every entry after it shifts. Leaving the file
unreachable costs nothing.

**A chapter-name table for `current_room`.** Argued above.

**An options screen, region or language selection.**

**Running *OUT OF THIS WORLD*.** Unchanged from the boot sequence spec: this engine
carries no Part I data.

**Changing the boot stills or the game-select menu.** They are verified on hardware.

**A second death hook.** Contingent on what the emulator run shows; see Risks.

**Host (SDL) parity for the menus.** The gate and the pause poll are `#ifdef HOTA_SATURN`;
the host build keeps its existing quicksave path unchanged.

## Acceptance

1. Selecting *HEART OF THE ALIEN* plays the opening cinematic.
2. Pressing any button during it lands on the sub-title menu; so does letting it finish.
3. The sub-title menu shows the title card with `START GAME` and `LOAD GAME` in the card's
   own blue ramp, the selected row brighter, and an occasional lightning flash.
4. Track 03 starts from zero on arrival, fades out completing at 40 s, and restarts with a
   fade in.
5. 15 s without input replays the opening cinematic and returns to the sub-title menu with
   the music restarted.
6. `START GAME` begins a new game.
7. `LOAD GAME` lists three slots with room and date, loads a good one, and reports a bad
   one without disturbing anything.
8. Start during play opens the pause menu; resume returns to the exact frame it covered.
9. Save writes a slot; overwriting asks first and defaults to no.
10. *Return to Title* asks first and lands on the sub-title menu.
11. The password entry screen is never shown, at any point, including after the ending.
12. Finishing the game lands on the sub-title menu, not the load screen; a death in the
    game started from it lands on the load screen again.
13. A build with the menu `.ART` files missing boots into the game rather than hanging.
