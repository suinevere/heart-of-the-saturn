# Sub-title and Save Menus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the port a sub-title menu over the title card, working save/load/pause menus, and one gate that suppresses the Sega CD password screen wherever the engine asks for it.

**Architecture:** Three pure C modules under `saturn/src/menus/` (state machine, draw-item layout, music and idle timing) tested on the host by `run_tests.sh`; one SRL seam under `saturn/src/system/` that draws an item list as VDP1 sprites over whatever NBG0 already holds; one driver that joins them and two call sites in `main.c`'s `run()`.

**Tech Stack:** C99 for the pure modules and the driver, C++ (`.cxx`) for the SRL seam, Python 3 for the asset tool, host `gcc` for tests, `sh2eb-elf-g++` for syntax checks.

**Spec:** `docs/superpowers/specs/2026-08-14-hota-saturn-subtitle-and-save-menus-design.md`

## Global Constraints

- **Banner comments are mandatory.** Every file, and every non-trivial function, constant, enum, struct and static, opens with the repo's banner. `Description:` carries the *reason* — the constraint, the bug avoided, the ordering requirement — never a restatement of the name. `Author: suinevere`. File banners also carry `Dependencies:`. Use `N/A` for fields that do not apply. **No comments inside function bodies.**
- **The human runs the build and the emulator.** Never invoke `compile.bat`, `make` (without `-n`), or Mednafen from a tool call. `make -n` is permitted; it writes nothing.
- **C for portable logic (`.c`), C++ only for SRL (`.cxx`).** A `.cpp` file silently drops out of the link — `shared.mk` has pattern rules for `%.c` and `%.cxx` only.
- **`saturn/makefile` globs `src/**/*.c` and `src/**/*.cxx` recursively.** A task that adds a caller without its callee breaks the link for every task in between. Each task lands caller and callee together.
- **`SRL::Input::Digital::Button::START` is uppercase**; `Right`, `Up`, `Down`, `Left`, `A`, `B`, `C` are mixed case (`srl_input.hpp:710`). The mixed-case `Start` at lines 318 and 465 belongs to other peripheral types. Getting this wrong costs a build.
- **`quicksave`, `quickload`, `quicksave_sprites`, `quickload_sprites` are byte-for-byte frozen.** The payload must stay a contiguous prefix of the host port's own quicksave file.
- **The `#ifdef HOTA_SATURN` block in `run()` that holds `saturn_save_poll()` is kept and repurposed, never deleted.** Its position — between `check_events()` and the task loop's first `toggle_aux(0)` — is the only place `quicksave`/`quickload`'s "no active thread" precondition holds, and it is the save point verified on hardware.
- **`SRL::Fxp`'s floating-point constructor is `consteval`.** A function parameter is never a constant expression, so no helper may take a `double`/`float` and build an `Fxp` from it. Pass `int16_t`.
- **VDP1 sprite width must be a multiple of 8.** `CMDSIZE` stores horizontal size in 8-dot units; nothing rejects a bad width, it only shears on screen.
- **Every new host test suite is mutation-tested before it is trusted.** Break the implementation deliberately, confirm the predicted test fails, restore.
- **Commit after every task.** One sentence, no body, no bullets, no trailers, and no mention of Claude, AI, or the session.

## File Structure

**Created:**

| Path | Responsibility |
|---|---|
| `saturn/src/menus/menu_state.h` | Screen, action, input and state types; four entry points |
| `saturn/src/menus/menu_state.c` | The four screens' transitions. No drawing, no backup RAM, no engine |
| `saturn/src/menus/menu_layout.h` | `MenuItem`, panel and ramp ids, three entry points |
| `saturn/src/menus/menu_layout.c` | State → draw items; slot-row and status strings |
| `saturn/src/menus/menu_clock.h` | Timing constants and the frame struct |
| `saturn/src/menus/menu_clock.c` | Volume ramp, 40 s music cycle, 15 s attract trigger |
| `saturn/src/menus/menu.h` | `menu_gate` and `menu_pause_poll` |
| `saturn/src/menus/menu.c` | The driver: edge detection, action dispatch, the two inner loops |
| `saturn/src/system/saturn_menuart.h` | The C face of the menu's drawing |
| `saturn/src/system/saturn_menuart.cxx` | `.ART` loading, sprite drawing, palette flash, NBG0 control |
| `tools/font8x8.py` | The 8x8 glyph table for ASCII `0x20`–`0x5F` |
| `tools/mkmenuart.py` | Builds `MENUFONT.ART` and three panel `.ART` files |
| `saturn/tests/test_menu_state.c` | Host tests for the state machine |
| `saturn/tests/test_menu_layout.c` | Host tests for layout and strings |
| `saturn/tests/test_menu_clock.c` | Host tests for the timing |
| `saturn/cd/data/MENUFONT.ART` | Generated |
| `saturn/cd/data/MENUPANP.ART` | Generated |
| `saturn/cd/data/MENUPANS.ART` | Generated |
| `saturn/cd/data/MENUPANC.ART` | Generated |

**Modified:**

| Path | Change |
|---|---|
| `saturn/src/savedata.c` | `savedata_probe` rejects a slot shorter than a header |
| `saturn/tests/test_savedata.c` | One new test for that |
| `saturn/src/system/saturn_bootart.h` / `.cxx` | Add `boot_art_title_texture()` and `boot_art_title_flash()` |
| `saturn/makefile` | `SRL_MAX_TEXTURES` 100 → 128 |
| `saturn/src/input.h` | `input_debug_chord` → `input_menu_start` |
| `saturn/src/system/input_srl.cxx` | Same |
| `saturn/src/main.c` | Two call sites; `play_intro` loses `static`; the chord's body is replaced; `ending_played` defined |
| `saturn/src/decode.c` | One `extern` and one assignment on the ending path |
| `saturn/src/main.h` | Declare `play_intro` |
| `saturn/tests/run_tests.sh` | Three new suites |

Not modified: `saturn/src/Makefile`. The gate and the pause poll are `#ifdef HOTA_SATURN`, so the host `alien` binary never references `menus/`.

---

### Task 1: Reject a slot file shorter than its header

`savedata_probe` calls `savedata_read_header` on whatever `sat_bup_read` returned without checking that the entry is at least `SAVE_HEADER_SIZE` bytes. A 4-byte file whose contents happen to be `HOTA` probes as `SLOT_OK` with a garbage room and date, and the menu would display it as a loadable save. This is a known Minor from the saves review; the slot list is the first thing that makes it visible.

**Files:**
- Modify: `saturn/src/savedata.c` — `savedata_probe`
- Test: `saturn/tests/test_savedata.c`

**Interfaces:**
- Consumes: `sat_bup_dir` filling `SatBupEntry { int exists; unsigned long size; unsigned long date; }`
- Produces: no signature change. `savedata_probe` gains one guard

- [ ] **Step 1: Write the failing test**

Add to `saturn/tests/test_savedata.c`, and add a call to it in `main`:

```c
static void test_probe_rejects_undersized_file(void)
{
    unsigned char scratch[SAVE_MAX_BYTES];
    unsigned char file[4];
    SlotInfo info;

    stub_bup_reset();
    file[0] = 'H';
    file[1] = 'O';
    file[2] = 'T';
    file[3] = 'A';
    stub_bup_place(SAT_BUP_INTERNAL, "HOTASAVE1", file, (int)sizeof(file));

    expect_int("a 4-byte file starting with HOTA is damaged, not OK",
               (int)savedata_probe(SAT_BUP_INTERNAL, 0, &info, scratch,
                                   SAVE_MAX_BYTES),
               (int)SLOT_DAMAGED);
}
```

- [ ] **Step 2: Run the test and watch it fail**

```
sh saturn/tests/run_tests.sh
```

Expected: the `savedata` suite reports
`FAIL a 4-byte file starting with HOTA is damaged, not OK / actual = 1 / expected = 2`
(`SLOT_OK` is 1, `SLOT_DAMAGED` is 2). The script uses `set -e`, so later suites do not run; that is expected until the fix lands.

- [ ] **Step 3: Add the guard**

In `saturn/src/savedata.c`, inside `savedata_probe`, replace:

```c
    if (sat_bup_dir(device, name, &entry) != SAT_BUP_OK || !entry.exists) {
        return SLOT_EMPTY;
    }
```

with:

```c
    if (sat_bup_dir(device, name, &entry) != SAT_BUP_OK || !entry.exists) {
        return SLOT_EMPTY;
    }
    if (entry.size < (unsigned long)SAVE_HEADER_SIZE) {
        out->state = SLOT_DAMAGED;
        return SLOT_DAMAGED;
    }
```

Update `savedata_probe`'s banner `Description:` to name the reason — a file too short to hold a header can still start with the magic, and `savedata_read_header` reads 48 bytes regardless of how many arrived, so the size check has to come first.

- [ ] **Step 4: Run the tests and watch them pass**

```
sh saturn/tests/run_tests.sh
```

Expected: every suite passes, including the existing `empty slot`, `good slot` and `damaged slot` tests.

- [ ] **Step 5: Mutation-test the new test**

Comment out the two lines of the guard, re-run, confirm the new test fails and nothing else does. Restore.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/savedata.c saturn/tests/test_savedata.c
git commit -m "Reject a backup slot shorter than its own header, so a four-byte file that happens to start with the magic no longer probes as a loadable save with a garbage room and date."
```

---

### Task 2: `menu_state` — the pure state machine

Ported from `..\Another-Saturn\saturn\src\menu_state.cxx`, converted to C99 and retargeted at this repo's `savedata.h`. Types keep their `MenuState` spelling so the diff against the source stays readable; functions become `snake_case`.

**Files:**
- Create: `saturn/src/menus/menu_state.h`
- Create: `saturn/src/menus/menu_state.c`
- Create: `saturn/tests/test_menu_state.c`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: `savedata.h` — `SlotInfo`, `SlotState` (`SLOT_EMPTY`, `SLOT_OK`, `SLOT_DAMAGED`, `SLOT_OLD_VERSION`), `SAVE_NUM_SLOTS` (3); `saturn_backup.h` — `SAT_BUP_INTERNAL` (1), `SAT_BUP_CART` (2)
- Produces:
  - `void menu_state_enter_title(MenuState *st);`
  - `void menu_state_enter_pause(MenuState *st);`
  - `void menu_state_enter_slots(MenuState *st, int saving, MenuScreen back);`
  - `MenuAction menu_state_step(MenuState *st, const MenuInput *in);`
  - `enum MenuScreen { MENU_NONE, MENU_TITLE, MENU_PAUSE, MENU_SLOTS, MENU_CONFIRM }`
  - `enum MenuAction { MENU_ACT_NONE, MENU_ACT_START_GAME, MENU_ACT_RESUME, MENU_ACT_SAVE_SLOT, MENU_ACT_LOAD_SLOT, MENU_ACT_RETURN_TO_TITLE, MENU_ACT_RESCAN_SLOTS }`
  - `struct MenuInput { int up, down, left, right, confirm, cancel, pause; }`
  - `struct MenuState { MenuScreen screen; int cursor; int slotCursor; int saving; unsigned long device; int cartPresent; int confirmYes; MenuAction pending; SlotInfo slots[SAVE_NUM_SLOTS]; MenuScreen returnScreen; }`

- [ ] **Step 1: Write the header**

Create `saturn/src/menus/menu_state.h`. Give the file banner this rationale: pure logic, host-testable, and the reason it must not include `srl.hpp` or any engine header — the same reason `bootmenu.h` and `discfmt.h` are kept clean.

```c
#ifndef MENU_STATE_H
#define MENU_STATE_H

#include "savedata.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MENU_NONE,
    MENU_TITLE,
    MENU_PAUSE,
    MENU_SLOTS,
    MENU_CONFIRM
} MenuScreen;

typedef enum {
    MENU_ACT_NONE,
    MENU_ACT_START_GAME,
    MENU_ACT_RESUME,
    MENU_ACT_SAVE_SLOT,
    MENU_ACT_LOAD_SLOT,
    MENU_ACT_RETURN_TO_TITLE,
    MENU_ACT_RESCAN_SLOTS
} MenuAction;

typedef struct {
    int up, down, left, right, confirm, cancel, pause;
} MenuInput;

typedef struct {
    MenuScreen screen;
    int        cursor;
    int        slotCursor;
    int        saving;
    unsigned long device;
    int        cartPresent;
    int        confirmYes;
    MenuAction pending;
    SlotInfo   slots[SAVE_NUM_SLOTS];
    MenuScreen returnScreen;
} MenuState;

void menu_state_enter_title(MenuState *st);
void menu_state_enter_pause(MenuState *st);
void menu_state_enter_slots(MenuState *st, int saving, MenuScreen back);
MenuAction menu_state_step(MenuState *st, const MenuInput *in);

#ifdef __cplusplus
}
#endif

