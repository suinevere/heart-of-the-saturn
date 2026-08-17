# Heart of The Alien → Sega Saturn — Controller Menu Design Spec

**Date:** 2026-08-16
**Status:** Implemented, then revised — see Revisions below
**Target engine:** SaturnRingLib (SRL)

## Revisions, 2026-08-16

Three changes landed after the design below was implemented. **Where this document and
the code disagree, the code is right and these notes say why.** The body is left as the
record of the original reasoning rather than rewritten around it.

1. **The jump-forward row is gone.** It was redundant: the chord is emergent, so a row
   naming one button for it was a second route to a move the other two rows already
   produce. `KeymapRow` is three rows, all always bound. The cascade is larger than the
   deletion — the shortcut row was the only row allowed to hold `PAD_NONE`, and that was
   the only reason `keymap_assign` could refuse anything, so the rejection case, the
   `IN USE` state, `MenuState.mapRejected` and the `LEFT CLEARS` hint all went with it.
   Every section below about the shortcut row, its rejection case, or Left clearing it
   describes code that no longer exists.
2. **`KEYMAP_FORMAT_VERSION` is 2**, carrying three bindings at bytes 6..8. A stored v1
   entry is refused and falls back to defaults rather than being read short.
3. **A and C both confirm in every menu.** `menu_key_mask` sets `MENU_BIT_CONFIRM` from
   `PAD_BIT_A | PAD_BIT_C`. The boot menu already accepted A, B or C
   (`BOOT_KEY_CONFIRM`), so this makes the rest of the game agree with it.

A fourth change, the death screen, is a separate feature and is documented in its own
section at the end of this file.

## Goal

Let the player reassign the three gameplay buttons, and give the jump-forward move an
optional button of its own, from a screen reachable while playing, with the choice
surviving a power cycle.

Today `input_srl.cxx` hardwires SRL's A, B and C to `key_a`, `key_b` and `key_c` and
leaves X, Y, Z, L and R doing nothing. Jump forward is run-and-jump held together, which
on a Genesis pad is two adjacent buttons in one row and on a Saturn pad is a reach the
original never asked for.

Done means: a *CONTROLS* screen reachable from the pause menu and from the sub-title card;
Run/Shoot/Shield, Whip and Jump each captured to any of the eight face and shoulder
buttons; a fourth row binding one optional button that fires run and jump together;
collisions resolved by swapping; the mapping stored in its own backup RAM entry and
reloaded at boot; and no menu anywhere changing behaviour because of any of it.

## Scope

**A new pure `keymap` module, a new backup RAM entry, a `CONTROLS` screen in the existing
menu layer, and a re-basing of every menu's input onto raw pad state.**