#endif /* MENU_STATE_H */
```

Banner notes that must appear:
- On `MenuState`: `returnScreen` is private to `menu_state.c` — it remembers which screen opened the slot list so a cancel goes back to the right place. Callers must not read or write it, except through `menu_state_enter_slots`.
- On `MenuInput`: every field is true only on the frame the button became pressed. The caller owns edge detection.
- On `menu_state_step`: `MENU_ACT_RESCAN_SLOTS` means the caller must re-probe `st->device` and refill `st->slots` before the next call.

- [ ] **Step 2: Write the failing tests**

Create `saturn/tests/test_menu_state.c`:

```c
/*----------------------
 | test_menu_state.c
 | Description: Host unit tests for menu_state.c. Built and run by
 |   run_tests.sh with the host gcc, never by the Saturn makefile -- that
 |   globs src/ under saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: menu_state.h, stdio.h, string.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "menu_state.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static MenuInput none(void)
{
    MenuInput in;
    memset(&in, 0, sizeof(in));
    return in;
}

static void test_title_cursor_and_start(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    expect_int("title starts on start game", st.cursor, 0);

    in = none();
    in.down = 1;
    expect_int("down moves off start game",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("cursor is now load game", st.cursor, 1);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up moves back", st.cursor, 0);

    in = none();
    in.confirm = 1;
    expect_int("confirm on row 0 starts the game",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_START_GAME);
}

static void test_title_load_opens_slots(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    in = none();
    in.down = 1;
    menu_state_step(&st, &in);

    in = none();
    in.confirm = 1;
    expect_int("confirm on row 1 asks for a rescan",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RESCAN_SLOTS);
    expect_int("screen is the slot list", (int)st.screen, (int)MENU_SLOTS);
    expect_int("opened in load mode", st.saving, 0);
    expect_int("slot cursor reset", st.slotCursor, 0);
}

static void test_slots_cancel_returns_to_title(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    expect_int("entered the slot list", (int)st.screen, (int)MENU_SLOTS);

    in = none();
    in.cancel = 1;
    expect_int("cancel yields no action",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("cancel out of a gate-opened slot list lands on the sub-title menu",
               (int)st.screen, (int)MENU_TITLE);
}

static void test_slots_cursor_wraps(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from row 0 wraps to the last slot",
               st.slotCursor, SAVE_NUM_SLOTS - 1);

    in = none();
    in.down = 1;
    menu_state_step(&st, &in);
    expect_int("down from the last slot wraps to 0", st.slotCursor, 0);
}

static void test_device_toggle_needs_a_cart(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    st.device = SAT_BUP_INTERNAL;
    st.cartPresent = 0;

    in = none();
    in.right = 1;
    expect_int("no cart means no toggle",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("device unchanged", (int)st.device, SAT_BUP_INTERNAL);

    st.cartPresent = 1;
    in = none();
    in.right = 1;
    expect_int("a cart means a toggle and a rescan",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RESCAN_SLOTS);
    expect_int("device is the cart", (int)st.device, SAT_BUP_CART);
}

static void test_load_only_from_a_good_slot(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    st.slots[0].state = SLOT_DAMAGED;

    in = none();
    in.confirm = 1;
    expect_int("a damaged slot cannot be loaded",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);

    st.slots[0].state = SLOT_OK;
    in = none();
    in.confirm = 1;
    expect_int("a good slot loads",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_LOAD_SLOT);
}

static void test_save_over_an_empty_slot_does_not_ask(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 1, MENU_PAUSE);
    st.slots[0].state = SLOT_EMPTY;

    in = none();
    in.confirm = 1;
    expect_int("an empty slot saves straight away",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_SLOT);
}

static void test_overwrite_asks_and_defaults_to_no(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 1, MENU_PAUSE);
    st.slots[0].state = SLOT_OK;

    in = none();
    in.confirm = 1;
    expect_int("an occupied slot asks first",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("the confirm screen is up", (int)st.screen, (int)MENU_CONFIRM);
    expect_int("it defaults to no", st.confirmYes, 0);

    in = none();
    in.confirm = 1;
    expect_int("confirming the default does nothing",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("and backs out to the slot list",
               (int)st.screen, (int)MENU_SLOTS);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.right = 1;
    menu_state_step(&st, &in);
    expect_int("right flips to yes", st.confirmYes, 1);
    in = none();
    in.confirm = 1;
    expect_int("confirming yes saves",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_SLOT);
}

static void test_pause_resume_and_return_to_title(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);
    expect_int("pause starts on resume", st.cursor, 0);

    in = none();
    in.pause = 1;
    expect_int("the pause button resumes from any row",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RESUME);

    menu_state_enter_pause(&st);
    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from resume wraps to return to title", st.cursor, 3);

    in = none();
    in.confirm = 1;
    expect_int("return to title asks first",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("the confirm screen is up", (int)st.screen, (int)MENU_CONFIRM);

    in = none();
    in.left = 1;
    menu_state_step(&st, &in);
    in = none();
    in.confirm = 1;
    expect_int("confirming yes returns to title",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_RETURN_TO_TITLE);
    expect_int("and lands on the sub-title menu",
               (int)st.screen, (int)MENU_TITLE);
}

static void test_confirm_cancel_goes_back_where_it_came_from(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);
    st.cursor = 3;
    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);

    in = none();
    in.cancel = 1;
    menu_state_step(&st, &in);
    expect_int("cancelling a return-to-title prompt goes back to the pause menu",
               (int)st.screen, (int)MENU_PAUSE);
}

int main(void)
{
    test_title_cursor_and_start();
    test_title_load_opens_slots();
    test_slots_cancel_returns_to_title();
    test_slots_cursor_wraps();
    test_device_toggle_needs_a_cart();
    test_load_only_from_a_good_slot();
    test_save_over_an_empty_slot_does_not_ask();
    test_overwrite_asks_and_defaults_to_no();
    test_pause_resume_and_return_to_title();
    test_confirm_cancel_goes_back_where_it_came_from();

    if (g_fail != 0) {
        printf("menu_state: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("menu_state: all tests passed\n");
    return 0;
}
```

- [ ] **Step 3: Add the suite to `run_tests.sh`**

Append to `saturn/tests/run_tests.sh`:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system -I../src/menus \
    -o run_tests_menustate test_menu_state.c ../src/menus/menu_state.c
./run_tests_menustate
```

- [ ] **Step 4: Run and watch it fail**

```
sh saturn/tests/run_tests.sh
```

Expected: the compile fails with `menu_state.c: No such file or directory`.

- [ ] **Step 5: Write the implementation**

Create `saturn/src/menus/menu_state.c`. Each `static` step function gets a banner.

```c
#include "menu_state.h"

void menu_state_enter_title(MenuState *st)
{
    st->screen = MENU_TITLE;
    st->cursor = 0;
}

void menu_state_enter_pause(MenuState *st)
{
    st->screen = MENU_PAUSE;
    st->cursor = 0;
}

void menu_state_enter_slots(MenuState *st, int saving, MenuScreen back)
{
    st->screen = MENU_SLOTS;
    st->saving = saving;
    st->slotCursor = 0;
    st->returnScreen = back;
}

static MenuAction step_title(MenuState *st, const MenuInput *in)
{
    if (in->up || in->down) {
        st->cursor = 1 - st->cursor;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        if (st->cursor == 0) {
            return MENU_ACT_START_GAME;
        }
        menu_state_enter_slots(st, 0, MENU_TITLE);
        return MENU_ACT_RESCAN_SLOTS;
    }
    return MENU_ACT_NONE;
}

static MenuAction step_pause(MenuState *st, const MenuInput *in)
{
    if (in->cancel || in->pause) {
        return MENU_ACT_RESUME;
    }
    if (in->up) {
        st->cursor = (st->cursor + 3) % 4;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % 4;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        if (st->cursor == 0) {
            return MENU_ACT_RESUME;
        }
        if (st->cursor == 1 || st->cursor == 2) {
            menu_state_enter_slots(st, st->cursor == 1, MENU_PAUSE);
            return MENU_ACT_RESCAN_SLOTS;
        }
        st->screen = MENU_CONFIRM;
        st->pending = MENU_ACT_RETURN_TO_TITLE;
        st->confirmYes = 0;
        return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

static MenuAction step_slots(MenuState *st, const MenuInput *in)
{
    SlotState state;

    if (in->cancel) {
        st->screen = st->returnScreen;
        return MENU_ACT_NONE;
    }
    if (in->up) {
        st->slotCursor = (st->slotCursor + SAVE_NUM_SLOTS - 1) % SAVE_NUM_SLOTS;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->slotCursor = (st->slotCursor + 1) % SAVE_NUM_SLOTS;
        return MENU_ACT_NONE;
    }
    if (in->left || in->right) {
        if (!st->cartPresent) {
            return MENU_ACT_NONE;
        }
        st->device = (st->device == SAT_BUP_INTERNAL) ? SAT_BUP_CART
                                                      : SAT_BUP_INTERNAL;
        return MENU_ACT_RESCAN_SLOTS;
    }
    if (in->confirm) {
        state = st->slots[st->slotCursor].state;
        if (st->saving) {
            if (state == SLOT_EMPTY) {
                return MENU_ACT_SAVE_SLOT;
            }
            st->screen = MENU_CONFIRM;
            st->pending = MENU_ACT_SAVE_SLOT;
            st->confirmYes = 0;
            return MENU_ACT_NONE;
        }
        if (state == SLOT_OK) {
            return MENU_ACT_LOAD_SLOT;
        }
        return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

static MenuAction step_confirm(MenuState *st, const MenuInput *in)
{
    MenuScreen decline =
        (st->pending == MENU_ACT_RETURN_TO_TITLE) ? MENU_PAUSE : MENU_SLOTS;
    MenuAction action;

    if (in->left || in->right) {
        st->confirmYes = !st->confirmYes;
        return MENU_ACT_NONE;
    }
    if (in->cancel) {
        st->screen = decline;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        if (!st->confirmYes) {
            st->screen = decline;
            return MENU_ACT_NONE;
        }
        action = st->pending;
        st->screen =
            (action == MENU_ACT_RETURN_TO_TITLE) ? MENU_TITLE : MENU_PAUSE;
        return action;
    }
    return MENU_ACT_NONE;
}

MenuAction menu_state_step(MenuState *st, const MenuInput *in)
{
    switch (st->screen) {
    case MENU_TITLE:   return step_title(st, in);
    case MENU_PAUSE:   return step_pause(st, in);
    case MENU_SLOTS:   return step_slots(st, in);
    case MENU_CONFIRM: return step_confirm(st, in);
    default:           return MENU_ACT_NONE;
    }
}
```

Banners that must carry rationale:
- `step_confirm`: `confirmYes` starts false every time the screen is entered, so a stray confirm cannot destroy a save.
- `step_pause`: cancel and the pause button both resume from any cursor position, because a player who opened the menu by accident should not have to find the resume row.
- `menu_state_enter_slots`: exists so the gate can open the slot list directly after a death with `returnScreen = MENU_TITLE`, which is what keeps a player with no saves off a dead end.

- [ ] **Step 6: Run the tests and watch them pass**

```
sh saturn/tests/run_tests.sh
```

Expected: `menu_state: all tests passed`, and every earlier suite still passes.

- [ ] **Step 7: Mutation-test**

Make each of these changes one at a time, re-run, confirm the named test fails, then restore:

1. In `step_confirm`, change `st->confirmYes = !st->confirmYes;` to `st->confirmYes = 1;` → `it defaults to no` / `confirming the default does nothing` must fail.
2. In `step_slots`, delete the `if (!st->cartPresent)` guard → `no cart means no toggle` must fail.
3. In `step_slots`'s confirm branch, change `if (state == SLOT_OK)` to `if (state != SLOT_EMPTY)` → `a damaged slot cannot be loaded` must fail.
4. In `step_title`, change `menu_state_enter_slots(st, 0, MENU_TITLE)` to `menu_state_enter_slots(st, 1, MENU_TITLE)` → `opened in load mode` must fail.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menus/menu_state.h saturn/src/menus/menu_state.c \
        saturn/tests/test_menu_state.c saturn/tests/run_tests.sh
git commit -m "Add the menu state machine, ported from Another-Saturn and retargeted at this port's save layer, with a slot-list entry point the gate uses so cancelling out after a death lands on the sub-title menu rather than nowhere."
```

---

### Task 3: `menu_layout` — state to draw items

Turns a `MenuState` into an ordered list of things to draw, in pixels, in the 320x224 frame. This is where the host tests earn their keep: they assert what renders where and in which ramp, which is the class of bug that actually ships.

Screen geometry is Another-Saturn's, re-centred for this port's 320x224 frame rather than its 320x200 page.

**Files:**
- Create: `saturn/src/menus/menu_layout.h`
- Create: `saturn/src/menus/menu_layout.c`
- Create: `saturn/tests/test_menu_layout.c`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: `menu_state.h` — `MenuState`, `MenuScreen`, `SAVE_NUM_SLOTS`; `savedata.h` — `SlotInfo`, `savedata_date_split`; `saturn_backup.h` — `SAT_BUP_*` codes; `savegame.h` — `SAVE_ERR_*` codes
- Produces:
  - `struct MenuItem { unsigned char kind; unsigned char id; short x, y; unsigned char ramp; }`
  - `int menu_layout_build(const MenuState *st, const char *status, MenuItem *out, int cap);`
  - `void menu_layout_slot_row(char *out, int cap, int slot, const SlotInfo *info);`
  - `const char *menu_layout_status_text(int err, unsigned long device);`
  - `MENU_ITEM_PANEL` 0, `MENU_ITEM_GLYPH` 1
  - `MENU_PANEL_PAUSE` 0, `MENU_PANEL_SLOTS` 1, `MENU_PANEL_CONFIRM` 2
  - `MENU_RAMP_DIM` 0, `MENU_RAMP_SEL` 1
  - `MENU_LAYOUT_MAX_ITEMS` 200, `MENU_ROW_CHARS` 32

- [ ] **Step 1: Write the header**

Create `saturn/src/menus/menu_layout.h`:

```c
#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

#include "menu_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_ITEM_PANEL 0
#define MENU_ITEM_GLYPH 1

#define MENU_PANEL_PAUSE   0
#define MENU_PANEL_SLOTS   1
#define MENU_PANEL_CONFIRM 2

#define MENU_RAMP_DIM 0
#define MENU_RAMP_SEL 1

#define MENU_LAYOUT_MAX_ITEMS 200
#define MENU_ROW_CHARS        32

typedef struct {
    unsigned char kind;
    unsigned char id;
    short         x;
    short         y;
    unsigned char ramp;
} MenuItem;

int menu_layout_build(const MenuState *st, const char *status,
                      MenuItem *out, int cap);
void menu_layout_slot_row(char *out, int cap, int slot, const SlotInfo *info);
const char *menu_layout_status_text(int err, unsigned long device);

#ifdef __cplusplus
}
#endif

#endif /* MENU_LAYOUT_H */
```

Banner notes that must appear:
- On `MenuItem`: `x` and `y` are the top-left corner in pixels in the 320x224 frame, not cells and not the 304x192 the engine renders into — the menu draws as VDP1 sprites over the whole display, which is wider and taller than the game's own bitmap.
- On `id`: for a glyph it is the ASCII code, not an index. The art layer subtracts `0x20`; keeping ASCII here means the tests read as text.
- On `MENU_LAYOUT_MAX_ITEMS`: 200 sits under `SGL_MAX_POLYGONS` (256) with room for the backdrop and the panel, so a full slot list cannot overrun the VDP1 command list. The widest screen measures ~119 items.
- On `MENU_ROW_CHARS`: 32 characters is 256 pixels, which is the widest a row can be and still fit inside the slot panel's interior.

- [ ] **Step 2: Write the failing tests**

Create `saturn/tests/test_menu_layout.c`:

```c
/*----------------------
 | test_menu_layout.c
 | Description: Host unit tests for menu_layout.c. Asserts what renders, where,
 |   and in which ramp -- the pixel packing itself lives in VDP1 and is not
 |   reachable from here, so the layout is what gets pinned.
 | Author: suinevere
 | Dependencies: menu_layout.h, stdio.h, string.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "menu_layout.h"
#include "savegame.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void expect_str(const char *what, const char *got, const char *want)
{
    if (got == 0 || strcmp(got, want) != 0) {
        g_fail++;
        printf("FAIL %s\n  actual   = %s\n  expected = %s\n",
               what, got ? got : "(null)", want);
    }
}

static int count_kind(const MenuItem *items, int n, unsigned char kind)
{
    int i, c = 0;
    for (i = 0; i < n; i++) {
        if (items[i].kind == kind) {
            c++;
        }
    }
    return c;
}

static int ramp_of(const MenuItem *items, int n, int x, int y)
{
    int i;
    for (i = 0; i < n; i++) {
        if (items[i].kind == MENU_ITEM_GLYPH && items[i].x == x
            && items[i].y == y) {
            return (int)items[i].ramp;
        }
    }
    return -1;
}

static void test_no_spaces_are_emitted(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n, i;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);

    for (i = 0; i < n; i++) {
        if (items[i].kind == MENU_ITEM_GLYPH && items[i].id == ' ') {
            g_fail++;
            printf("FAIL a space was emitted as a glyph at index %d\n", i);
            return;
        }
    }
    expect_int("the sub-title menu has no panel",
               count_kind(items, n, MENU_ITEM_PANEL), 0);
    expect_int("START GAME and LOAD GAME are 18 non-space glyphs",
               count_kind(items, n, MENU_ITEM_GLYPH), 18);
}

static void test_title_selected_row_uses_the_bright_ramp(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);
    st.cursor = 0;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("START GAME is lit when the cursor is on it",
               ramp_of(items, n, 90, 152), MENU_RAMP_SEL);
    expect_int("LOAD GAME is dim", ramp_of(items, n, 90, 168), MENU_RAMP_DIM);

    st.cursor = 1;
    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("START GAME is dim when the cursor moves off it",
               ramp_of(items, n, 90, 152), MENU_RAMP_DIM);
    expect_int("LOAD GAME is lit", ramp_of(items, n, 90, 168), MENU_RAMP_SEL);
}

static void test_slot_rows(void)
{
    char row[MENU_ROW_CHARS];
    SlotInfo info;

    memset(&info, 0, sizeof(info));
    info.state = SLOT_EMPTY;
    menu_layout_slot_row(row, (int)sizeof(row), 0, &info);
    expect_str("an empty slot", row, "SLOT 1  - EMPTY -");

    info.state = SLOT_DAMAGED;
    menu_layout_slot_row(row, (int)sizeof(row), 1, &info);
    expect_str("a damaged slot", row, "SLOT 2  - DAMAGED -");

    info.state = SLOT_OLD_VERSION;
    menu_layout_slot_row(row, (int)sizeof(row), 2, &info);
    expect_str("an old save", row, "SLOT 3  - OLD SAVE -");

    info.state = SLOT_OK;
    info.roomId = 12;
    info.date = 0u;
    menu_layout_slot_row(row, (int)sizeof(row), 0, &info);
    expect_str("a good slot at the BUP epoch", row,
               "SLOT 1  ROOM 12  01/01 00:00");
}

static void test_a_full_row_fits_the_panel(void)
{
    char row[MENU_ROW_CHARS];
    SlotInfo info;
    int len;

    memset(&info, 0, sizeof(info));
    info.state = SLOT_OK;
    info.roomId = 255;
    info.date = 0xFFFFFFFFu;
    menu_layout_slot_row(row, (int)sizeof(row), 2, &info);
    len = (int)strlen(row);
    if (len > 30) {
        g_fail++;
        printf("FAIL the widest slot row is %d chars, over the 30 that fit\n",
               len);
    }
}

static void test_build_never_exceeds_cap(void)
{
    MenuState st;
    MenuItem items[8];
    int n, i;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    for (i = 0; i < SAVE_NUM_SLOTS; i++) {
        st.slots[i].state = SLOT_OK;
        st.slots[i].roomId = 99;
    }
    n = menu_layout_build(&st, "SAVE DATA DAMAGED", items, 8);
    if (n > 8) {
        g_fail++;
        printf("FAIL build returned %d items for a cap of 8\n", n);
    }
}

static void test_slot_list_fits_the_command_list(void)
{
    MenuState st;
    MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n, i;

    memset(&st, 0, sizeof(st));
    menu_state_enter_slots(&st, 0, MENU_TITLE);
    st.cartPresent = 1;
    st.device = SAT_BUP_CART;
    for (i = 0; i < SAVE_NUM_SLOTS; i++) {
        st.slots[i].state = SLOT_OK;
        st.slots[i].roomId = 255;
        st.slots[i].date = 0xFFFFFFFFu;
    }
    n = menu_layout_build(&st, "CARTRIDGE WRITE PROTECTED", items,
                          MENU_LAYOUT_MAX_ITEMS);
    if (n >= MENU_LAYOUT_MAX_ITEMS) {
        g_fail++;
        printf("FAIL the widest slot list produced %d items, hitting the cap\n",
               n);
    }
    expect_int("the slot list draws one panel",
               count_kind(items, n, MENU_ITEM_PANEL), 1);
    expect_int("that panel is the slot panel", (int)items[0].id,
               MENU_PANEL_SLOTS);
}

static void test_status_text(void)
{
    expect_int("success has nothing to report",
               menu_layout_status_text(SAT_BUP_OK, SAT_BUP_INTERNAL) == 0, 1);
    expect_str("an unformatted cart is worded for the cart",
               menu_layout_status_text(SAT_BUP_ERR_UNFORMAT, SAT_BUP_CART),
               "CARTRIDGE UNFORMATTED");
    expect_str("an unformatted internal is worded for internal",
               menu_layout_status_text(SAT_BUP_ERR_UNFORMAT, SAT_BUP_INTERNAL),
               "BACKUP RAM UNFORMATTED");
    expect_str("an oversized state is not a space problem",
               menu_layout_status_text(SAVE_ERR_TOO_LARGE, SAT_BUP_INTERNAL),
               "SAVE STATE TOO LARGE");
    expect_str("anything unrecognised still says something",
               menu_layout_status_text(9999, SAT_BUP_INTERNAL), "SAVE FAILED");
}

int main(void)
{
    test_no_spaces_are_emitted();
    test_title_selected_row_uses_the_bright_ramp();
    test_slot_rows();
    test_a_full_row_fits_the_panel();
    test_build_never_exceeds_cap();
    test_slot_list_fits_the_command_list();
    test_status_text();

    if (g_fail != 0) {
        printf("menu_layout: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("menu_layout: all tests passed\n");
    return 0;
}
```

- [ ] **Step 3: Add the suite to `run_tests.sh`**

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system -I../src/menus \
    -o run_tests_menulayout test_menu_layout.c stub_saturn_backup.c \
       ../src/menus/menu_layout.c ../src/menus/menu_state.c ../src/savedata.c
./run_tests_menulayout
```

`savedata.c` is linked for `savedata_date_split`; `stub_saturn_backup.c` satisfies the `sat_bup_*` symbols `savedata.c` references but this suite never calls.

- [ ] **Step 4: Run and watch it fail**

Expected: `menu_layout.c: No such file or directory`.

- [ ] **Step 5: Write the implementation**

Create `saturn/src/menus/menu_layout.c`. Layout constants first, each with a banner; then the string helpers; then the four per-screen builders.

```c
#include "menu_layout.h"
#include "savegame.h"

#define MENU_GLYPH_W 8

#define MENU_TITLE_TEXT_X   90
#define MENU_TITLE_START_Y  152
#define MENU_TITLE_LOAD_Y   168
#define MENU_TITLE_CURSOR_X 74

#define MENU_PAUSE_PANEL_X 76
#define MENU_PAUSE_PANEL_Y 64
#define MENU_PAUSE_TEXT_X  100
#define MENU_PAUSE_ROW0_Y  76
#define MENU_PAUSE_ROW_DY  16
#define MENU_PAUSE_CURSOR_X 84

#define MENU_SLOTS_PANEL_X  24
#define MENU_SLOTS_PANEL_Y  28
#define MENU_SLOTS_HEAD_X   128
#define MENU_SLOTS_HEAD_Y   40
#define MENU_SLOTS_DEVICE_X 56
#define MENU_SLOTS_DEVICE_Y 60
#define MENU_SLOTS_ROW_X    64
#define MENU_SLOTS_ROW0_Y   84
#define MENU_SLOTS_ROW_DY   16
#define MENU_SLOTS_CURSOR_X 48
#define MENU_SLOTS_STATUS_X 48
#define MENU_SLOTS_STATUS_Y 148
#define MENU_SLOTS_FOOT_X   48
#define MENU_SLOTS_FOOT_Y   172

#define MENU_CONFIRM_PANEL_X 40
#define MENU_CONFIRM_PANEL_Y 76
#define MENU_CONFIRM_TEXT_X  64
#define MENU_CONFIRM_LINE0_Y 90
#define MENU_CONFIRM_LINE1_Y 106
#define MENU_CONFIRM_YES_X   136
#define MENU_CONFIRM_NO_X    184
#define MENU_CONFIRM_ANSWER_Y 128
#define MENU_CONFIRM_YES_CURSOR_X 120
#define MENU_CONFIRM_NO_CURSOR_X  168

static void put_panel(MenuItem *out, int cap, int *n, int id, int x, int y)
{
    if (*n >= cap) {
        return;
    }
    out[*n].kind = MENU_ITEM_PANEL;
    out[*n].id = (unsigned char)id;
    out[*n].x = (short)x;
    out[*n].y = (short)y;
    out[*n].ramp = MENU_RAMP_DIM;
    (*n)++;
}

static void put_text(MenuItem *out, int cap, int *n, int x, int y,
                     const char *s, int ramp)
{
    while (*s != 0) {
        if (*n >= cap) {
            return;
        }
        if (*s != ' ' && (unsigned char)*s >= 0x20u
            && (unsigned char)*s <= 0x5Fu) {
            out[*n].kind = MENU_ITEM_GLYPH;
            out[*n].id = (unsigned char)*s;
            out[*n].x = (short)x;
            out[*n].y = (short)y;
            out[*n].ramp = (unsigned char)ramp;
            (*n)++;
        }
        x += MENU_GLYPH_W;
        s++;
    }
}

static void append_char(char *dst, int cap, int *pos, char c)
{
    if (*pos + 1 >= cap) {
        return;
    }
    dst[*pos] = c;
    (*pos)++;
    dst[*pos] = 0;
}

static void append_str(char *dst, int cap, int *pos, const char *s)
{
    while (*s != 0) {
        append_char(dst, cap, pos, *s);
        s++;
    }
}

static void append_pad2(char *dst, int cap, int *pos, int v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > 99) {
        v = 99;
    }
    append_char(dst, cap, pos, (char)('0' + (v / 10)));
    append_char(dst, cap, pos, (char)('0' + (v % 10)));
}

static void append_room(char *dst, int cap, int *pos, unsigned short room)
{
    if (room > 999u) {
        room = 999u;
    }
    if (room >= 100u) {
        append_char(dst, cap, pos, (char)('0' + (room / 100u)));
    }
    if (room >= 10u) {
        append_char(dst, cap, pos, (char)('0' + ((room / 10u) % 10u)));
    }
    append_char(dst, cap, pos, (char)('0' + (room % 10u)));
}

void menu_layout_slot_row(char *out, int cap, int slot, const SlotInfo *info)
{
    int pos = 0;
    int month = 0, day = 0, hour = 0, minute = 0;

    out[0] = 0;
    append_str(out, cap, &pos, "SLOT ");
    append_char(out, cap, &pos, (char)('1' + slot));
    append_str(out, cap, &pos, "  ");

    if (info->state == SLOT_EMPTY) {
        append_str(out, cap, &pos, "- EMPTY -");
        return;
    }
    if (info->state == SLOT_DAMAGED) {
        append_str(out, cap, &pos, "- DAMAGED -");
        return;
    }
    if (info->state == SLOT_OLD_VERSION) {
        append_str(out, cap, &pos, "- OLD SAVE -");
        return;
    }

    append_str(out, cap, &pos, "ROOM ");
    append_room(out, cap, &pos, info->roomId);
    append_str(out, cap, &pos, "  ");

    savedata_date_split(info->date, &month, &day, &hour, &minute);
    append_pad2(out, cap, &pos, month);
    append_char(out, cap, &pos, '/');
    append_pad2(out, cap, &pos, day);
    append_char(out, cap, &pos, ' ');
    append_pad2(out, cap, &pos, hour);
    append_char(out, cap, &pos, ':');
    append_pad2(out, cap, &pos, minute);
}

const char *menu_layout_status_text(int err, unsigned long device)
{
    switch (err) {
    case SAT_BUP_OK:            return 0;
    case SAVE_ERR_TOO_LARGE:    return "SAVE STATE TOO LARGE";
    case SAT_BUP_ERR_NONE:      return "NO BACKUP DEVICE";
    case SAT_BUP_ERR_UNFORMAT:  return (device == SAT_BUP_CART)
                                       ? "CARTRIDGE UNFORMATTED"
                                       : "BACKUP RAM UNFORMATTED";
    case SAT_BUP_ERR_PROTECTED: return "CARTRIDGE WRITE PROTECTED";
    case SAT_BUP_ERR_NO_SPACE:  return "NOT ENOUGH SPACE";
    case SAT_BUP_ERR_NOT_FOUND: return "SAVE NOT FOUND";
    case SAT_BUP_ERR_EXISTS:    return "SLOT ALREADY IN USE";
    case SAT_BUP_ERR_BROKEN:    return "SAVE DATA DAMAGED";
    default:                    return "SAVE FAILED";
    }
}

static void build_title(const MenuState *st, MenuItem *out, int cap, int *n)
{
    put_text(out, cap, n, MENU_TITLE_TEXT_X, MENU_TITLE_START_Y, "START GAME",
             st->cursor == 0 ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    put_text(out, cap, n, MENU_TITLE_TEXT_X, MENU_TITLE_LOAD_Y, "LOAD GAME",
             st->cursor == 1 ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    put_text(out, cap, n, MENU_TITLE_CURSOR_X,
             st->cursor == 0 ? MENU_TITLE_START_Y : MENU_TITLE_LOAD_Y, ">",
             MENU_RAMP_SEL);
}

static void build_pause(const MenuState *st, MenuItem *out, int cap, int *n)
{
    static const char *ROWS[4] = {
        "RESUME", "SAVE GAME", "LOAD GAME", "RETURN TO TITLE"
    };
    int i;

    put_panel(out, cap, n, MENU_PANEL_PAUSE, MENU_PAUSE_PANEL_X,
              MENU_PAUSE_PANEL_Y);
    for (i = 0; i < 4; i++) {
        put_text(out, cap, n, MENU_PAUSE_TEXT_X,
                 MENU_PAUSE_ROW0_Y + i * MENU_PAUSE_ROW_DY, ROWS[i],
                 st->cursor == i ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    }
    put_text(out, cap, n, MENU_PAUSE_CURSOR_X,
             MENU_PAUSE_ROW0_Y + st->cursor * MENU_PAUSE_ROW_DY, ">",
             MENU_RAMP_SEL);
}

static void build_slots(const MenuState *st, const char *status, MenuItem *out,
                        int cap, int *n)
{
    char row[MENU_ROW_CHARS];
    int i;

    put_panel(out, cap, n, MENU_PANEL_SLOTS, MENU_SLOTS_PANEL_X,
              MENU_SLOTS_PANEL_Y);
    put_text(out, cap, n, MENU_SLOTS_HEAD_X, MENU_SLOTS_HEAD_Y,
             st->saving ? "SAVE GAME" : "LOAD GAME", MENU_RAMP_DIM);
    put_text(out, cap, n, MENU_SLOTS_DEVICE_X, MENU_SLOTS_DEVICE_Y,
             st->device == SAT_BUP_CART ? "< CARTRIDGE >"
                                        : "< INTERNAL MEMORY >",
             st->cartPresent ? MENU_RAMP_SEL : MENU_RAMP_DIM);

    for (i = 0; i < SAVE_NUM_SLOTS; i++) {
        menu_layout_slot_row(row, MENU_ROW_CHARS, i, &st->slots[i]);
        put_text(out, cap, n, MENU_SLOTS_ROW_X,
                 MENU_SLOTS_ROW0_Y + i * MENU_SLOTS_ROW_DY, row,
                 st->slotCursor == i ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    }
    put_text(out, cap, n, MENU_SLOTS_CURSOR_X,
             MENU_SLOTS_ROW0_Y + st->slotCursor * MENU_SLOTS_ROW_DY, ">",
             MENU_RAMP_SEL);

    if (status != 0) {
        put_text(out, cap, n, MENU_SLOTS_STATUS_X, MENU_SLOTS_STATUS_Y, status,
                 MENU_RAMP_DIM);
    }
    put_text(out, cap, n, MENU_SLOTS_FOOT_X, MENU_SLOTS_FOOT_Y,
             "A SELECT   B BACK", MENU_RAMP_DIM);
}

static void build_confirm(const MenuState *st, MenuItem *out, int cap, int *n)
{
    char row[MENU_ROW_CHARS];
    int pos = 0;

    put_panel(out, cap, n, MENU_PANEL_CONFIRM, MENU_CONFIRM_PANEL_X,
              MENU_CONFIRM_PANEL_Y);

    if (st->pending == MENU_ACT_RETURN_TO_TITLE) {
        put_text(out, cap, n, MENU_CONFIRM_TEXT_X, MENU_CONFIRM_LINE0_Y,
                 "RETURN TO TITLE ?", MENU_RAMP_DIM);
        put_text(out, cap, n, MENU_CONFIRM_TEXT_X, MENU_CONFIRM_LINE1_Y,
                 "PROGRESS WILL BE LOST", MENU_RAMP_DIM);
    } else {
        row[0] = 0;
        append_str(row, MENU_ROW_CHARS, &pos, "OVERWRITE SLOT ");
        append_char(row, MENU_ROW_CHARS, &pos, (char)('1' + st->slotCursor));
        append_str(row, MENU_ROW_CHARS, &pos, " ?");
        put_text(out, cap, n, MENU_CONFIRM_TEXT_X, MENU_CONFIRM_LINE0_Y, row,
                 MENU_RAMP_DIM);
    }

    put_text(out, cap, n, MENU_CONFIRM_YES_X, MENU_CONFIRM_ANSWER_Y, "YES",
             st->confirmYes ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    put_text(out, cap, n, MENU_CONFIRM_NO_X, MENU_CONFIRM_ANSWER_Y, "NO",
             st->confirmYes ? MENU_RAMP_DIM : MENU_RAMP_SEL);
    put_text(out, cap, n,
             st->confirmYes ? MENU_CONFIRM_YES_CURSOR_X
                            : MENU_CONFIRM_NO_CURSOR_X,
             MENU_CONFIRM_ANSWER_Y, ">", MENU_RAMP_SEL);
}

int menu_layout_build(const MenuState *st, const char *status, MenuItem *out,
                      int cap)
{
    int n = 0;

    switch (st->screen) {
    case MENU_TITLE:   build_title(st, out, cap, &n); break;
    case MENU_PAUSE:   build_pause(st, out, cap, &n); break;
    case MENU_SLOTS:   build_slots(st, status, out, cap, &n); break;
    case MENU_CONFIRM: build_confirm(st, out, cap, &n); break;
    default: break;
    }
    return n;
}
```

Banners that must carry rationale:
- File banner: no `snprintf` anywhere in this file. `sprintf` would pull stdio into a translation unit that has no other need of it, which is the same reason Another-Saturn hand-rolls its appenders.
- On the geometry block: these are Another-Saturn's panels re-centred for a 320x224 frame rather than its 320x200 page. Do not copy its literals; they land 12 rows high here.
- On `put_text`: a space advances the cursor without emitting an item. That is not an optimisation for its own sake — it is what keeps the widest screen under `MENU_LAYOUT_MAX_ITEMS` and therefore under VDP1's command list.
- On `append_room`: the room id is printed without leading zeros because `ROOM 07` reads as a chapter number the game does not have.

- [ ] **Step 6: Run the tests and watch them pass**

Expected: `menu_layout: all tests passed`.

If `a good slot at the BUP epoch` fails, check `savedata_date_split`'s epoch: date 0 is 1 January 1980 00:00, so the row is `01/01 00:00`.

- [ ] **Step 7: Mutation-test**

One at a time, confirm the named test fails, restore:

1. In `put_text`, delete the `*s != ' '` condition → `a space was emitted as a glyph` must fail.
2. In `build_title`, swap `MENU_RAMP_SEL` and `MENU_RAMP_DIM` → `START GAME is lit when the cursor is on it` must fail.
3. In `put_text`, delete the `if (*n >= cap) return;` guard → `build returned N items for a cap of 8` must fail.
4. In `menu_layout_slot_row`, remove the `SLOT_OLD_VERSION` branch → `an old save` must fail.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menus/menu_layout.h saturn/src/menus/menu_layout.c \
        saturn/tests/test_menu_layout.c saturn/tests/run_tests.sh
git commit -m "Turn menu state into a list of draw items in frame pixels, with the slot rows and error strings built by hand rather than through stdio, and spaces emitting nothing so the widest screen stays inside VDP1's command list."
```

---

### Task 4: `menu_clock` — music cycle and attract timing

The same shape as `bootmenu.c`: unsigned differences over a millisecond count, decisions returned rather than performed, correct across the counter's wrap.

**Files:**
- Create: `saturn/src/menus/menu_clock.h`
- Create: `saturn/src/menus/menu_clock.c`
- Create: `saturn/tests/test_menu_clock.c`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: `discfmt.h` — `discfmt_cue_track_for_music`, for the test only
- Produces:
  - `void menu_clock_enter(menu_clock_state *st, unsigned int now_ms);`
  - `void menu_clock_step(menu_clock_state *st, unsigned int now_ms, int had_input, menu_clock_frame *out);`
  - `struct menu_clock_state { unsigned int music_start_ms; unsigned int idle_start_ms; }`
  - `struct menu_clock_frame { unsigned char music_volume; int music_restart; int launch_attract; }`
  - `MENU_MUSIC_INDEX` 2, `MENU_MUSIC_CYCLE_MS` 40000, `MENU_FADE_MS` 1000, `MENU_IDLE_MS` 15000, `MENU_VOLUME_MAX` 7

- [ ] **Step 1: Write the header**

Create `saturn/src/menus/menu_clock.h`:

```c
#ifndef MENU_CLOCK_H
#define MENU_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_MUSIC_INDEX     2
#define MENU_MUSIC_CYCLE_MS  40000u
#define MENU_FADE_MS          1000u
#define MENU_IDLE_MS         15000u
#define MENU_VOLUME_MAX          7u

typedef struct {
    unsigned int music_start_ms;
    unsigned int idle_start_ms;
} menu_clock_state;

typedef struct {
    unsigned char music_volume;
    int           music_restart;
    int           launch_attract;
} menu_clock_frame;

void menu_clock_enter(menu_clock_state *st, unsigned int now_ms);
void menu_clock_step(menu_clock_state *st, unsigned int now_ms, int had_input,
                     menu_clock_frame *out);

#ifdef __cplusplus
}
#endif

#endif /* MENU_CLOCK_H */
```

Banner notes that must appear:
- `MENU_MUSIC_INDEX`: the engine music index, which `discfmt_cue_track_for_music` maps to cue track 3 — `track03.wav`, 2:46.17. The disc numbers its 41 audio tracks 02..42, so this is the second audio track despite being disc track three. The wrong index is inaudible as a bug and merely sounds like different music, which is why the test pins the mapping.
- `MENU_VOLUME_MAX`: `SND_SetCdDaLev` takes 0..7, so a fade is an eight-step staircase, not a ramp. Lengthening `MENU_FADE_MS` adds time to notice each step, never resolution.
- `MENU_IDLE_MS`: the idle timer runs only while the sub-title screen itself is up. `menu.c` does not call this while the slot list or a confirm prompt is showing, because a player reading three slot rows is not idle.
- `menu_clock_enter`: resets both timers. The caller starts the track immediately after; the first frame's volume is therefore 0 and fades in, which is what makes re-entry from the attract loop silent at the seam instead of clicking.

- [ ] **Step 2: Write the failing tests**

Create `saturn/tests/test_menu_clock.c`:

```c
/*----------------------
 | test_menu_clock.c
 | Description: Host unit tests for menu_clock.c. discfmt.c is linked only so
 |   the music index can be checked against the cue track it resolves to.
 | Author: suinevere
 | Dependencies: menu_clock.h, discfmt.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "menu_clock.h"
#include "discfmt.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void test_music_index_resolves_to_track_three(void)
{
    expect_int("MENU_MUSIC_INDEX maps to cue track 3",
               discfmt_cue_track_for_music(MENU_MUSIC_INDEX), 3);
}

static void test_fade_in_from_entry(void)
{
    menu_clock_state st;
    menu_clock_frame f;

    menu_clock_enter(&st, 5000u);

    menu_clock_step(&st, 5000u, 0, &f);
    expect_int("entry starts silent", (int)f.music_volume, 0);
    expect_int("entry does not ask for a restart", f.music_restart, 0);

    menu_clock_step(&st, 5000u + 500u, 0, &f);
    expect_int("half a fade in is half volume", (int)f.music_volume, 3);

    menu_clock_step(&st, 5000u + 1000u, 0, &f);
    expect_int("a full fade in is full volume", (int)f.music_volume,
               (int)MENU_VOLUME_MAX);
}

static void test_fade_out_completes_at_the_cycle(void)
{
    menu_clock_state st;
    menu_clock_frame f;

    menu_clock_enter(&st, 0u);

    menu_clock_step(&st, 39000u, 1, &f);
    expect_int("still full a second before the end", (int)f.music_volume,
               (int)MENU_VOLUME_MAX);

    menu_clock_step(&st, 39500u, 1, &f);
    expect_int("half faded out", (int)f.music_volume, 3);

    menu_clock_step(&st, 40000u, 1, &f);
    expect_int("the cycle restarts the track", f.music_restart, 1);
    expect_int("and it restarts silent", (int)f.music_volume, 0);

    menu_clock_step(&st, 41000u, 1, &f);
    expect_int("then fades back in", (int)f.music_volume,
               (int)MENU_VOLUME_MAX);
    expect_int("without restarting again", f.music_restart, 0);
}

static void test_idle_launches_the_attract(void)
{
    menu_clock_state st;
    menu_clock_frame f;

    menu_clock_enter(&st, 0u);

    menu_clock_step(&st, 14999u, 0, &f);
    expect_int("not yet idle", f.launch_attract, 0);

    menu_clock_step(&st, 15000u, 0, &f);
    expect_int("fifteen seconds launches the attract", f.launch_attract, 1);
}

static void test_input_resets_the_idle_timer(void)
{
    menu_clock_state st;
    menu_clock_frame f;

    menu_clock_enter(&st, 0u);

    menu_clock_step(&st, 14000u, 1, &f);
    expect_int("input at fourteen seconds does not launch", f.launch_attract, 0);

    menu_clock_step(&st, 28000u, 0, &f);
    expect_int("fourteen more seconds still does not launch",
               f.launch_attract, 0);

    menu_clock_step(&st, 29000u, 0, &f);
    expect_int("fifteen after the input does", f.launch_attract, 1);
}

static void test_correct_across_the_counter_wrap(void)
{
    menu_clock_state st;
    menu_clock_frame f;
    unsigned int base = 0xFFFFF000u;

    menu_clock_enter(&st, base);

    menu_clock_step(&st, base + 500u, 0, &f);
    expect_int("fade in survives the wrap", (int)f.music_volume, 3);

    menu_clock_step(&st, base + 15000u, 0, &f);
    expect_int("the idle trigger survives the wrap", f.launch_attract, 1);

    menu_clock_step(&st, base + 40000u, 1, &f);
    expect_int("the music cycle survives the wrap", f.music_restart, 1);
}

int main(void)
{
    test_music_index_resolves_to_track_three();
    test_fade_in_from_entry();
    test_fade_out_completes_at_the_cycle();
    test_idle_launches_the_attract();
    test_input_resets_the_idle_timer();
    test_correct_across_the_counter_wrap();

    if (g_fail != 0) {
        printf("menu_clock: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("menu_clock: all tests passed\n");
    return 0;
}
```

- [ ] **Step 3: Add the suite to `run_tests.sh`**

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/menus \
    -o run_tests_menuclock test_menu_clock.c ../src/menus/menu_clock.c \
       ../src/discfmt.c
./run_tests_menuclock
```

- [ ] **Step 4: Run and watch it fail**

Expected: `menu_clock.c: No such file or directory`.

- [ ] **Step 5: Write the implementation**

Create `saturn/src/menus/menu_clock.c`:

```c
#include "menu_clock.h"

static unsigned char menu_ramp(unsigned int elapsed)
{
    unsigned int remaining;

    if (elapsed < MENU_FADE_MS) {
        return (unsigned char)((elapsed * MENU_VOLUME_MAX) / MENU_FADE_MS);
    }
    if (elapsed >= MENU_MUSIC_CYCLE_MS) {
        return 0u;
    }
    remaining = MENU_MUSIC_CYCLE_MS - elapsed;
    if (remaining >= MENU_FADE_MS) {
        return (unsigned char)MENU_VOLUME_MAX;
    }
    return (unsigned char)((remaining * MENU_VOLUME_MAX) / MENU_FADE_MS);
}

void menu_clock_enter(menu_clock_state *st, unsigned int now_ms)
{
    st->music_start_ms = now_ms;
    st->idle_start_ms = now_ms;
}

void menu_clock_step(menu_clock_state *st, unsigned int now_ms, int had_input,
                     menu_clock_frame *out)
{
    unsigned int music_ms;

    out->music_restart = 0;
    out->launch_attract = 0;

    if (had_input) {
        st->idle_start_ms = now_ms;
    }

    music_ms = now_ms - st->music_start_ms;
    if (music_ms >= MENU_MUSIC_CYCLE_MS) {
        st->music_start_ms = now_ms;
        music_ms = 0u;
        out->music_restart = 1;
    }

    if (now_ms - st->idle_start_ms >= MENU_IDLE_MS) {
        out->launch_attract = 1;
    }

    out->music_volume = menu_ramp(music_ms);
}
```

`menu_ramp`'s banner must say why the restart and the silence coincide: the cycle boundary resets `music_start_ms`, so the frame that issues `music_restart` reports volume 0 and the track comes up through the fade in rather than snapping to 7. That is the fix for the boot menu's known seam click, where the attract restart raised volume 0→7 in the same frame as `disc_play_track`.

- [ ] **Step 6: Run the tests and watch them pass**

Expected: `menu_clock: all tests passed`.

- [ ] **Step 7: Mutation-test**

One at a time, confirm the named test fails, restore:

1. In `menu_clock_step`, delete `if (had_input) { st->idle_start_ms = now_ms; }` → `fourteen more seconds still does not launch` must fail.
2. In `menu_ramp`, delete the fade-in branch and return `MENU_VOLUME_MAX` for `elapsed < MENU_FADE_MS` → `entry starts silent` must fail.
3. In `menu_clock_step`, delete the `music_ms = 0u;` line after a restart → `and it restarts silent` must fail.
4. Change `MENU_MUSIC_INDEX` to 3 → `MENU_MUSIC_INDEX maps to cue track 3` must fail.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menus/menu_clock.h saturn/src/menus/menu_clock.c \
        saturn/tests/test_menu_clock.c saturn/tests/run_tests.sh
git commit -m "Add the sub-title screen's timing: track 03 from zero with a fade in, a fade out completing at forty seconds before it restarts silent, and fifteen seconds without input launching the attract cinematic."
```

---

### Task 5: The asset tool and the four `.ART` files

Builds the font and the three panels, with every colour derived from `HEART OF THE ALIEN003.tga` so a re-grab re-keys the menus rather than leaving them keyed to a palette that no longer exists.

The `.ART` format is `mkbootart.py`'s: big-endian header of magic `0x4241`, width, height, palette count, then that many `HighColor` entries of two bytes, then 4bpp pixels packed high-nibble-left.

Palette layout, shared by all four files so they can share one CRAM bank:

| Index | Use |
|---|---|
| 0 | Transparent. VDP1 treats palette index 0 in a `Paletted16` sprite as transparent, which is what lets a glyph sit over the title card with no box around it |
| 1 | Dim text |
| 2 | Panel fill |
| 3 | Panel border |
| 4 | Selected text — written into the palette but used by no pixel, so `saturn_menuart.cxx` can build the bright bank by copying the dim one and replacing entry 1 with this |
| 5–15 | Black padding |

**Files:**
- Create: `tools/font8x8.py`
- Create: `tools/mkmenuart.py`
- Create: `saturn/cd/data/MENUFONT.ART`, `MENUPANP.ART`, `MENUPANS.ART`, `MENUPANC.ART`

**Interfaces:**
- Consumes: `tools/mkbootart.py` — `read_tga`, `active`, `high_color`, `reduce_palette`; `tools/assets/TGA/HEART OF THE ALIEN003.tga`
- Produces: four `.ART` files, and the constant geometry `saturn_menuart.cxx` hardcodes — font 8x512, pause panel 168x96, slot panel 272x168, confirm panel 240x72, palette count 16, glyph stride 32 bytes, pixel data starting at byte 40

- [ ] **Step 1: Create the font table**

Create `tools/font8x8.py`. It is data, so the file banner is the only banner it needs.

```python
"""
font8x8.py
Description: The menu font: ASCII 0x20-0x5F as 8x8 cells, one byte per row,
  bit 7 leftmost. Glyphs are 5 pixels wide in the top 5 bits with row 8 blank,
  which gives 3 pixels of letter spacing and 1 of leading at an 8-pixel cell
  pitch. Uppercase only, because every string in these menus is uppercase and
  a lowercase range would cost 32 more VDP1 texture slots out of 128.
  Kept as a literal rather than rendered from a system font so the asset is
  reproducible on any machine.
Author: suinevere
"""

FONT8X8_FIRST = 0x20
FONT8X8_LAST = 0x5F

FONT8X8 = (
    (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),  # 0x20 ' '
    (0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x20, 0x00),  # 0x21 '!'
    (0x50, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),  # 0x22 '"'
    (0x50, 0xF8, 0x50, 0xF8, 0x50, 0x00, 0x00, 0x00),  # 0x23 '#'
    (0x20, 0x78, 0xA0, 0x70, 0x28, 0xF0, 0x20, 0x00),  # 0x24 '$'
    (0xC8, 0xC8, 0x10, 0x20, 0x40, 0x98, 0x98, 0x00),  # 0x25 '%'
    (0x60, 0x90, 0x60, 0xA8, 0x90, 0x90, 0x68, 0x00),  # 0x26 '&'
    (0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),  # 0x27 "'"
    (0x10, 0x20, 0x40, 0x40, 0x40, 0x20, 0x10, 0x00),  # 0x28 '('
    (0x40, 0x20, 0x10, 0x10, 0x10, 0x20, 0x40, 0x00),  # 0x29 ')'
    (0x00, 0xA8, 0x70, 0xF8, 0x70, 0xA8, 0x00, 0x00),  # 0x2A '*'
    (0x00, 0x20, 0x20, 0xF8, 0x20, 0x20, 0x00, 0x00),  # 0x2B '+'
    (0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x20, 0x00),  # 0x2C ','
    (0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00),  # 0x2D '-'
    (0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00),  # 0x2E '.'
    (0x08, 0x10, 0x10, 0x20, 0x40, 0x40, 0x80, 0x00),  # 0x2F '/'
    (0x70, 0x88, 0x98, 0xA8, 0xC8, 0x88, 0x70, 0x00),  # 0x30 '0'
    (0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70, 0x00),  # 0x31 '1'
    (0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xF8, 0x00),  # 0x32 '2'
    (0xF0, 0x08, 0x08, 0x70, 0x08, 0x08, 0xF0, 0x00),  # 0x33 '3'
    (0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10, 0x00),  # 0x34 '4'
    (0xF8, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70, 0x00),  # 0x35 '5'
    (0x70, 0x80, 0x80, 0xF0, 0x88, 0x88, 0x70, 0x00),  # 0x36 '6'
    (0xF8, 0x08, 0x10, 0x20, 0x20, 0x40, 0x40, 0x00),  # 0x37 '7'
    (0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70, 0x00),  # 0x38 '8'
    (0x70, 0x88, 0x88, 0x78, 0x08, 0x08, 0x70, 0x00),  # 0x39 '9'
    (0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x00, 0x00),  # 0x3A ':'
    (0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x20, 0x00),  # 0x3B ';'
    (0x10, 0x20, 0x40, 0x80, 0x40, 0x20, 0x10, 0x00),  # 0x3C '<'
    (0x00, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00, 0x00),  # 0x3D '='
    (0x40, 0x20, 0x10, 0x08, 0x10, 0x20, 0x40, 0x00),  # 0x3E '>'
    (0x70, 0x88, 0x08, 0x10, 0x20, 0x00, 0x20, 0x00),  # 0x3F '?'
    (0x70, 0x88, 0xB8, 0xA8, 0xB8, 0x80, 0x70, 0x00),  # 0x40 '@'
    (0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88, 0x00),  # 0x41 'A'
    (0xF0, 0x88, 0x88, 0xF0, 0x88, 0x88, 0xF0, 0x00),  # 0x42 'B'
    (0x70, 0x88, 0x80, 0x80, 0x80, 0x88, 0x70, 0x00),  # 0x43 'C'
    (0xE0, 0x90, 0x88, 0x88, 0x88, 0x90, 0xE0, 0x00),  # 0x44 'D'
    (0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0xF8, 0x00),  # 0x45 'E'
    (0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0x80, 0x00),  # 0x46 'F'
    (0x70, 0x88, 0x80, 0xB8, 0x88, 0x88, 0x78, 0x00),  # 0x47 'G'
    (0x88, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88, 0x00),  # 0x48 'H'
    (0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70, 0x00),  # 0x49 'I'
    (0x18, 0x08, 0x08, 0x08, 0x88, 0x88, 0x70, 0x00),  # 0x4A 'J'
    (0x88, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x88, 0x00),  # 0x4B 'K'
    (0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xF8, 0x00),  # 0x4C 'L'
    (0x88, 0xD8, 0xA8, 0xA8, 0x88, 0x88, 0x88, 0x00),  # 0x4D 'M'
    (0x88, 0xC8, 0xA8, 0xA8, 0x98, 0x88, 0x88, 0x00),  # 0x4E 'N'
    (0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, 0x00),  # 0x4F 'O'
    (0xF0, 0x88, 0x88, 0xF0, 0x80, 0x80, 0x80, 0x00),  # 0x50 'P'
    (0x70, 0x88, 0x88, 0x88, 0xA8, 0x90, 0x68, 0x00),  # 0x51 'Q'
    (0xF0, 0x88, 0x88, 0xF0, 0xA0, 0x90, 0x88, 0x00),  # 0x52 'R'
    (0x78, 0x80, 0x80, 0x70, 0x08, 0x08, 0xF0, 0x00),  # 0x53 'S'
    (0xF8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00),  # 0x54 'T'
    (0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, 0x00),  # 0x55 'U'
    (0x88, 0x88, 0x88, 0x88, 0x88, 0x50, 0x20, 0x00),  # 0x56 'V'
    (0x88, 0x88, 0x88, 0xA8, 0xA8, 0xD8, 0x88, 0x00),  # 0x57 'W'
    (0x88, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88, 0x00),  # 0x58 'X'
    (0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0x20, 0x00),  # 0x59 'Y'
    (0xF8, 0x08, 0x10, 0x20, 0x40, 0x80, 0xF8, 0x00),  # 0x5A 'Z'
    (0x70, 0x40, 0x40, 0x40, 0x40, 0x40, 0x70, 0x00),  # 0x5B '['
    (0x80, 0x40, 0x40, 0x20, 0x10, 0x10, 0x08, 0x00),  # 0x5C backslash
    (0x70, 0x10, 0x10, 0x10, 0x10, 0x10, 0x70, 0x00),  # 0x5D ']'
    (0x20, 0x50, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00),  # 0x5E '^'
    (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x00),  # 0x5F '_'
)

assert len(FONT8X8) == FONT8X8_LAST - FONT8X8_FIRST + 1
```

- [ ] **Step 2: Write `mkmenuart.py`**

Create `tools/mkmenuart.py`. Every function gets a banner in the `mkbootart.py` comment style.

```python
"""
mkmenuart.py
Description: Builds the menu's four .ART files. The font is a fixed glyph
  table; the panels are solid rectangles. Everything colour comes out of
  HEART OF THE ALIEN003.tga, the same grab BOOTTITL.ART is cut from, so a
  re-grab re-keys the menus instead of leaving them keyed to a palette that
  no longer exists.
  Run from anywhere; paths resolve against the repository.
Author: suinevere
Usage: python tools/mkmenuart.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkbootart import read_tga, active, high_color, reduce_palette, SRC_W, ACTIVE_H, TGA_DIR, OUT_DIR
from font8x8 import FONT8X8, FONT8X8_FIRST, FONT8X8_LAST

TITLE_SOURCE = "HEART OF THE ALIEN003.tga"

GLYPH_W = 8
GLYPH_H = 8
GLYPH_COUNT = FONT8X8_LAST - FONT8X8_FIRST + 1

PANELS = [
    ("MENUPANP.ART", 168, 96),
    ("MENUPANS.ART", 272, 168),
    ("MENUPANC.ART", 240, 72),
]

PANEL_BORDER_PX = 2

IDX_TRANSPARENT = 0
IDX_TEXT_DIM = 1
IDX_PANEL_FILL = 2
IDX_PANEL_BORDER = 3
IDX_TEXT_SEL = 4
PALETTE_ENTRIES = 16


def luminance(colour):
    return (colour[0] * 30 + colour[1] * 59 + colour[2] * 11) // 100


def title_ramp():
    """Four colours pulled off the title card, darkest to brightest."""
    image = active(read_tga(os.path.join(TGA_DIR, TITLE_SOURCE)))
    lookup = {}
    palette = []
    counts = []
    for i in range(SRC_W * ACTIVE_H):
        colour = image[i * 3:i * 3 + 3]
        if colour not in lookup:
            lookup[colour] = len(palette)
            palette.append(colour)
            counts.append(0)
        counts[lookup[colour]] += 1
    if len(palette) > 16:
        palette, _ = reduce_palette(palette, counts, 16)

    blues = [c for c in palette if c[2] >= c[0] and c[2] >= c[1]]
    if len(blues) < 4:
        blues = list(palette)
    blues.sort(key=luminance)

    fill = blues[0]
    border = blues[len(blues) // 3]
    dim = blues[(2 * len(blues)) // 3]
    sel = blues[-1]
    return fill, border, dim, sel


def build_palette(fill, border, dim, sel):
    entries = [(0, 0, 0)] * PALETTE_ENTRIES
    entries[IDX_TEXT_DIM] = dim
    entries[IDX_PANEL_FILL] = fill
    entries[IDX_PANEL_BORDER] = border
    entries[IDX_TEXT_SEL] = sel
    return entries


def write_art_indexed(path, indices, w, h, palette):
    header = bytearray()
    header += (0x4241).to_bytes(2, "big")
    header += w.to_bytes(2, "big")
    header += h.to_bytes(2, "big")
    header += len(palette).to_bytes(2, "big")
    for colour in palette:
        header += high_color(colour).to_bytes(2, "big")

    packed = bytearray((w * h) // 2)
    for i in range(0, w * h, 2):
        packed[i // 2] = ((indices[i] & 0x0F) << 4) | (indices[i + 1] & 0x0F)

    with open(path, "wb") as handle:
        handle.write(header)
        handle.write(packed)


def font_indices():
    """Every glyph stacked into one 8-wide column, glyph i at rows i*8..i*8+7."""
    indices = bytearray(GLYPH_W * GLYPH_H * GLYPH_COUNT)
    for g in range(GLYPH_COUNT):
        for row in range(GLYPH_H):
            bits = FONT8X8[g][row]
            for col in range(GLYPH_W):
                if bits & (0x80 >> col):
                    indices[(g * GLYPH_H + row) * GLYPH_W + col] = IDX_TEXT_DIM
    return indices


def panel_indices(w, h):
    indices = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            edge = (x < PANEL_BORDER_PX or x >= w - PANEL_BORDER_PX
                    or y < PANEL_BORDER_PX or y >= h - PANEL_BORDER_PX)
            indices[y * w + x] = IDX_PANEL_BORDER if edge else IDX_PANEL_FILL
    return indices


def verify(expected, palette):
    for name, w, h in expected:
        path = os.path.join(OUT_DIR, name)
        data = open(path, "rb").read()
        if int.from_bytes(data[0:2], "big") != 0x4241:
            sys.exit("mkmenuart: %s has the wrong magic" % name)
        if int.from_bytes(data[2:4], "big") != w:
            sys.exit("mkmenuart: %s width is not %d" % (name, w))
        if int.from_bytes(data[4:6], "big") != h:
            sys.exit("mkmenuart: %s height is not %d" % (name, h))
        count = int.from_bytes(data[6:8], "big")
        if count != PALETTE_ENTRIES:
            sys.exit("mkmenuart: %s carries %d palette entries, not %d"
                     % (name, count, PALETTE_ENTRIES))
        if w % 8 != 0:
            sys.exit("mkmenuart: %s is %d wide; VDP1 stores width in 8-dot "
                     "units and a remainder shears every line" % (name, w))
        if h % 2 != 0:
            sys.exit("mkmenuart: %s is %d tall; 4bpp rows pack two pixels a "
                     "byte" % (name, h))
        want = 8 + 2 * count + (w * h) // 2
        if len(data) != want:
            sys.exit("mkmenuart: %s is %d bytes, header claims %d"
                     % (name, len(data), want))
        print("  %-14s %4dx%-4d %6d bytes" % (name, w, h, len(data)))

    fill = palette[IDX_PANEL_FILL]
    border = palette[IDX_PANEL_BORDER]
    dim = palette[IDX_TEXT_DIM]
    sel = palette[IDX_TEXT_SEL]
    if not (luminance(sel) > luminance(dim) > luminance(border) >= luminance(fill)):
        sys.exit("mkmenuart: the title card's ramp has collapsed -- fill %d, "
                 "border %d, dim %d, selected %d must be strictly increasing"
                 % (luminance(fill), luminance(border), luminance(dim),
                    luminance(sel)))
    if luminance(dim) - luminance(fill) < 40:
        sys.exit("mkmenuart: dim text is only %d brighter than the panel fill; "
                 "under 40 it is unreadable on a CRT"
                 % (luminance(dim) - luminance(fill)))
    print("  ramp: fill %d, border %d, dim %d, selected %d"
          % (luminance(fill), luminance(border), luminance(dim),
             luminance(sel)))


def main():
    fill, border, dim, sel = title_ramp()
    palette = build_palette(fill, border, dim, sel)
    os.makedirs(OUT_DIR, exist_ok=True)

    write_art_indexed(os.path.join(OUT_DIR, "MENUFONT.ART"), font_indices(),
                      GLYPH_W, GLYPH_H * GLYPH_COUNT, palette)
    for name, w, h in PANELS:
        write_art_indexed(os.path.join(OUT_DIR, name), panel_indices(w, h),
                          w, h, palette)

    print("verify:")
    verify([("MENUFONT.ART", GLYPH_W, GLYPH_H * GLYPH_COUNT)] + PANELS, palette)
    print("glyph stride %d bytes, pixel data starts at byte %d"
          % (GLYPH_W * GLYPH_H // 2, 8 + 2 * PALETTE_ENTRIES))


if __name__ == "__main__":
    main()
```

Banner notes that must appear:
- On `verify`'s width check: nothing in SRL or SGL rejects a bad width; it only ever shows up on screen, sheared two pixels further right on every line.
- On `IDX_TEXT_SEL`: it is written into the palette but no pixel uses it. `saturn_menuart.cxx` builds the bright bank by copying the dim one and replacing entry 1 with this, so the selected colour stays art-derived rather than invented at runtime.
- On `title_ramp`'s blue filter: the fallback to the whole palette exists because a re-grab could land on a frame that is not blue-dominant, and failing to a readable grey ramp beats failing to a crash.

- [ ] **Step 3: Run the tool**

```
python tools/mkmenuart.py
```

Expected output shape:

```
verify:
  MENUFONT.ART      8x512     2088 bytes
  MENUPANP.ART    168x96      8104 bytes
  MENUPANS.ART    272x168    22888 bytes
  MENUPANC.ART    240x72      8680 bytes
  ramp: fill N, border N, dim N, selected N
glyph stride 32 bytes, pixel data starts at byte 40
```

If `the title card's ramp has collapsed` fires, the title card's palette does not carry four separable blue tones — report it rather than loosening the check, because the whole point is that unreadable text fails the build.

- [ ] **Step 4: Eyeball the font**

Run this one-liner and read the output. It renders the table the tool ships, not a re-derivation, so a typo in `font8x8.py` shows up as a broken letter:

```
python -c "import sys; sys.path.insert(0,'tools'); from font8x8 import FONT8X8, FONT8X8_FIRST;
[print(''.join(''.join('#' if FONT8X8[ord(c)-FONT8X8_FIRST][y] & (0x80>>i) else ' ' for i in range(8)) for c in s)) for s in ['ABCDEFGHIJKLM','NOPQRSTUVWXYZ','0123456789 :/?'] for y in range(8)]"
```

Expected: three blocks of eight lines each, every letter and digit legible.

- [ ] **Step 5: Commit**

```bash
git add tools/font8x8.py tools/mkmenuart.py saturn/cd/data/MENUFONT.ART \
        saturn/cd/data/MENUPANP.ART saturn/cd/data/MENUPANS.ART \
        saturn/cd/data/MENUPANC.ART
git commit -m "Build the menu font and its three panels, with every colour derived from the title card's own palette so a re-grab re-keys the menus, and a verify that fails the build on a collapsed ramp or a width VDP1 would shear."
```

---

### Task 6: `saturn_menuart` — the SRL seam

Loads the four `.ART` files, draws an item list as VDP1 sprites, runs the lightning flash, and owns NBG0's visibility. Nothing here is host-testable; it gets a syntax check and the emulator run.

**Files:**
- Create: `saturn/src/system/saturn_menuart.h`
- Create: `saturn/src/system/saturn_menuart.cxx`
- Modify: `saturn/src/system/saturn_bootart.h` — add `boot_art_title_texture`
- Modify: `saturn/src/system/saturn_bootart.cxx` — implement it
- Modify: `saturn/makefile` — `SRL_MAX_TEXTURES` 100 → 128

**Interfaces:**
- Consumes: `menu_layout.h` — `MenuItem`, `MENU_ITEM_PANEL`, `MENU_ITEM_GLYPH`, `MENU_PANEL_*`, `MENU_RAMP_SEL`; `disc.h` — `disc_read_file`; `saturn_compat.h` — `saturn_lwram_alloc`, `saturn_lwram_free`; `saturn_bootart.h` — `boot_art_title_texture`, `boot_art_title_flash`
- Produces:
  - `int boot_art_title_texture(void);` — the `BOOTTITL.ART` texture id, or -1
  - `void boot_art_title_flash(int lit);`
  - `int menu_art_load(void);`
  - `void menu_art_begin(int exclusive);`
  - `void menu_art_draw(const MenuItem *items, int count);`
  - `void menu_art_present(void);`
  - `void menu_art_end(void);`

- [ ] **Step 1: Expose the title texture from the boot art**

In `saturn/src/system/saturn_bootart.h`, add inside the `extern "C"` block:

```c
/*----------------------
 | boot_art_title_texture
 | Description: The BOOTTITL.ART texture id, so the sub-title menu can draw
 |   the title card without loading a second copy. VDP1's allocator is a bump
 |   allocator with no free, so a duplicate would cost 35 KB of sprite VRAM
 |   permanently and a second one per attract replay.
 | Author: suinevere
 | Params: N/A
 | Returns: the texture id, or -1 if the boot art never loaded
 ----------------------*/
int boot_art_title_texture(void);

/*----------------------
 | boot_art_title_flash
 | Description: Swaps the title card's CRAM bank between its own palette and a
 |   brightened copy, which is the sub-title menu's lightning. It lives here
 |   rather than in saturn_menuart.cxx because this file owns that texture and
 |   its bank; a flash driven from outside would need both handed out and
 |   could be left on by a caller that returned early.
 |
 |   Both palettes are computed once during boot_art_load, so a frame that
 |   flashes costs one slDMACopy and no arithmetic.
 | Author: suinevere
 | Params: lit -- non-zero for the brightened palette, zero for the original
 | Returns: N/A
 ----------------------*/
void boot_art_title_flash(int lit);
```

In `saturn/src/system/saturn_bootart.cxx`, add these statics beside `g_texture`:

```cpp
static int32_t g_titleBank = -1;
static SRL::Types::HighColor g_titlePal[16];
static SRL::Types::HighColor g_titleLit[16];
static int16_t g_titleCount;
static int g_titleFlashOn = -1;
```

Their banner: the title card's palette has to be kept because the staging buffer it was read from is freed at the end of `boot_art_load`, and the flash needs the original back every time it turns off. `g_titleFlashOn` starts at -1 rather than 0 so the first call always writes, whichever state it asks for.

In `boot_art_load`'s per-file loop, after the `Palette(...).Load(...)` call, capture the title card's bank and palette:

```cpp
        if (i == BOOT_ART_TITLE)
        {
            int16_t e;

            g_titleBank = bank;
            g_titleCount = (int16_t)count;

            for (e = 0; e < (int16_t)count && e < 16; e++)
            {
                SRL::Types::HighColor c = ((SRL::Types::HighColor *)(stage + 8))[e];
                uint16_t raw = *(uint16_t *)&c;
                uint16_t r = (uint16_t)(raw & 0x1Fu);
                uint16_t g = (uint16_t)((raw >> 5) & 0x1Fu);
                uint16_t b = (uint16_t)((raw >> 10) & 0x1Fu);

                r = (uint16_t)(r + ((31u - r) / 2u));
                g = (uint16_t)(g + ((31u - g) / 2u));
                b = (uint16_t)(b + ((31u - b) / 2u));

                g_titlePal[e] = c;
                raw = (uint16_t)(0x8000u | (b << 10) | (g << 5) | r);
                g_titleLit[e] = *(SRL::Types::HighColor *)&raw;
            }
        }