The engine is untouched. `update_keys`, `decode.c`, `vm.c` and the four save functions do
not change, and neither does `video_srl.cxx`, `sound_srl.cxx` or the renderer. The only
edit outside the menu and input layers is `boot_key_mask` in `main.c`, argued under
[The hazard this design exists to avoid](#the-hazard-this-design-exists-to-avoid).

Explicitly not in scope, each argued in [Out of scope](#out-of-scope): remapping the
d-pad, remapping menu navigation, keyboard remapping in the host build, 3D/analog pad
support, per-save-slot mappings, and multiple named profiles.

## What the engine already does, measured

### `update_keys` is the whole of the input contract

`update_keys` (`main.c:404-461`) is the only place the seven key globals become anything
the game can see. It writes VM variables and nothing else:

| Global | Writes | Player-facing action |
|---|---|---|
| `key_right` / `key_left` | var 252 = ±1, flags 1 / 2 | move |
| `key_down` | var 251 = +1, var 229 = +1, flag 4 | crouch / down |
| `key_up` | var 229 = −1, flag 8 | up |
| `key_a` | var 250 = 1, var 254 \|= 0x80 | **Run / Shoot / Shield** |
| `key_b` | var 254 \|= 0x40 | **Whip** |
| `key_c` | var 251 = −1, flag 8 | **Jump** |

Tap A shoots, hold A shields, releasing a held A fires the super blast, and A held with a
direction runs — all four are one global as far as this design is concerned, because the
distinction is made inside the room scripts from how long var 250 stays set, not by the
input layer.

### The chord is free, and cannot be broken

There is no A+C input. The engine reads *level* state from seven independent globals and
never reads a transition, so "jump forward" is nothing more than `key_a` and `key_c` both
true on the same frame.

**Any mapping that writes `key_a` from one button and `key_c` from another therefore
reproduces jump forward automatically, whatever those two buttons are.** Nothing has to
implement the chord, and no mapping can remove it.

That is why the fourth row is a *shortcut* and not a binding. It names one button that
sets both globals at once. Setting it to `NONE` costs convenience and never costs the
move — which is the property that lets the row be freely configurable without any risk of
stranding the player.

### Up is a fourth direction, not a fifth action

`key_up` writes var 229 while `key_c` writes var 251, so up and jump are genuinely
different inputs even though both raise flag 8. Up stays on the d-pad and gets no row:
the d-pad is movement and menu navigation at once, and remapping it would break the
second to serve the first.

## The hazard this design exists to avoid

`menu_key_mask` (`menu.c:155-169`) builds the pause menu's input out of the key globals —
`key_a` is confirm, `key_b` is cancel. `boot_key_mask` (`main.c:1244-1257`) folds the same
globals into the boot menu's `BOOT_KEY_*`.

Those are exactly the globals a remap writes. Left alone, binding Run to X would silently
move the pause menu's confirm button to X as well, and the boot menu's with it. That is
the opposite of "gameplay only", and it is a direct route to a player who cannot operate
the screen that would let them undo the change.

**The rule, stated once and applied without exception: the keymap writes the key globals,
and every menu reads raw pad state.** No ordering invariant to preserve, no "the boot menu
is fine because the map loads later" argument that breaks the day someone moves the load.

This needs one new seam and removes another:

- `input_raw_buttons(void)` returns a `PAD_BIT_*` mask of everything physically held on
  port 0 this frame, before any mapping. Saturn-only, behind `HOTA_SATURN`, exactly as
  `input_menu_start` already is.
- `input_menu_start()` is **removed**. Start becomes `PAD_BIT_START` in the mask, and
  `menu.c:166` — its only caller in the tree — reads it from there.

Three callers move to the mask: `menu_key_mask`, `boot_key_mask`, and the capture state on
the new screen.

## Modules

### `saturn/src/keymap.c` / `keymap.h` — pure

No `srl.hpp`, no engine headers, no backup RAM. Host-testable with gcc, the same
discipline `savedata`, `menu_state`, `menu_layout` and `bootmenu` already keep.

```c
typedef enum {
    PAD_NONE = 0,
    PAD_A, PAD_B, PAD_C, PAD_X, PAD_Y, PAD_Z, PAD_L, PAD_R
} PadButton;

#define PAD_BIT_A 0x001  /* … through PAD_BIT_R */
#define PAD_BIT_UP 0x100 /* … the four directions */
#define PAD_BIT_START 0x1000

typedef enum {
    KEYMAP_ROW_RUN,      /* Run / Shoot / Shield → key_a */
    KEYMAP_ROW_WHIP,     /* Whip                 → key_b */
    KEYMAP_ROW_JUMP,     /* Jump                 → key_c */
    KEYMAP_ROW_FORWARD,  /* Jump forward         → key_a and key_c */
    KEYMAP_ROW_COUNT
} KeymapRow;

typedef struct { PadButton row[KEYMAP_ROW_COUNT]; } KeyMap;
```

`PadButton` living in a plain header is what keeps capture from breaching the state
machine's purity: `menu_state.c` handles a button *identity*, and only `input_srl.cxx`
ever knows it corresponds to an `SRL::Input::Digital::Button`.

The module owns one active `KeyMap` behind `keymap_active()`, so `input_srl.cxx` and
`menu.c` cannot drift apart holding two copies. The screen edits `MenuState`'s own copy
and never the active one — the active map is replaced, once, when the screen closes and
the caller handles `MENU_ACT_SAVE_KEYMAP`. Editing in place would change the controls
under a player who then cancels.

| Function | Contract |
|---|---|
| `keymap_defaults(KeyMap *)` | A, B, C, `PAD_NONE` — today's hardwiring exactly, so a fresh console plays identically to the current build |
| `keymap_assign(KeyMap *, KeymapRow, PadButton)` | the swap rule below; returns 1 if applied, 0 if rejected |
| `keymap_apply(const KeyMap *, unsigned raw, int *a, int *b, int *c)` | mask in, three globals out |
| `keymap_serialise(const KeyMap *, unsigned char *)` | 16-byte entry |
| `keymap_parse(KeyMap *, const unsigned char *, int len)` | validates before writing; leaves the map untouched on any failure |

#### The swap rule, and the one case it rejects

No two rows may hold the same non-`NONE` button. Assigning button `b` to row `r`:

1. Let `q` be the row currently holding `b`, if any.
2. If there is no such `q`, set `r = b`. Done.
3. Otherwise `q` takes `r`'s previous binding and `r` takes `b`. One reversible step; the
   player can always undo it by capturing again.

Step 3 has exactly one bad case, and it is worth naming because it is easy to implement
wrong: if `r` is the shortcut row and its previous binding was `PAD_NONE`, then `q` — a
core row — would be handed `PAD_NONE` and Run, Whip or Jump would go dead.

**A capture that would leave a core row unbound is rejected.** The map does not change and
the screen shows `IN USE`. This is reachable only from "shortcut is NONE, player picks a
button a core row owns", and the fix available to the player is obvious: give the shortcut
a free button, or move the core row first.

The three core rows can never hold `PAD_NONE`; only the shortcut row can — and since a
capture can only ever produce a button, **Left on the shortcut row is what clears it back
to `NONE`.** `keymap_assign` accepts `PAD_NONE` for that row alone and rejects it for the
other three, so the "no core row unbound" rule holds on this path too.

#### The stored entry

16 bytes, following `savedata.h`'s header conventions (`SAVE_HEADER_SIZE`,
`SAVE_FORMAT_VERSION` at `savedata.h:26-28`): magic `'HOTC'` at 0, version at 4, reserved
at 5, the four bindings at 6..9, zero padding to 16. `keymap_parse` refuses a bad magic,
an unknown version, a short buffer, an out-of-range `PadButton`, a duplicate non-`NONE`
binding, or a `PAD_NONE` in a core row — and on every one of those the caller falls back
to `keymap_defaults`, because a corrupt config must never be worse than a fresh one.

### `saturn/src/system/saturn_keymap.cxx` — backup RAM

A sibling of `saturn_saveslot.cxx`, and deliberately not part of it: a save slot is
per-playthrough game state, a control mapping is a per-console preference. Folding one
into the other would mean a slot-format bump every time a preference changes, and would
pull control state into `menu_state.h` through the `savedata.h` include it already has.

Filename `HOTA_CFG`. The entry is 16 bytes and internal backup RAM is the one device
always present, so internal leads on both paths:

- **Load** reads `SAT_BUP_INTERNAL`, and on `SAT_BUP_ERR_NOT_FOUND` or
  `SAT_BUP_ERR_UNFORMAT` tries `SAT_BUP_CART`. First valid entry wins; anything else means
  defaults.
- **Save** writes `SAT_BUP_INTERNAL`, and only on `SAT_BUP_ERR_NO_SPACE`,
  `_UNFORMAT` or `_PROTECTED` falls back to `SAT_BUP_CART`.

This deliberately does not use `savedata_pick_default_device`: that picks a device by
which one holds *saves*, which is the right question for a save slot and the wrong one for
a 16-byte preference.

On a write failure to both, the mapping is session-only and the screen reports it through
the existing `menu_layout_status_text` wording for `SAT_BUP_*` codes — the same
degradation the save menu already performs via `cartPresent`.

- `saturn_keymap_load(KeyMap *)` — called once from platform init, after
  `saturn_saveslot_init`. Any failure leaves defaults.
- `saturn_keymap_save(const KeyMap *)` — called when the screen closes, and only if the
  map actually changed.

### `menu_state.c` — the screen

Additions to the existing pure state machine:

- `MENU_CONTROLS` in `MenuScreen`.
- `MENU_ACT_SAVE_KEYMAP` in `MenuAction`, meaning the caller must persist `st->map`.
- `MenuState` gains `KeyMap map` and `int capturing` (the row being captured, or −1).
- `MenuInput` gains `PadButton captured` — the button that became held this frame, or
  `PAD_NONE`.

  This needs a **second** edge, not the one `menu.c:508` already computes. That one is
  `current & ~previous` over the seven-bit `MENU_BIT_*` mask, which cannot distinguish X
  from Z because neither has a bit in it. `menu.c` therefore keeps a `previousRaw`
  alongside `previous` and derives `captured` from the raw edge, resolving ties by the
  `PadButton` order if two buttons land on the same frame.
- `menu_state_enter_controls(MenuState *st, MenuScreen back)`, reusing the `returnScreen`
  mechanism `menu_state_enter_slots` established, so the screen can be opened from both
  the pause menu and the sub-title card without either knowing about the other.

Six rows: Run/Shoot/Shield, Whip, Jump, Jump Forward, Reset To Defaults, Back.

Up and Down move the cursor. Confirm on one of the four binding rows enters capture.
Confirm on Reset restores `keymap_defaults`. Confirm on Back, or Cancel from the row list,
returns to `returnScreen` and raises `MENU_ACT_SAVE_KEYMAP` if the map changed.

#### Capture has exactly one escape, and it is Start

In capture mode every one of the eight face and shoulder buttons is capturable — including
B, which is Cancel everywhere else. Letting B mean cancel here would make B the one button
the player could never bind to Whip's neighbour rows, and letting it mean capture would
leave the mode with no way out.

**Start aborts a capture, and Start is the only thing that does.** It is already
non-capturable for being the pause button, so there is no ambiguity to resolve, and the
screen says so: `PRESS BUTTON — START CANCELS`. No new `MenuInput` field is needed for it:
Start already arrives as `in->pause`, which `MENU_CONTROLS` reads as "abort capture" while
`capturing >= 0` and ignores otherwise.

The four directions are likewise never reported as `captured`. `step_controls` returns from
the capture block before reaching the up/down handlers, though, so the cursor is locked
while capturing rather than free to move underneath it — a d-pad that moved the cursor
mid-capture would be confusing.

### `menu_layout.c` — the rows

**No new panel.** `CONTROLS` draws on `MENU_PANEL_SLOTS`. A fourth panel id would cost a
new art asset, a `disc_manifest.h` entry, an art-tool run and a bump to
`MENU_ART_PANEL_COUNT` and `g_panel[]` in `saturn_menuart.cxx` — all to obtain geometry
identical to the slots panel's, since the rows are narrower than a slot row. Reusing it
means the art layer needs no change at all for this feature.

Each row is a label glyph run plus
a binding name right-aligned: `A` … `Z`, `L`, `R`, or `NONE`; `PRESS BUTTON` in place of
the name on the row being captured.

The widest constructible row is `RUN/SHOOT/SHIELD` plus `NONE` — 16 and 4 characters,
well inside `MENU_ROW_CHARS` (32) and, from `MENU_SLOTS_ROW_X`, well inside the panel's
293 px interior. Six rows plus a panel plus a backdrop is nowhere near
`MENU_LAYOUT_MAX_ITEMS` (200).

Ramps follow the existing per-item rule: `MENU_RAMP_DIM` / `MENU_RAMP_SEL` on a panelled
screen, which `CONTROLS` is on both routes into it.

### `input_srl.cxx` — translation only

`check_events` reads all eight face and shoulder buttons plus Start and the four
directions into one `PAD_BIT_*` mask, writes the four direction globals straight through,
and calls `keymap_apply` for `key_a`, `key_b` and `key_c`. The `IsConnected` guard and the
port-0-only reasoning in its current banner are unchanged and still load-bearing — an
unguarded read on any other port still reads every button as held.

`input_raw_buttons` returns that mask. `input_menu_start` is deleted.

## Testing

Following the fifteen existing pure-module binaries, and using `stub_saturn_backup.c` for
the backup path as the save tests already do.

**New `run_tests_keymap.exe`:**
1. Defaults are A, B, C, NONE — byte-identical behaviour to the current hardwiring.
2. `keymap_apply` routes each bound button to the right global and nothing else.
3. The shortcut row sets `key_a` and `key_c` together from one button.
4. **Chord emergence:** with run on X and jump on Y and the shortcut NONE, holding X and Y
   sets both globals. This is the test that proves jump forward cannot be lost.
5. Swap: assigning a taken button moves the displaced row to the freed one.
6. Rejection: shortcut NONE plus a core row's button leaves the map bit-identical.
7. Serialise/parse round trip; parse refuses bad magic, bad version, short buffer,
   out-of-range button, duplicates, and a NONE in a core row.

**Extended `run_tests_menustate.exe`:** entering controls from each of the two screens and
cancelling back to the right one; cursor movement over six rows; capture enter, abort on
Start, and completion; the Reset row; `MENU_ACT_SAVE_KEYMAP` raised only when the map
changed.

**Extended `run_tests_menulayout.exe`:** row text for every binding value including NONE
and the capture prompt; item count under `MENU_LAYOUT_MAX_ITEMS`; no row exceeding
`MENU_ROW_CHARS`.

## Acceptance criteria

Revised for the changes above. This is the list to run on hardware.

1. With no `HOTA_CFG` entry present, the game plays exactly as the previous build does.
2. *CONTROLS* opens from the pause menu and from the sub-title card, and cancelling from
   each returns to the one it was opened from. It shows **five** rows.
3. Binding Jump to Z and pressing Z jumps.
4. With Run on X and Jump on Y, holding X and Y performs jump forward — the move survives
   remapping with no row of its own.
5. Capturing a button another row holds swaps the two rows, and nothing is ever refused.
6. Start aborts a capture and leaves the row unchanged.
7. After remapping Run to X, the pause menu's confirm is still A, and the boot menu is
   unaffected.
8. **A and C both select** on the boot menu, the sub-title card, the pause menu, the slot
   list and the controls screen. B still cancels.
9. Power-cycling preserves the mapping; Reset To Defaults restores A/B/C and that survives
   a power cycle too. A console holding a v1 entry from the previous build comes up on the
   defaults rather than a misread mapping.
10. With backup RAM unavailable, the controls screen still works, reports the failure on
    its hint line, and the mapping applies for the session.
11. **Dying opens the death screen** over the frame you died on, cursor on RESUME.
12. RESUME, cancel and Start each reload the room you died in with your progress intact —
    not the title screen, and not a new game.
13. SAVE AND RESUME writes slot 1 and then resumes. Loading that slot afterwards puts you
    at the restarted room, **not** back into the death.
14. A slot row loads it; an empty or damaged slot row does nothing.
15. RETURN TO TITLE reaches the sub-title card, from which START GAME and LOAD GAME both
    still work.

## Out of scope

- **Remapping the d-pad.** It is movement and menu navigation at the same time; remapping
  it would break the second to serve the first, and no player wants it.
- **Remapping menu navigation.** The whole point of the raw-pad rule is that menus stay
  fixed, so a bad mapping is always recoverable.
- **Keyboard remapping in the host build.** `input_sdl.c` has its own scheme and its own
  conveniences (quit, throttle, record/replay) that have no pad equivalent. It keeps
  linking and its tests keep passing; nothing else.
- **3D Control Pad / analog.** `SRL::Input::Digital` on port 0 is what the port reads
  today, and adding a second peripheral family is its own piece of work.
- **Per-save-slot mappings.** Controls are a property of the player and the console, not
  of a playthrough. Storing them per slot would mean loading someone's save changes your
  controls.
- **Multiple named profiles.** One console, one player, one mapping. Revisit only if
  someone asks.
- **Remapping Start.** It is the pause button and the capture escape hatch; it must be the
  one fixed point.

## The death screen

Added 2026-08-16, after the controller menu. A death used to open the load screen, which
meant every death — not just a terminal one — pushed the player into a menu whose only
exits were loading a save or starting over.

`MENU_DEATH` replaces it: **RESUME**, **SAVE AND RESUME**, one row per save slot, then
**RETURN TO TITLE**. Cancel and Start both resume, and the cursor starts on RESUME, so the
stray press a player is probably already making does the least destructive thing.

### Why resume reloads the room rather than continuing the script

`decode.c`'s `0x21` handler sets `next_script = 7; leave = 1` when `death_played` — that
`leave` unwinds the room script, and it is what routes a death to the gate at all. Without
it the script would carry on, which is the engine's own checkpoint restart. By the time
`menu_gate` runs, that script is already gone, so "keep playing" cannot mean "continue
where it left off" from there.

So RESUME returns `current_room` and, deliberately, **does not** call `vm_reset()`.
`run()`'s existing block reloads the room over the surviving variables. A death now costs
the room rather than the playthrough. Reaching for the script instead would have meant
running the menu inside the task loop, where `quicksave`'s no-active-thread precondition
does not hold.

### Why the save is deferred a frame

At the moment SAVE AND RESUME is chosen, the running state is the one the player just died
in — task program counters sitting just past the death opcode. A save taken there would
restore them into the death they just escaped. So the gate sets a pending flag,
`run()` reloads the room, and `menu_deferred_save_poll` writes slot 1 at the next frame
top, beside `menu_pause_poll` and for the same reason: it is the one point where a save is
legal. The device is the one the death screen was showing, carried alongside the flag, so
the save lands where the slot rows the player was looking at came from.

### Out of scope here

The death screen's slot rows **load** only. Saving to a chosen slot is what the pause
menu is for, and SAVE AND RESUME covers the one-press case. The device is shown but not
selectable — a death is not the moment to make someone hunt for their saves.