```

The brightening is "halfway to full" per channel rather than a multiply, so it cannot overflow and needs no clamp — a channel already at 31 stays at 31. `HighColor` is 5 bits per channel, `0x8000 | (b << 10) | (g << 5) | r`.

Add beside the other `extern "C"` definitions:

```cpp
extern "C" int boot_art_title_texture(void)
{
    return (int)g_texture[BOOT_ART_TITLE];
}

extern "C" void boot_art_title_flash(int lit)
{
    if (g_titleBank < 0 || g_titleFlashOn == (lit != 0))
    {
        return;
    }

    g_titleFlashOn = (lit != 0);

    SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16,
                       (uint16_t)g_titleBank)
        .Load(lit ? g_titleLit : g_titlePal, g_titleCount);
}
```

`boot_art_title_flash` returns early when the state is unchanged, so a menu frame that is not flashing costs nothing at all rather than a redundant DMA every frame.

- [ ] **Step 2: Raise the texture ceiling**

In `saturn/makefile`, change:

```
SRL_MAX_TEXTURES = 100          # Number of VDP1 texture slots
```

to:

```
SRL_MAX_TEXTURES = 128          # Number of VDP1 texture slots
```

Extend the trailing comment to say why: boot art holds 7 permanently and the menu font plus its three panels hold 67 more, for 74. 100 would fit but leaves no room for another screen, and the table costs one descriptor per slot.

- [ ] **Step 3: Write the header**

Create `saturn/src/system/saturn_menuart.h`:

```c
#ifndef SATURN_MENUART_H
#define SATURN_MENUART_H

#include "menu_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

int  menu_art_load(void);
void menu_art_begin(int exclusive);
void menu_art_draw(const MenuItem *items, int count);
void menu_art_present(void);
void menu_art_end(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_MENUART_H */
```

File banner must carry:
- The same seam rule `saturn_bootart.h` states: callers never include `<srl.hpp>`, because the engine's headers wrap SGL's C headers in `extern "C"` and mixing that with SRL's C++ headers in one translation unit is fragile.
- `menu_art_begin(1)` hides NBG0 and draws the title card behind the items; `menu_art_begin(0)` leaves NBG0 alone so the menu composites over the game's last presented frame. Exclusive mode is the sub-title menu and the gate's load screen; non-exclusive is the pause menu. VDP1 sprites draw in front of NBG0, which is what makes the non-exclusive case free — nothing is copied and no engine state is touched.
- Present nothing through `video_present` between `menu_art_begin` and `menu_art_end`.

- [ ] **Step 4: Write the implementation**

Create `saturn/src/system/saturn_menuart.cxx`:

```cpp
#include <srl.hpp>
#include <stdio.h>
#include "saturn_menuart.h"
#include "disc.h"
#include "saturn_bootart.h"
#include "saturn_compat.h"

using namespace SRL::Math::Types;

#define MENU_ART_Z_BACK   480
#define MENU_ART_Z_PANEL  470
#define MENU_ART_Z_GLYPH  460

#define MENU_ART_STAGE_BYTES 23040

#define MENU_ART_PAL_ENTRIES 16
#define MENU_ART_PIXELS_AT   (8 + 2 * MENU_ART_PAL_ENTRIES)
#define MENU_ART_GLYPH_W     8
#define MENU_ART_GLYPH_H     8
#define MENU_ART_GLYPH_BYTES ((MENU_ART_GLYPH_W * MENU_ART_GLYPH_H) / 2)
#define MENU_ART_GLYPH_COUNT 64

#define MENU_ART_IDX_TEXT 1
#define MENU_ART_IDX_SEL  4

#define MENU_ART_PANEL_COUNT 3

#define MENU_ART_FLASH_FRAMES 3
#define MENU_ART_FLASH_PERIOD 137
#define MENU_ART_FLASH_SCALE  6

#define MENU_ART_SCREEN_W 320
#define MENU_ART_SCREEN_H 224

static const char *MENU_ART_PANEL_FILES[MENU_ART_PANEL_COUNT] =
{
    "MENUPANP.ART",
    "MENUPANS.ART",
    "MENUPANC.ART"
};

static int32_t g_glyph[MENU_ART_GLYPH_COUNT];
static int32_t g_panel[MENU_ART_PANEL_COUNT];
static int16_t g_panelW[MENU_ART_PANEL_COUNT];
static int16_t g_panelH[MENU_ART_PANEL_COUNT];
static int32_t g_bankDim = -1;
static int32_t g_bankSel = -1;
static int g_loaded;
static int g_exclusive;
static unsigned int g_frame;
```

`g_glyph`, `g_panel`, `g_bankDim` and `g_bankSel` are allocated once and never released, for the reason `boot_art_load` records: `VDP1::TryAllocateTexture` is a bump allocator with no free, so the attract loop would run 512 KB dry in a few passes if this reloaded.

The title card's palette is not held here. `saturn_bootart.cxx` owns that texture and its CRAM bank, so it owns the flash too — this file only decides *when*.

- [ ] **Step 4a: Write the sprite and palette helpers**

```cpp
static void menu_art_sprite(int32_t texture, int16_t x, int16_t y, int16_t w,
                            int16_t h, int16_t z, SRL::CRAM::Palette *pal)
{
    int16_t cx;
    int16_t cy;

    if (texture < 0)
    {
        return;
    }

    cx = (int16_t)(x + (w / 2) - (MENU_ART_SCREEN_W / 2));
    cy = (int16_t)(y + (h / 2) - (MENU_ART_SCREEN_H / 2));

    SRL::Scene2D::DrawSprite((uint16_t)texture, pal, Vector3D(cx, cy, z));
}
```

`menu_art_sprite`'s banner: `int16_t` parameters rather than a floating type, for the reason `bootArtSprite` records — `Fxp`'s floating-point constructor is `consteval` and a function parameter is never a constant expression. Also state the coordinate convention: callers pass a top-left corner in the 320x224 frame, and this converts to the centre-relative coordinate `Scene2D::DrawSprite` wants, so no caller has to know where the origin is.

```cpp
static int32_t menu_art_bank(const uint8_t *file, int textIndex)
{
    SRL::Types::HighColor entries[MENU_ART_PAL_ENTRIES];
    int32_t bank;
    int i;

    bank = SRL::CRAM::GetFreeBank(SRL::CRAM::TextureColorMode::Paletted16);

    if (bank < 0)
    {
        printf("menu_art_load: no CRAM bank\n");
        return -1;
    }

    for (i = 0; i < MENU_ART_PAL_ENTRIES; i++)
    {
        entries[i] = ((SRL::Types::HighColor *)(file + 8))[i];
    }
    entries[MENU_ART_IDX_TEXT] = entries[textIndex];

    SRL::CRAM::SetBankUsedState((uint16_t)bank,
        SRL::CRAM::TextureColorMode::Paletted16, true);
    SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16, (uint16_t)bank)
        .Load(entries, (int16_t)MENU_ART_PAL_ENTRIES);

    return bank;
}
```

`menu_art_bank`'s banner: both banks are built from the same file. The dim bank passes `MENU_ART_IDX_TEXT`, which is a self-copy and leaves the palette as authored; the bright bank passes `MENU_ART_IDX_SEL`, entry 4, which no pixel uses and which `mkmenuart.py` filled with the selected colour precisely so the runtime does not have to invent one.

- [ ] **Step 4b: Write the loader**

```cpp
static int menu_art_release_banks(void)
{
    if (g_bankDim >= 0)
    {
        SRL::CRAM::SetBankUsedState((uint16_t)g_bankDim,
            SRL::CRAM::TextureColorMode::Paletted16, false);
        g_bankDim = -1;
    }
    if (g_bankSel >= 0)
    {
        SRL::CRAM::SetBankUsedState((uint16_t)g_bankSel,
            SRL::CRAM::TextureColorMode::Paletted16, false);
        g_bankSel = -1;
    }
    return 0;
}

extern "C" int menu_art_load(void)
{
    uint8_t *stage;
    int i;

    if (g_loaded)
    {
        return 1;
    }

    stage = (uint8_t *)saturn_lwram_alloc(MENU_ART_STAGE_BYTES);

    if (stage == 0)
    {
        printf("menu_art_load: no LWRAM for the staging buffer\n");
        return 0;
    }

    if (disc_read_file("MENUFONT.ART", stage, MENU_ART_STAGE_BYTES) < 0)
    {
        printf("menu_art_load: MENUFONT.ART did not read\n");
        saturn_lwram_free(stage);
        return 0;
    }

    g_bankDim = menu_art_bank(stage, MENU_ART_IDX_TEXT);
    g_bankSel = menu_art_bank(stage, MENU_ART_IDX_SEL);

    if (g_bankDim < 0 || g_bankSel < 0)
    {
        menu_art_release_banks();
        saturn_lwram_free(stage);
        return 0;
    }

    for (i = 0; i < MENU_ART_GLYPH_COUNT; i++)
    {
        g_glyph[i] = SRL::VDP1::TryLoadTexture(MENU_ART_GLYPH_W,
            MENU_ART_GLYPH_H, SRL::CRAM::TextureColorMode::Paletted16,
            (uint16_t)g_bankDim,
            stage + MENU_ART_PIXELS_AT + i * MENU_ART_GLYPH_BYTES);

        if (g_glyph[i] < 0)
        {
            printf("menu_art_load: glyph %d got no VDP1 texture\n", i);
            menu_art_release_banks();
            saturn_lwram_free(stage);
            return 0;
        }
    }

    for (i = 0; i < MENU_ART_PANEL_COUNT; i++)
    {
        uint16_t width;
        uint16_t height;
        uint16_t count;

        if (disc_read_file(MENU_ART_PANEL_FILES[i], stage,
                           MENU_ART_STAGE_BYTES) < 0)
        {
            printf("menu_art_load: %s did not read\n",
                   MENU_ART_PANEL_FILES[i]);
            menu_art_release_banks();
            saturn_lwram_free(stage);
            return 0;
        }

        width = (uint16_t)((stage[2] << 8) | stage[3]);
        height = (uint16_t)((stage[4] << 8) | stage[5]);
        count = (uint16_t)((stage[6] << 8) | stage[7]);

        if (count != MENU_ART_PAL_ENTRIES)
        {
            printf("menu_art_load: %s has %u palette entries\n",
                   MENU_ART_PANEL_FILES[i], (unsigned)count);
            menu_art_release_banks();
            saturn_lwram_free(stage);
            return 0;
        }

        g_panelW[i] = (int16_t)width;
        g_panelH[i] = (int16_t)height;
        g_panel[i] = SRL::VDP1::TryLoadTexture(width, height,
            SRL::CRAM::TextureColorMode::Paletted16, (uint16_t)g_bankDim,
            stage + MENU_ART_PIXELS_AT);

        if (g_panel[i] < 0)
        {
            printf("menu_art_load: %s got no VDP1 texture\n",
                   MENU_ART_PANEL_FILES[i]);
            menu_art_release_banks();
            saturn_lwram_free(stage);
            return 0;
        }
    }

    saturn_lwram_free(stage);
    g_loaded = 1;
    return 1;
}
```

Banner notes that must appear on `menu_art_load`:
- It returns 1 immediately when already loaded, for the reason `boot_art_load` records: the attract loop re-enters it and VDP1's allocator cannot free.
- `saturn_lwram_free(stage)` is on **every** exit path including success. It is a real allocator, not a bump allocator, and holding 22 KB of LWRAM across the whole game would come out of the same pool `savedata_probe`'s scratch draws from.
- Panel width and height come out of each file's own header, never from a constant here, so a resize in `mkmenuart.py` cannot desynchronise the two.
- A failed `TryLoadTexture` after a bank was already marked used leaks that bank unless it is released by hand; SRL does not unmark it. That is a pre-existing gap in SRL, which is why `menu_art_release_banks` exists.

- [ ] **Step 4c: Write the drawing and the mode control**

```cpp
static void menu_art_panel(int id, int16_t x, int16_t y)
{
    SRL::CRAM::Palette pal(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bankDim);

    menu_art_sprite(g_panel[id], x, y, g_panelW[id], g_panelH[id],
                    MENU_ART_Z_PANEL, &pal);
}

extern "C" void menu_art_begin(int exclusive)
{
    g_exclusive = exclusive;
    g_frame = 0u;

    if (exclusive)
    {
        SRL::VDP2::NBG0::ScrollDisable();
    }
}

extern "C" void menu_art_draw(const MenuItem *items, int count)
{
    SRL::CRAM::Palette dim(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bankDim);
    SRL::CRAM::Palette sel(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bankSel);
    int i;

    if (!g_loaded)
    {
        return;
    }

    if (g_exclusive)
    {
        g_frame++;
        boot_art_title_flash((g_frame % MENU_ART_FLASH_PERIOD)
                             < MENU_ART_FLASH_FRAMES);
        menu_art_sprite(boot_art_title_texture(), 0, 0, MENU_ART_SCREEN_W,
                        MENU_ART_SCREEN_H, MENU_ART_Z_BACK, 0);
    }

    for (i = 0; i < count; i++)
    {
        if (items[i].kind == MENU_ITEM_PANEL)
        {
            if (items[i].id < MENU_ART_PANEL_COUNT)
            {
                menu_art_panel(items[i].id, items[i].x, items[i].y);
            }
        }
        else if (items[i].id >= 0x20u && items[i].id <= 0x5Fu)
        {
            menu_art_sprite(g_glyph[items[i].id - 0x20u], items[i].x,
                            items[i].y, MENU_ART_GLYPH_W, MENU_ART_GLYPH_H,
                            MENU_ART_Z_GLYPH,
                            (items[i].ramp == MENU_RAMP_SEL) ? &sel : &dim);
        }
    }
}

extern "C" void menu_art_present(void)
{
    SRL::Core::Synchronize();
}

extern "C" void menu_art_end(void)
{
    if (g_exclusive)
    {
        boot_art_title_flash(0);
        SRL::VDP2::NBG0::ScrollEnable();
        g_exclusive = 0;
    }
}
```

Banner notes that must appear:
- On `menu_art_draw`: the backdrop and the flash are exclusive-mode only. In non-exclusive mode NBG0 is still showing the game's last presented frame, and a backdrop over it would be exactly the freeze buffer this design exists to avoid.
- On the `Palette` locals: built per call rather than held as statics because `Palette` is two members and a constructor, and this codebase has already lost a day to a static-initialiser hypothesis. Constructing them here costs nothing and cannot be ordered wrongly.
- On `menu_art_end`: the flash is turned off before NBG0 comes back, or the title card's palette would be left bright for the next screen that uses it. And the caller must present one more empty frame after this, or the last menu frame's sprites stay composited over the game — nothing else in this port draws sprites, so nothing would otherwise replace them.
- On the Z constants: `slSetSprite` sorts far to near, so the smaller value draws on top. Unverified until the emulator run, the same way `BOOT_ART_Z_BACK` was. If glyphs vanish behind the panel, swap `MENU_ART_Z_PANEL` and `MENU_ART_Z_GLYPH`.

The Z constants' banner must carry the same caveat `BOOT_ART_Z_BACK` does: `slSetSprite` sorts far to near, so the smaller value draws on top, and this ordering is unverified until the emulator run. If glyphs disappear behind the panel, swap `MENU_ART_Z_PANEL` and `MENU_ART_Z_GLYPH`.

- [ ] **Step 5: Syntax-check both new translation units**

Get the flags:

```
cd saturn && make -n src/system/saturn_bootart.o
```

Take the `sh2eb-elf-g++` command it prints, replace `-c -o <obj>` with `-fsyntax-only`, and run it against each file in turn:

```
cd saturn
../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe <flags> -fsyntax-only src/system/saturn_menuart.cxx
../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe <flags> -fsyntax-only src/system/saturn_bootart.cxx
```

Expected: no output from either. The compiler is not on `PATH`; use the path above.

- [ ] **Step 6: Confirm the host suites still pass**

```
sh saturn/tests/run_tests.sh
```

Nothing here should touch them, but `saturn_menuart.h` includes `menu_layout.h` and a header mistake would show up as a broken suite.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/system/saturn_menuart.h saturn/src/system/saturn_menuart.cxx \
        saturn/src/system/saturn_bootart.h saturn/src/system/saturn_bootart.cxx \
        saturn/makefile
git commit -m "Draw the menus as VDP1 sprites over whatever NBG0 already holds, reusing the boot art's title card texture rather than a second copy, with the two text ramps as CRAM banks selected per sprite and the lightning as a flash of the backdrop's own palette."
```

---

### Task 7: The driver and the two call sites

The task that lands caller and callee together, because `saturn/makefile` globs `src/**/*.c` and a caller without its callee breaks the link for every task in between.

**Files:**
- Create: `saturn/src/menus/menu.h`
- Create: `saturn/src/menus/menu.c`
- Modify: `saturn/src/input.h` — replace `input_debug_chord` with `input_menu_start`
- Modify: `saturn/src/system/input_srl.cxx` — same
- Modify: `saturn/src/main.h` — declare `play_intro`
- Modify: `saturn/src/main.c` — `play_intro` loses `static`; the chord's body is replaced; the gate is added; `ending_played` is defined
- Modify: `saturn/src/decode.c` — set `ending_played` on the ending path

**Interfaces:**
- Consumes: `menu_state.h`, `menu_layout.h`, `menu_clock.h`, `saturn_menuart.h`, `saturn_saveslot.h` (`saturn_saveslot_save`, `saturn_saveslot_load`), `saturn_backup.h` (`sat_bup_probe`, `SatBupDev`), `savedata.h` (`savedata_probe`, `savedata_pick_default_device`, `SAVE_MAX_BYTES`), `disc.h` (`disc_play_track`, `disc_stop_track`, `disc_set_music_volume`), `input.h` (`check_events`, `input_menu_start`, and `key_up`/`key_down`/`key_left`/`key_right`/`key_a`/`key_b`), `platform.h` (`platform_ticks`), `client.h` (`cls.quit`), `main.h` (`play_intro`), `saturn_compat.h` (`saturn_lwram_alloc`, `saturn_lwram_free`)
- Produces:
  - `int input_menu_start(void);` — 1 while Start is held, 0 otherwise
  - `int menu_gate(void);` — the room `run()` should load, or 0 if a load already restored the state
  - `void menu_pause_poll(void);`
  - `int ending_played;` — defined in `main.c` beside `next_script`, set by `decode.c`, consumed by `menu_gate`
  - `MENU_START_ROOM` 1, `MENU_PASSWORD_ROOM` 7

**Why `input_menu_start` and not `key_select`:** `input_srl.cxx:72-76` states that `key_select` is *deliberately never written* on Saturn — it is host input-recording state, and `update_keys` reads it for the record/replay path. Start is read in exactly one place today, inside `input_debug_chord`, which this task removes. Rather than add a ninth key global to a seam both backends share, the chord's declaration is replaced in place by a narrower one that answers the only question the menu asks.

- [ ] **Step 1: Write the header**

Create `saturn/src/menus/menu.h`:

```c
#ifndef MENU_H
#define MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_PASSWORD_ROOM 7
#define MENU_START_ROOM    1

int  menu_gate(void);
void menu_pause_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_H */
```

Banner notes that must appear:
- `MENU_PASSWORD_ROOM`: room 7 is the Sega CD's password entry screen. `run()` reaches it after the intro (`main.c`), and `decode.c:1861` sends the ending credits back to it. Intercepting the room rather than the reason covers every route, including whatever a death does, which is not visible in the code.
- `MENU_START_ROOM`: where a new game begins, now that the password screen no longer picks it. One constant, because this is the one value in the feature that could not be verified by reading and has to survive an emulator run.
- `menu_gate`: returns 0 when a slot was loaded, because `quickload()` calls `load_room` and restores tasks and sprites itself — running `run()`'s load block over that would reset the sprite list and the tasks it just restored and load the room twice.
- `menu_pause_poll`: must be called between `check_events()` and the task loop's first `toggle_aux(0)` and nowhere else. That is the only point where `quicksave`'s and `quickload`'s "no active thread" precondition holds.

- [ ] **Step 2: Write the driver**

Create `saturn/src/menus/menu.c`. Statics, then helpers, then the two entry points, all bannered.

```c
#include <string.h>
#include <stdio.h>

#include "menu.h"
#include "menu_state.h"
#include "menu_layout.h"
#include "menu_clock.h"
#include "saturn_menuart.h"
#include "saturn_saveslot.h"
#include "saturn_backup.h"
#include "savedata.h"
#include "saturn_compat.h"
#include "disc.h"
#include "input.h"
#include "platform.h"
#include "client.h"
#include "main.h"

#define MENU_GATE_SUBTITLE 0
#define MENU_GATE_LOAD     1

static int s_gateMode = MENU_GATE_SUBTITLE;
static int s_pausePrev;

#define MENU_BIT_UP      0x01
#define MENU_BIT_DOWN    0x02
#define MENU_BIT_LEFT    0x04
#define MENU_BIT_RIGHT   0x08
#define MENU_BIT_CONFIRM 0x10
#define MENU_BIT_CANCEL  0x20
#define MENU_BIT_PAUSE   0x40

static int menu_key_mask(void)
{
    int mask = 0;

    if (key_up)    mask |= MENU_BIT_UP;
    if (key_down)  mask |= MENU_BIT_DOWN;
    if (key_left)  mask |= MENU_BIT_LEFT;
    if (key_right) mask |= MENU_BIT_RIGHT;
    if (key_a)     mask |= MENU_BIT_CONFIRM;
    if (key_b)     mask |= MENU_BIT_CANCEL;

    if (input_menu_start()) mask |= MENU_BIT_PAUSE;

    return mask;
}

static void menu_edges(int pressed, MenuInput *in)
{
    memset(in, 0, sizeof(*in));
    in->up      = (pressed & MENU_BIT_UP) != 0;
    in->down    = (pressed & MENU_BIT_DOWN) != 0;
    in->left    = (pressed & MENU_BIT_LEFT) != 0;
    in->right   = (pressed & MENU_BIT_RIGHT) != 0;
    in->confirm = (pressed & MENU_BIT_CONFIRM) != 0;
    in->cancel  = (pressed & MENU_BIT_CANCEL) != 0;
    in->pause   = (pressed & MENU_BIT_PAUSE) != 0;
}

static int menu_slots_used(unsigned long device, unsigned char *scratch)
{
    SlotInfo info;
    int i;
    int used = 0;

    for (i = 0; i < SAVE_NUM_SLOTS; i++)
    {
        if (savedata_probe(device, i, &info, scratch, SAVE_MAX_BYTES)
            != SLOT_EMPTY)
        {
            used++;
        }
    }
    return used;
}

static void menu_rescan(MenuState *st, unsigned char *scratch)
{
    SatBupDev internal;
    SatBupDev cart;
    int i;

    memset(&internal, 0, sizeof(internal));
    memset(&cart, 0, sizeof(cart));
    sat_bup_probe(SAT_BUP_INTERNAL, &internal);
    sat_bup_probe(SAT_BUP_CART, &cart);
    st->cartPresent = cart.present && cart.formatted;

    if (st->device != SAT_BUP_INTERNAL && st->device != SAT_BUP_CART)
    {
        st->device = savedata_pick_default_device(&internal, &cart,
            menu_slots_used(SAT_BUP_INTERNAL, scratch),
            st->cartPresent ? menu_slots_used(SAT_BUP_CART, scratch) : 0);
    }
    if (st->device == SAT_BUP_CART && !st->cartPresent)
    {
        st->device = SAT_BUP_INTERNAL;
    }

    for (i = 0; i < SAVE_NUM_SLOTS; i++)
    {
        savedata_probe(st->device, i, &st->slots[i], scratch, SAVE_MAX_BYTES);
    }
}
```

`menu_rescan`'s banner must say why the device is only chosen when `st->device` holds neither valid id: the entry points leave it zero so the first rescan picks, and every rescan after that respects what the player selected with left and right. Without that guard a device toggle would be undone by the rescan it triggers.

`menu_slots_used`'s banner must say why it exists rather than passing zeros: `savedata_pick_default_device` decides between a cart and internal memory on whether each *holds saves*, and handing it two zeros would make it return internal unconditionally and turn a real decision into dead code. It runs at most twice, once per menu open.

`menu_run` is the shared inner loop both entry points use:

```c
static MenuAction menu_run(MenuState *st, int exclusive, int useClock,
                           MenuAction onNoMemory)
{
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    menu_clock_state clk;
    menu_clock_frame frame;
    MenuInput in;
    MenuAction action = MENU_ACT_NONE;
    const char *status = 0;
    unsigned char *scratch;
    int previous;
    int current;
    int pressed;
    int count;
    int err;

    scratch = (unsigned char *)saturn_lwram_alloc(SAVE_MAX_BYTES);

    if (scratch == 0)
    {
        printf("menu_run: no LWRAM for the probe scratch\n");
        return onNoMemory;
    }

    menu_art_begin(exclusive);
    menu_art_draw(items, 0);
    menu_art_present();

    check_events();
    previous = menu_key_mask();

    menu_rescan(st, scratch);

    if (useClock)
    {
        menu_clock_enter(&clk, (unsigned int)platform_ticks());
        disc_play_track(MENU_MUSIC_INDEX, 0);
    }

    while (cls.quit == 0)
    {
        check_events();
        current = menu_key_mask();
        pressed = current & ~previous;
        previous = current;

        menu_edges(pressed, &in);
        action = menu_state_step(st, &in);

        if (action == MENU_ACT_RESCAN_SLOTS)
        {
            menu_rescan(st, scratch);
            action = MENU_ACT_NONE;
        }
        else if (action == MENU_ACT_SAVE_SLOT)
        {
            err = saturn_saveslot_save(st->device, st->slotCursor);
            status = menu_layout_status_text(err, st->device);
            menu_rescan(st, scratch);
            action = MENU_ACT_NONE;
        }
        else if (action == MENU_ACT_LOAD_SLOT)
        {
            err = saturn_saveslot_load(st->device, st->slotCursor);

            if (err == SAT_BUP_OK)
            {
                break;
            }
            status = menu_layout_status_text(err, st->device);
            action = MENU_ACT_NONE;
        }
        else if (action != MENU_ACT_NONE)
        {
            break;
        }

        if (useClock && st->screen == MENU_TITLE)
        {
            menu_clock_step(&clk, (unsigned int)platform_ticks(),
                            pressed != 0, &frame);
            disc_set_music_volume(frame.music_volume);

            if (frame.music_restart)
            {
                disc_play_track(MENU_MUSIC_INDEX, 0);
            }
            if (frame.launch_attract)
            {
                menu_art_end();
                menu_art_present();
                disc_stop_track();
                play_intro();
                disc_set_music_volume((unsigned char)MENU_VOLUME_MAX);
                menu_art_begin(1);
                menu_state_enter_title(st);
                status = 0;
                menu_clock_enter(&clk, (unsigned int)platform_ticks());
                disc_play_track(MENU_MUSIC_INDEX, 0);
                check_events();
                previous = menu_key_mask();
                continue;
            }
        }

        count = menu_layout_build(st, status, items, MENU_LAYOUT_MAX_ITEMS);
        menu_art_draw(items, count);
        menu_art_present();
    }

    menu_art_end();
    menu_art_present();
    saturn_lwram_free(scratch);

    return action;
}
```

Banner notes that must appear on `menu_run`:
- One frame is drawn and presented before the pad is ever sampled, and this ordering is the whole defence against a button held on entry acting as a press. `check_events()` only reads SRL's peripheral array; the thing that refreshes it is `Core::Synchronize`, reached solely through `menu_art_present`. Until that first refresh port 0 holds its static initialiser `0xff`, which reads as not-connected, and `check_events` zeroes every key — so priming from it would capture a synthetic zero and report every held button as newly pressed. This is `boot_sequence`'s rule and it is load-bearing here for the same reason.
- The final `menu_art_present` after `menu_art_end` submits an empty command list, so the last menu frame's sprites are not left composited over the game.
- `items` is `static` rather than automatic: 200 `MenuItem` is 1,600 bytes, and this function is on the stack under `run()` with the whole VM below it.
- The attract block's order is fixed. `play_intro` needs NBG0 and the drive, so the overlay comes down and the music stops before it runs; volume goes back to full before it starts, or the cinematic's own tracks would play at the menu's faded-out level; and `previous` is re-primed from the live pad afterwards because the button that skipped the cinematic is very likely still held.
- `onNoMemory` exists because the two callers want different fallbacks when the scratch cannot be allocated: the gate must still start a game, the pause poll must still resume.
- The clock is stepped only while `st->screen == MENU_TITLE`. A player reading three slot rows is not idle, and tearing the screen down under them to play a cinematic would be a bug that looks like a feature.

The two entry points:

```c
int menu_gate(void)
{
    MenuState st;
    MenuAction action;

    if (ending_played)
    {
        ending_played = 0;
        s_gateMode = MENU_GATE_SUBTITLE;
    }

    if (!menu_art_load())
    {
        return MENU_START_ROOM;
    }

    memset(&st, 0, sizeof(st));

    if (s_gateMode == MENU_GATE_LOAD)
    {
        menu_state_enter_slots(&st, 0, MENU_TITLE);
    }
    else
    {
        menu_state_enter_title(&st);
    }

    action = menu_run(&st, 1, 1, MENU_ACT_START_GAME);

    if (action == MENU_ACT_LOAD_SLOT)
    {
        s_gateMode = MENU_GATE_LOAD;
        return 0;
    }

    s_gateMode = MENU_GATE_LOAD;
    return MENU_START_ROOM;
}

void menu_pause_poll(void)
{
    MenuState st;
    MenuAction action;
    int current = menu_key_mask();
    int pressed = current & ~s_pausePrev;

    s_pausePrev = current;

    if ((pressed & MENU_BIT_PAUSE) == 0)
    {
        return;
    }
    if (!menu_art_load())
    {
        return;
    }

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);

    action = menu_run(&st, 0, 0, MENU_ACT_RESUME);
    s_pausePrev = menu_key_mask();

    if (action == MENU_ACT_RETURN_TO_TITLE)
    {
        s_gateMode = MENU_GATE_SUBTITLE;
        next_script = MENU_PASSWORD_ROOM;
    }
}
```

`next_script` and `ending_played` are declared `extern int` at the top of the file, beside a banner explaining both: setting `next_script` to `MENU_PASSWORD_ROOM` is how *Return to Title* re-enters the gate, because it is the one value `run()`'s loop routes through `menu_gate`; and `ending_played` is `decode.c`'s hint that the credits just finished, which is the one arrival the driver cannot infer for itself.

`menu_gate`'s banner must state:
- Both non-load exits set `MENU_GATE_LOAD`, so the next arrival opens the load screen rather than the sub-title menu.
- `ending_played` is **consumed**, not merely read. Clearing it as the mode is selected is what makes the flag mean "this arrival" rather than "this playthrough" — leave it set and every death in the game the player starts from the credits would land on the sub-title menu too.
- `menu_pause_poll` is the only other thing that sets the mode back to `MENU_GATE_SUBTITLE`.

`menu_pause_poll`'s banner must state that `s_pausePrev` is re-primed from the live pad after the menu closes, because the player is very likely still holding the button that dismissed it, and that the edge shape here is deliberately `current & ~previous` rather than the debug chord's — going from Start+B to Start+A+B without releasing fired a save under the old shape.

- [ ] **Step 3: Declare `play_intro`**

In `saturn/src/main.h`, add:

```c
/*----------------------
 | play_intro
 | Description: Plays the four-file opening cinematic, INTRO1..4.BIN over cue
 |   tracks 31..34. Any button breaks the whole sequence, not just the current
 |   file, because play_anm is called with skippable == 0.
 |
 |   Not static, because menus/menu.c replays it as the sub-title menu's
 |   attract loop. The alternative -- a re-entrant gate returning "play the
 |   cinematic and call me again" -- buys nothing and complicates the one loop
 |   in the program that must stay readable.
 | Author: suinevere
 | Dependencies: disc.h, animation.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void play_intro(void);
```

In `saturn/src/main.c`, change `static void play_intro()` to `void play_intro(void)` and delete the old doc comment above it, which the banner in `main.h` replaces.

- [ ] **Step 4: Replace the chord and add the gate**

In `saturn/src/input.h`, replace the `input_debug_chord` banner and declaration with:

```c
/*----------------------
 | input_menu_start
 | Description: Whether Start is held. The menu needs it and no key_* global
 |   carries it: input_srl.cxx deliberately never writes key_select, which is
 |   host input-recording state that update_keys reads for record and replay.
 |   A ninth key global would put a Saturn-only signal into a seam both
 |   backends share, so the question is asked directly instead.
 |
 |   Level, not edge -- menu.c owns edge detection for every button it reads,
 |   and mixing the two is what let the debug chord this replaces fire a save
 |   when the player went from Start plus B to Start plus A plus B without
 |   releasing.
 |
 |   Saturn only. The host has no caller: menu_pause_poll is behind
 |   HOTA_SATURN, which is why input_sdl.c never defined the chord either.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: 1 while Start is held, 0 otherwise
 ----------------------*/
int input_menu_start(void);
```

In `saturn/src/system/input_srl.cxx`, replace `input_debug_chord` and its banner with:

```cpp
extern "C" int input_menu_start(void)
{
    SRL::Input::Digital pad(0);

    return (pad.IsConnected()
            && pad.IsHeld(SRL::Input::Digital::Button::START)) ? 1 : 0;
}
```

Take the peripheral construction from the surrounding file rather than the line above if it differs; the point is that `Button::START` is uppercase and its neighbours are not, and the port-0 plus `IsConnected` guard is required because ports 1-11 read as a connected pad with every button held until the first refresh.

Delete any statics the old chord owned, and its edge-tracking state.

In `saturn/src/decode.c`, add beside the existing `extern int next_script;` at line 42:

```c
extern int ending_played;
```

and set it in the ending branch of the `load screen` opcode handler, immediately before the
existing `next_script = 7;`:

```c
				else if (next_script == 6)
				{
					play_animation("END1.BIN", 0, 37);
					play_animation("END2.BIN", 0, 38);
					play_animation("END3.BIN", 0, 39);
					play_animation("END4.BIN", 0, 40);

					/* return to password selection */
					ending_played = 1;
					next_script = 7;
					leave = 1;
				}
```

That is the whole change to `decode.c`. Leave the upstream comment alone, add none of your own inside the function, and do not guard the assignment — an `#ifdef` here would mean including a Saturn header in an engine file, and the host build compiles a store to an `int` nothing reads at no cost.

In `saturn/src/main.c`:

Add `#include "menu.h"` beside the other includes.

Define the flag beside `next_script` at line 81, with a banner:

```c
/*----------------------
 | ending_played
 | Description: Set by decode.c when the four ending animations have played and
 |   it is about to ask for room 7. It is the one arrival at the password screen
 |   the menu driver cannot infer for itself -- a death and an ending reach that
 |   room by the same route and look identical from there -- and it exists so a
 |   player who has just watched the credits gets the sub-title menu rather than
 |   a list of saves to reload.
 |
 |   Consumed by menu_gate, which clears it as it acts on it, so it means "this
 |   arrival" and not "this playthrough".
 |
 |   An int beside next_script rather than a call into menus/, because that
 |   would put a Saturn header and an #ifdef into decode.c for one bit.
 | Author: suinevere
 ----------------------*/
int ending_played;
```

Replace the `s_saveDevice` static and the whole body of `saturn_save_poll` with a call to `menu_pause_poll`, keeping the `#ifdef HOTA_SATURN` block itself and its call site:

```c
#ifdef HOTA_SATURN
static void saturn_save_poll(void)
{
	menu_pause_poll();
}
#endif
```

Update `saturn_save_poll`'s banner: it is no longer a development chord, it is the pause menu's poll, and this position — between `check_events()` and the task loop's first `toggle_aux(0)` — is the only place `quicksave`'s and `quickload`'s "no active thread" precondition holds, which is why the function survives rather than the call moving.

Then, in `run()`, add the gate immediately above the existing `if (next_script != 0)` block:

```c
	while (cls.quit == 0)
	{
		int i;

#ifdef HOTA_SATURN
		if (next_script == MENU_PASSWORD_ROOM)
		{
			next_script = menu_gate();
		}
#endif

		if (next_script != 0)
		{
```

Delete the now-unused `#include "system/saturn_backup.h"` and `#include "system/saturn_saveslot.h"` from `main.c` only if nothing else there uses them; check with a grep before removing either.

- [ ] **Step 5: Syntax-check the new and changed C**

```
cd saturn && make -n src/menus/menu.o
```

Take the printed `sh2eb-elf-gcc` command, replace `-c -o <obj>` with `-fsyntax-only`, and run it for `src/menus/menu.c` and then `src/main.c`.

Expected: no output from either. A missing symbol will not show here — that is the link, which the human runs — so read `menu.c`'s includes once more against the Interfaces list above before moving on.

- [ ] **Step 6: Confirm the host build and the host tests still pass**

```
sh saturn/tests/run_tests.sh
cd saturn/src && make
```

Expected: every suite passes, and `alien` links. The host build must be unaffected — `menus/` is not in its `OBJS` and both new call sites are `#ifdef HOTA_SATURN` — so a failure here means something leaked out of the guard.

- [ ] **Step 7: Read back the three things a compiler cannot check**

- `menu_pause_poll` is called from inside `saturn_save_poll`, and `saturn_save_poll`'s call site in `run()` is still between `check_events()` and the `for (i=0; i<MAX_TASKS; i++)` loop that calls `toggle_aux(0)`.
- The gate is inside `while (cls.quit == 0)` and **above** `if (next_script != 0)`, not below it.
- `quicksave`, `quickload`, `quicksave_sprites` and `quickload_sprites` are byte-for-byte unchanged: `git diff HEAD~1 -- saturn/src/main.c` shows no hunk inside any of them.
- `git diff HEAD~1 -- saturn/src/decode.c` shows exactly two added lines — one `extern`, one assignment — and no `#ifdef` and no new `#include`.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menus/menu.h saturn/src/menus/menu.c \
        saturn/src/input.h saturn/src/system/input_srl.cxx \
        saturn/src/decode.c saturn/src/main.c saturn/src/main.h
git commit -m "Wire the menus into the game loop: one gate on room 7 opens the sub-title menu after the intro and the load screen every time after, except when decode.c reports the credits have just played, the debug chord's call site becomes the pause menu's poll so the verified save point is kept, and the attract loop replays the opening cinematic after fifteen idle seconds."
```

---

## After the plan: the emulator run

Nothing above has run on hardware. Hand the build to the human and check these in order, because the first one can invalidate a design assumption:

1. **Play to a death.** Does the load screen appear? If not, deaths do not route through room 7 and a second hook after `play_death_animation` becomes a follow-up spec. This is the one thing in the design that could not be settled by reading.
2. `START GAME` begins a playable game. If it starts in the wrong place, change `MENU_START_ROOM`.
3. Text draws in front of the panel. If it vanishes, swap `MENU_ART_Z_PANEL` and `MENU_ART_Z_GLYPH`.
4. The sub-title menu's text is readable against the title card, and the lightning flash is a flicker rather than a strobe. `MENU_ART_FLASH_PERIOD` and `MENU_ART_FLASH_FRAMES` tune it.
5. `START GAME` sits where you wanted it. `MENU_TITLE_TEXT_X` and `MENU_TITLE_START_Y` move it.
6. Pause during play, then resume: the frame underneath must be exactly the one the menu covered.
7. Save, power cycle, load. The saves path is already hardware-verified; this checks the menu did not break it.
8. Leave the sub-title menu alone for fifteen seconds and watch the cinematic play and hand back cleanly, twice in a row — the second pass is what catches a VRAM or CRAM leak in the attract loop.
9. **The ending lands on the sub-title menu, not the load screen.** If reaching the credits legitimately is impractical, force it: set `ending_played = 1` from the pause menu's *Return to Title* path temporarily, or start a build with `next_script` forced to the room that runs the `load screen 17050` opcode. Then check the second half of the rule — start a game from that sub-title menu, die, and confirm the load screen comes back, which is what proves the flag is consumed rather than latched.
