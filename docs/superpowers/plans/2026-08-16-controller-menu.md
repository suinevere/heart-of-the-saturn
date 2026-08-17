# Controller Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the player reassign Run/Shoot/Shield, Whip and Jump to any of the Saturn pad's eight face and shoulder buttons, bind an optional single button that fires jump-forward, and keep the choice across a power cycle.

**Architecture:** A new pure `keymap` module turns a raw pad bitmask into the engine's `key_a`/`key_b`/`key_c`. `input_srl.cxx` becomes translation only. Every menu — pause, boot, and the new `CONTROLS` screen — re-bases onto raw pad state so a gameplay remap can never move a menu's confirm button. The mapping persists as a 16-byte `HOTA_CFG` backup RAM entry.

**Tech Stack:** C99 for pure modules (host-tested with gcc), C++ for SRL-facing backends (`.cxx`, never `.cpp` — the Makefile's pattern rules only match `.c` and `.cxx`), SaturnRingLib, `sh2eb-elf` via `compile.bat`.

**Spec:** `docs/superpowers/specs/2026-08-16-hota-saturn-controller-menu-design.md`

## Global Constraints

- **Every pure module compiles under `gcc -std=c99 -Wall -Wextra -Werror -O1 -g`.** `keymap.c` must not include `srl.hpp`, `sega_bup.h`, or any engine header.
- **The engine does not change.** `update_keys`, `decode.c`, `vm.c`, `quicksave`, `quickload`, `quicksave_sprites`, `quickload_sprites` stay byte-for-byte as they are. The only `main.c` edit in this plan is `boot_key_mask` (Task 4) and one init call (Task 8).
- **New C files use `.c`, new C++ files use `.cxx`.** The Makefile globs `src/` for both (`saturn/Makefile:93-94`) so **no Makefile edit is ever needed** for a new source file.
- **Every file gets the banner comment block** from `CLAUDE.md`: `Description`, `Author: suinevere`, `Dependencies`, `Globals`, `Params`, `Returns`, with `N/A` where a field does not apply. Every method and constant gets one too. Tests get a file banner only.
- **No comments inside functions.** Keep prose to a sentence.
- **Commit after every task.** One sentence, no body, no trailers, no mention of Claude or AI.
- **Defaults are A, B, C, NONE** — byte-identical play to the current build on a console with no saved config.
- Run the full host suite with `sh saturn/tests/run_tests.sh` (it is `set -e`, so it stops at the first failure).

---

## File Structure

**Created:**

| File | Responsibility |
|---|---|
| `saturn/src/keymap.h` | `PadButton`, `PAD_BIT_*`, `KeymapRow`, `KeyMap`, all declarations. Pure. |
| `saturn/src/keymap.c` | Defaults, apply, assign/swap, serialise/parse, the one active map. Pure. |
| `saturn/src/system/saturn_keymap.h` | Two-function backup RAM seam. |
| `saturn/src/system/saturn_keymap.cxx` | `HOTA_CFG` load and save over `saturn_backup.h`. |
| `saturn/tests/test_keymap.c` | Host tests for the whole pure module. |

**Modified:**

| File | Change |
|---|---|
| `saturn/src/input.h` | Add `input_raw_buttons()`, remove `input_menu_start()` |
| `saturn/src/system/input_srl.cxx` | Read all buttons into a mask; `keymap_apply` for A/B/C |
| `saturn/src/main.c:1244-1257` | `boot_key_mask` reads raw, not the mapped globals |
| `saturn/src/main.c:1561` | Call `saturn_keymap_load` after `saturn_saveslot_init` |
| `saturn/src/menus/menu_state.h` | `MENU_CONTROLS`, `MENU_ACT_SAVE_KEYMAP`, four `MenuState` fields, `MenuInput.captured`, `menu_state_enter_controls` |
| `saturn/src/menus/menu_state.c` | `step_controls`, three-row title, five-row pause |
| `saturn/src/menus/menu_layout.h` | Nothing new exported; geometry is private to the `.c` |
| `saturn/src/menus/menu_layout.c` | `build_controls`, third title row, fifth pause row |
| `saturn/src/menus/menu.c` | `previousRaw`, `captured`, `MENU_ACT_SAVE_KEYMAP`, raw `menu_key_mask` |
| `saturn/tests/run_tests.sh` | New `run_tests_keymap`; `keymap.c` added to two existing link lines |

---

### Task 1: keymap module — defaults and apply

The heart of the feature. Step 4's test is the one that proves jump-forward cannot be lost.

**Files:**
- Create: `saturn/src/keymap.h`, `saturn/src/keymap.c`
- Test: `saturn/tests/test_keymap.c`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: `PadButton` (`PAD_NONE`=0, `PAD_A`=1 … `PAD_R`=8); `PAD_BIT_A`=0x0001 … `PAD_BIT_R`=0x0080, `PAD_BIT_UP`=0x0100, `PAD_BIT_DOWN`=0x0200, `PAD_BIT_LEFT`=0x0400, `PAD_BIT_RIGHT`=0x0800, `PAD_BIT_START`=0x1000; `KeymapRow` (`KEYMAP_ROW_RUN`=0, `KEYMAP_ROW_WHIP`=1, `KEYMAP_ROW_JUMP`=2, `KEYMAP_ROW_FORWARD`=3, `KEYMAP_ROW_COUNT`=4); `typedef struct { PadButton row[KEYMAP_ROW_COUNT]; } KeyMap;`; `unsigned int keymap_button_bit(PadButton)`; `void keymap_defaults(KeyMap *)`; `void keymap_apply(const KeyMap *, unsigned int raw, int *a, int *b, int *c)`; `const KeyMap *keymap_active(void)`; `void keymap_set_active(const KeyMap *)`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_keymap.c`:

```c
/*----------------------
 | test_keymap.c
 | Description: Host unit tests for keymap.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: keymap.h, stdio.h, string.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "keymap.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void test_defaults_match_the_current_hardwiring(void)
{
    KeyMap m;

    keymap_defaults(&m);
    expect_int("run defaults to A",      (int)m.row[KEYMAP_ROW_RUN],     (int)PAD_A);
    expect_int("whip defaults to B",     (int)m.row[KEYMAP_ROW_WHIP],    (int)PAD_B);
    expect_int("jump defaults to C",     (int)m.row[KEYMAP_ROW_JUMP],    (int)PAD_C);
    expect_int("shortcut defaults off",  (int)m.row[KEYMAP_ROW_FORWARD], (int)PAD_NONE);
}

static void test_apply_routes_each_button(void)
{
    KeyMap m;
    int a, b, c;

    keymap_defaults(&m);

    keymap_apply(&m, PAD_BIT_A, &a, &b, &c);
    expect_int("A sets key_a", a, 1);
    expect_int("A leaves key_b", b, 0);
    expect_int("A leaves key_c", c, 0);

    keymap_apply(&m, PAD_BIT_B, &a, &b, &c);
    expect_int("B sets key_b", b, 1);
    expect_int("B leaves key_a", a, 0);

    keymap_apply(&m, PAD_BIT_C, &a, &b, &c);
    expect_int("C sets key_c", c, 1);
    expect_int("C leaves key_a", a, 0);

    keymap_apply(&m, 0, &a, &b, &c);
    expect_int("nothing held leaves key_a", a, 0);
    expect_int("nothing held leaves key_b", b, 0);
    expect_int("nothing held leaves key_c", c, 0);

    keymap_apply(&m, PAD_BIT_X | PAD_BIT_START | PAD_BIT_UP, &a, &b, &c);
    expect_int("unbound buttons set nothing", a + b + c, 0);
}

static void test_shortcut_sets_both_globals(void)
{
    KeyMap m;
    int a, b, c;

    keymap_defaults(&m);
    m.row[KEYMAP_ROW_FORWARD] = PAD_Z;

    keymap_apply(&m, PAD_BIT_Z, &a, &b, &c);
    expect_int("shortcut sets key_a", a, 1);
    expect_int("shortcut sets key_c", c, 1);
    expect_int("shortcut leaves key_b", b, 0);
}

static void test_chord_emerges_from_remapped_buttons(void)
{
    KeyMap m;
    int a, b, c;

    keymap_defaults(&m);
    m.row[KEYMAP_ROW_RUN]  = PAD_X;
    m.row[KEYMAP_ROW_JUMP] = PAD_Y;

    keymap_apply(&m, PAD_BIT_X | PAD_BIT_Y, &a, &b, &c);
    expect_int("remapped run and jump together set key_a", a, 1);
    expect_int("remapped run and jump together set key_c", c, 1);

    keymap_apply(&m, PAD_BIT_X, &a, &b, &c);
    expect_int("remapped run alone leaves key_c", c, 0);
}

int main(void)
{
    test_defaults_match_the_current_hardwiring();
    test_apply_routes_each_button();
    test_shortcut_sets_both_globals();
    test_chord_emerges_from_remapped_buttons();

    if (g_fail == 0) {
        printf("keymap: all tests passed\n");
        return 0;
    }
    printf("keymap: %d failure(s)\n", g_fail);
    return 1;
}
```

Append to `saturn/tests/run_tests.sh`, after the `run_tests_menuclock` block:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_keymap test_keymap.c ../src/keymap.c
./run_tests_keymap
```

- [ ] **Step 2: Run test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `fatal error: keymap.h: No such file or directory`

- [ ] **Step 3: Write the header**

Create `saturn/src/keymap.h`:

```c
/*----------------------
 | keymap.h
 | Description: The gameplay button mapping, as pure data plus the four
 |   operations on it. No srl.hpp, no engine headers, no backup RAM -- the
 |   same discipline savedata.h and menu_state.h keep, and the reason the
 |   swap rule and the chord can be tested with host gcc.
 |
 |   PadButton exists so menu_state.c can handle a button identity without
 |   knowing it corresponds to an SRL::Input::Digital::Button. Only
 |   input_srl.cxx makes that connection.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef KEYMAP_H
#define KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | PadButton
 | Description: A bindable button, or PAD_NONE for an empty binding. The
 |   values are contiguous from 1 so keymap_button_bit is one shift, and
 |   PAD_NONE is 0 so a memset-zeroed MenuInput reports no capture.
 | Author: suinevere
 ----------------------*/
typedef enum {
    PAD_NONE = 0,
    PAD_A, PAD_B, PAD_C, PAD_X, PAD_Y, PAD_Z, PAD_L, PAD_R
} PadButton;

/*----------------------
 | PAD_BIT_*
 | Description: One bit per physical control on port 0, so a frame of raw pad
 |   state is a single unsigned int. The eight bindable buttons occupy bits
 |   0-7 in PadButton order, which is what lets keymap_button_bit shift
 |   rather than switch. The directions and Start are here because menus read
 |   this same mask, and are deliberately not bindable.
 | Author: suinevere
 ----------------------*/
#define PAD_BIT_A     0x0001u
#define PAD_BIT_B     0x0002u
#define PAD_BIT_C     0x0004u
#define PAD_BIT_X     0x0008u
#define PAD_BIT_Y     0x0010u
#define PAD_BIT_Z     0x0020u
#define PAD_BIT_L     0x0040u
#define PAD_BIT_R     0x0080u
#define PAD_BIT_UP    0x0100u
#define PAD_BIT_DOWN  0x0200u
#define PAD_BIT_LEFT  0x0400u
#define PAD_BIT_RIGHT 0x0800u
#define PAD_BIT_START 0x1000u

/*----------------------
 | KeymapRow
 | Description: The four bindable actions, in screen order. KEYMAP_ROW_FORWARD
 |   is the only one that may hold PAD_NONE: it is a shortcut for run and jump
 |   together, not the only route to the move, so losing it costs convenience
 |   and never the move itself.
 | Author: suinevere
 ----------------------*/
typedef enum {
    KEYMAP_ROW_RUN,
    KEYMAP_ROW_WHIP,
    KEYMAP_ROW_JUMP,
    KEYMAP_ROW_FORWARD,
    KEYMAP_ROW_COUNT
} KeymapRow;

/*----------------------
 | KeyMap
 | Description: One binding per row. No two rows may hold the same non-NONE
 |   button; keymap_assign and keymap_parse are the only things that write
 |   one, and both enforce that.
 | Author: suinevere
 ----------------------*/
typedef struct {
    PadButton row[KEYMAP_ROW_COUNT];
} KeyMap;

/*----------------------
 | keymap_button_bit
 | Description: The PAD_BIT_* for a button.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- the button
 | Returns: its mask bit, or 0 for PAD_NONE -- which makes an unbound row
 |          test as never held without a special case at the call site
 ----------------------*/
unsigned int keymap_button_bit(PadButton b);

/*----------------------
 | keymap_defaults
 | Description: A, B, C and no shortcut -- exactly what input_srl.cxx
 |   hardwired before this module existed, so a console with no stored config
 |   plays identically to the previous build.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- map to fill in
 | Returns: N/A
 ----------------------*/
void keymap_defaults(KeyMap *m);

/*----------------------
 | keymap_apply
 | Description: Turns a frame of raw pad state into the three face-button key
 |   globals the engine reads.
 |
 |   There is no chord to implement. The engine reads level state from
 |   independent globals, so run and jump held together is jump forward
 |   whatever the two buttons are -- this function reproduces it by writing
 |   each global from its own binding and nothing else. The shortcut row is
 |   the only special case, and it only ever sets bits.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the mapping; raw -- PAD_BIT_* mask of everything held; a, b, c
 |         -- filled in with 0 or 1
 | Returns: N/A
 ----------------------*/
void keymap_apply(const KeyMap *m, unsigned int raw, int *a, int *b, int *c);

/*----------------------
 | keymap_active
 | Description: The mapping check_events reads. Defaults until something calls
 |   keymap_set_active, so a caller that never loads a config still gets a
 |   playable pad.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_active, g_activeInit
 | Params: N/A
 | Returns: the live mapping, borrowed -- callers copy it, never keep it
 ----------------------*/
const KeyMap *keymap_active(void);

/*----------------------
 | keymap_set_active
 | Description: Replaces the live mapping. The controls screen edits its own
 |   copy and calls this once on the way out, so cancelling cannot leave the
 |   player with controls they did not confirm.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_active, g_activeInit
 | Params: m -- the mapping to install, copied
 | Returns: N/A
 ----------------------*/
void keymap_set_active(const KeyMap *m);

#ifdef __cplusplus
}
#endif

#endif /* KEYMAP_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/keymap.c`:

```c
/*----------------------
 | keymap.c
 | Description: The pure half of the controller menu: defaults, the raw-mask
 |   to key-global translation, and the one live mapping. The swap rule and
 |   the stored entry are in this file too, added by later tasks.
 |
 |   Design: docs/superpowers/specs/2026-08-16-hota-saturn-controller-menu-design.md
 | Author: suinevere
 | Dependencies: keymap.h
 ----------------------*/
#include "keymap.h"

/*----------------------
 | g_active / g_activeInit
 | Description: The mapping check_events reads, and whether it has been
 |   initialised. Lazily defaulted rather than statically, because a static
 |   initialiser would have to repeat the default in a second place.
 | Author: suinevere
 ----------------------*/
static KeyMap g_active;
static int    g_activeInit;

unsigned int keymap_button_bit(PadButton b)
{
    if (b == PAD_NONE) {
        return 0u;
    }
    return 1u << ((unsigned int)b - 1u);
}

void keymap_defaults(KeyMap *m)
{
    m->row[KEYMAP_ROW_RUN]     = PAD_A;
    m->row[KEYMAP_ROW_WHIP]    = PAD_B;
    m->row[KEYMAP_ROW_JUMP]    = PAD_C;
    m->row[KEYMAP_ROW_FORWARD] = PAD_NONE;
}

void keymap_apply(const KeyMap *m, unsigned int raw, int *a, int *b, int *c)
{
    unsigned int forward = keymap_button_bit(m->row[KEYMAP_ROW_FORWARD]);

    *a = (raw & keymap_button_bit(m->row[KEYMAP_ROW_RUN]))  ? 1 : 0;
    *b = (raw & keymap_button_bit(m->row[KEYMAP_ROW_WHIP])) ? 1 : 0;
    *c = (raw & keymap_button_bit(m->row[KEYMAP_ROW_JUMP])) ? 1 : 0;

    if (forward != 0u && (raw & forward) != 0u) {
        *a = 1;
        *c = 1;
    }
}

const KeyMap *keymap_active(void)
{
    if (!g_activeInit) {
        keymap_defaults(&g_active);
        g_activeInit = 1;
    }
    return &g_active;
}

void keymap_set_active(const KeyMap *m)
{
    g_active = *m;
    g_activeInit = 1;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: every existing binary still passes, then `keymap: all tests passed`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/keymap.h saturn/src/keymap.c saturn/tests/test_keymap.c saturn/tests/run_tests.sh
git commit -m "Add the pure keymap module: defaults matching the current hardwiring, and a raw-mask to key-global translation in which jump forward needs no implementation because run and jump held together already produce it whatever buttons they are bound to."
```

---

### Task 2: The swap rule and the one capture it rejects

**Files:**
- Modify: `saturn/src/keymap.h`, `saturn/src/keymap.c`
- Test: `saturn/tests/test_keymap.c`

**Interfaces:**
- Consumes: `KeyMap`, `KeymapRow`, `PadButton`, `keymap_defaults` from Task 1.
- Produces: `int keymap_assign(KeyMap *m, KeymapRow row, PadButton b)` — returns 1 if the map changed, 0 if the assignment was refused.

- [ ] **Step 1: Write the failing test**

Add to `saturn/tests/test_keymap.c`, above `main`:

```c
static void test_assign_to_a_free_button(void)
{
    KeyMap m;

    keymap_defaults(&m);
    expect_int("binding jump to a free button is accepted",
               keymap_assign(&m, KEYMAP_ROW_JUMP, PAD_Z), 1);
    expect_int("jump is now Z", (int)m.row[KEYMAP_ROW_JUMP], (int)PAD_Z);
    expect_int("run is untouched", (int)m.row[KEYMAP_ROW_RUN], (int)PAD_A);
}

static void test_assign_swaps_with_the_row_that_held_it(void)
{
    KeyMap m;

    keymap_defaults(&m);
    expect_int("binding whip to A is accepted",
               keymap_assign(&m, KEYMAP_ROW_WHIP, PAD_A), 1);
    expect_int("whip took A",           (int)m.row[KEYMAP_ROW_WHIP], (int)PAD_A);
    expect_int("run took whip's old B", (int)m.row[KEYMAP_ROW_RUN],  (int)PAD_B);
    expect_int("jump is untouched",     (int)m.row[KEYMAP_ROW_JUMP], (int)PAD_C);
}

static void test_swap_is_reversible(void)
{
    KeyMap m;

    keymap_defaults(&m);
    keymap_assign(&m, KEYMAP_ROW_WHIP, PAD_A);
    keymap_assign(&m, KEYMAP_ROW_WHIP, PAD_B);
    expect_int("whip is back on B", (int)m.row[KEYMAP_ROW_WHIP], (int)PAD_B);
    expect_int("run is back on A",  (int)m.row[KEYMAP_ROW_RUN],  (int)PAD_A);
}

static void test_shortcut_may_not_steal_a_core_button(void)
{
    KeyMap m;
    KeyMap before;

    keymap_defaults(&m);
    before = m;

    expect_int("a swap that would unbind run is refused",
               keymap_assign(&m, KEYMAP_ROW_FORWARD, PAD_A), 0);
    expect_int("the map is bit-identical after a refusal",
               memcmp(&m, &before, sizeof(m)), 0);
}

static void test_shortcut_takes_a_free_button_and_can_be_cleared(void)
{
    KeyMap m;

    keymap_defaults(&m);
    expect_int("the shortcut accepts a free button",
               keymap_assign(&m, KEYMAP_ROW_FORWARD, PAD_Z), 1);
    expect_int("the shortcut is Z", (int)m.row[KEYMAP_ROW_FORWARD], (int)PAD_Z);

    expect_int("the shortcut accepts NONE",
               keymap_assign(&m, KEYMAP_ROW_FORWARD, PAD_NONE), 1);
    expect_int("the shortcut is clear",
               (int)m.row[KEYMAP_ROW_FORWARD], (int)PAD_NONE);
}

static void test_core_rows_refuse_none(void)
{
    KeyMap m;

    keymap_defaults(&m);
    expect_int("run refuses NONE", keymap_assign(&m, KEYMAP_ROW_RUN, PAD_NONE), 0);
    expect_int("run kept A",       (int)m.row[KEYMAP_ROW_RUN], (int)PAD_A);
}

static void test_swap_with_a_bound_shortcut_is_allowed(void)
{
    KeyMap m;

    keymap_defaults(&m);
    keymap_assign(&m, KEYMAP_ROW_FORWARD, PAD_Z);

    expect_int("run may take the shortcut's button",
               keymap_assign(&m, KEYMAP_ROW_RUN, PAD_Z), 1);
    expect_int("run is Z",                (int)m.row[KEYMAP_ROW_RUN],     (int)PAD_Z);
    expect_int("the shortcut took run's A", (int)m.row[KEYMAP_ROW_FORWARD], (int)PAD_A);
}
```

Call all seven from `main` before the result print:

```c
    test_assign_to_a_free_button();
    test_assign_swaps_with_the_row_that_held_it();
    test_swap_is_reversible();
    test_shortcut_may_not_steal_a_core_button();
    test_shortcut_takes_a_free_button_and_can_be_cleared();
    test_core_rows_refuse_none();
    test_swap_with_a_bound_shortcut_is_allowed();
```

- [ ] **Step 2: Run test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `implicit declaration of function 'keymap_assign'`, escalated to an error by `-Werror`

- [ ] **Step 3: Declare it**

Add to `saturn/src/keymap.h`, after `keymap_apply`:

```c
/*----------------------
 | keymap_assign
 | Description: Binds a button to a row, swapping with whichever row already
 |   held it so that nothing is ever left unbound by accident and every
 |   capture is one reversible step.
 |
 |   Refuses exactly two things. A core row may not be set to PAD_NONE. And a
 |   swap may not hand a core row PAD_NONE, which is reachable only when the
 |   shortcut row is empty and the button picked for it belongs to a core row
 |   -- there the displaced row would have taken the shortcut's nothing and
 |   Run, Whip or Jump would have gone dead. The caller shows IN USE and the
 |   player moves the core row first, or picks a free button.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the mapping; row -- which action; b -- the button, or PAD_NONE
 |         to clear the shortcut row
 | Returns: 1 if the mapping changed, 0 if the assignment was refused and the
 |          mapping is untouched
 ----------------------*/
int keymap_assign(KeyMap *m, KeymapRow row, PadButton b);
```

- [ ] **Step 4: Write the implementation**

Add to `saturn/src/keymap.c`, after `keymap_apply`:

```c
int keymap_assign(KeyMap *m, KeymapRow row, PadButton b)
{
    PadButton previous;
    int q;

    if (b == PAD_NONE) {
        if (row != KEYMAP_ROW_FORWARD) {
            return 0;
        }
        m->row[row] = PAD_NONE;
        return 1;
    }

    previous = m->row[row];

    for (q = 0; q < KEYMAP_ROW_COUNT; q++) {
        if (q == (int)row || m->row[q] != b) {
            continue;
        }
        if (previous == PAD_NONE) {
            return 0;
        }
        m->row[q] = previous;
        break;
    }

    m->row[row] = b;
    return 1;
}
```

`previous == PAD_NONE` inside that loop is reachable only when `row` is the shortcut row, because the three core rows can never hold `PAD_NONE` — which is what makes the single test sufficient for the whole rejection rule.

- [ ] **Step 5: Run tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: `keymap: all tests passed`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/keymap.h saturn/src/keymap.c saturn/tests/test_keymap.c
git commit -m "Resolve a binding collision by swapping the two rows, and refuse outright the one swap that would hand a core row nothing and leave run, whip or jump with no button at all."
```

---

### Task 3: The stored entry

**Files:**
- Modify: `saturn/src/keymap.h`, `saturn/src/keymap.c`
- Test: `saturn/tests/test_keymap.c`

**Interfaces:**
- Consumes: everything from Tasks 1 and 2.
- Produces: `KEYMAP_ENTRY_BYTES` (16), `KEYMAP_FORMAT_VERSION` (1), `void keymap_serialise(const KeyMap *m, unsigned char *buf)`, `int keymap_parse(KeyMap *m, const unsigned char *buf, int len)` returning 1 on success.

- [ ] **Step 1: Write the failing test**

Add to `saturn/tests/test_keymap.c`:

```c
static void test_round_trip(void)
{
    KeyMap out;
    KeyMap in;
    unsigned char buf[KEYMAP_ENTRY_BYTES];

    keymap_defaults(&out);
    keymap_assign(&out, KEYMAP_ROW_JUMP, PAD_X);
    keymap_assign(&out, KEYMAP_ROW_FORWARD, PAD_Z);

    keymap_serialise(&out, buf);
    memset(&in, 0, sizeof(in));

    expect_int("a serialised map parses", keymap_parse(&in, buf, sizeof(buf)), 1);
    expect_int("the round trip is exact", memcmp(&in, &out, sizeof(in)), 0);
}

static void test_parse_refuses_damaged_entries(void)
{
    KeyMap m;
    KeyMap untouched;
    unsigned char buf[KEYMAP_ENTRY_BYTES];
    unsigned char bad[KEYMAP_ENTRY_BYTES];

    keymap_defaults(&m);
    keymap_serialise(&m, buf);
    keymap_defaults(&untouched);

    expect_int("a short buffer is refused",
               keymap_parse(&m, buf, KEYMAP_ENTRY_BYTES - 1), 0);

    memcpy(bad, buf, sizeof(bad));
    bad[0] = 'X';
    expect_int("a bad magic is refused", keymap_parse(&m, bad, sizeof(bad)), 0);

    memcpy(bad, buf, sizeof(bad));
    bad[4] = KEYMAP_FORMAT_VERSION + 1;
    expect_int("an unknown version is refused",
               keymap_parse(&m, bad, sizeof(bad)), 0);

    memcpy(bad, buf, sizeof(bad));
    bad[6] = (unsigned char)(PAD_R + 1);
    expect_int("an out-of-range button is refused",
               keymap_parse(&m, bad, sizeof(bad)), 0);

    memcpy(bad, buf, sizeof(bad));
    bad[7] = bad[6];
    expect_int("a duplicate binding is refused",
               keymap_parse(&m, bad, sizeof(bad)), 0);

    memcpy(bad, buf, sizeof(bad));
    bad[6] = (unsigned char)PAD_NONE;
    expect_int("an unbound core row is refused",
               keymap_parse(&m, bad, sizeof(bad)), 0);

    expect_int("every refusal left the map alone",
               memcmp(&m, &untouched, sizeof(m)), 0);
}
```

Call both from `main`:

```c
    test_round_trip();
    test_parse_refuses_damaged_entries();
```

- [ ] **Step 2: Run test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `'KEYMAP_ENTRY_BYTES' undeclared`

- [ ] **Step 3: Declare it**

Add to `saturn/src/keymap.h`, after the `KeyMap` struct:

```c
/*----------------------
 | KEYMAP_ENTRY_BYTES / KEYMAP_FORMAT_VERSION
 | Description: The size of the stored entry and the format version it must
 |   carry, following savedata.h's convention. Sixteen bytes for ten used and
 |   six of room, because the backup RAM cost of the slack is nothing next to
 |   a format bump.
 | Author: suinevere
 ----------------------*/
#define KEYMAP_ENTRY_BYTES    16
#define KEYMAP_FORMAT_VERSION 1
```

And after `keymap_assign`:

```c
/*----------------------
 | keymap_serialise
 | Description: Packs a mapping into KEYMAP_ENTRY_BYTES. Layout: 0 magic
 |   'HOTC', 4 version, 5 reserved, 6..9 the four bindings, 10..15 zero.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the mapping; buf -- destination, must hold KEYMAP_ENTRY_BYTES
 | Returns: N/A
 ----------------------*/
void keymap_serialise(const KeyMap *m, unsigned char *buf);

/*----------------------
 | keymap_parse
 | Description: Reads a stored entry, validating everything before it writes
 |   anything: the magic, the version, the length, every button's range, that
 |   no two rows share a button, and that no core row is unbound. A corrupt
 |   config must never be worse than a fresh one, so the caller's response to
 |   a 0 is keymap_defaults.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- filled in only on success; buf -- the stored bytes; len --
 |         how many there are
 | Returns: 1 if the entry was valid and m was written, 0 otherwise with m
 |          untouched
 ----------------------*/
int keymap_parse(KeyMap *m, const unsigned char *buf, int len);
```

- [ ] **Step 4: Write the implementation**

Add `#include <string.h>` under `#include "keymap.h"` in `saturn/src/keymap.c`, then append:

```c
void keymap_serialise(const KeyMap *m, unsigned char *buf)
{
    int i;

    memset(buf, 0, KEYMAP_ENTRY_BYTES);
    buf[0] = 'H';
    buf[1] = 'O';
    buf[2] = 'T';
    buf[3] = 'C';
    buf[4] = KEYMAP_FORMAT_VERSION;

    for (i = 0; i < KEYMAP_ROW_COUNT; i++) {
        buf[6 + i] = (unsigned char)m->row[i];
    }
}

int keymap_parse(KeyMap *m, const unsigned char *buf, int len)
{
    KeyMap candidate;
    int i;
    int j;

    if (len < KEYMAP_ENTRY_BYTES) {
        return 0;
    }
    if (buf[0] != 'H' || buf[1] != 'O' || buf[2] != 'T' || buf[3] != 'C') {
        return 0;
    }
    if (buf[4] != KEYMAP_FORMAT_VERSION) {
        return 0;
    }

    for (i = 0; i < KEYMAP_ROW_COUNT; i++) {
        if (buf[6 + i] > (unsigned char)PAD_R) {
            return 0;
        }
        candidate.row[i] = (PadButton)buf[6 + i];
    }

    for (i = 0; i < KEYMAP_ROW_COUNT; i++) {
        if (i != KEYMAP_ROW_FORWARD && candidate.row[i] == PAD_NONE) {
            return 0;
        }
        for (j = i + 1; j < KEYMAP_ROW_COUNT; j++) {
            if (candidate.row[i] != PAD_NONE
                && candidate.row[i] == candidate.row[j]) {
                return 0;
            }
        }
    }

    *m = candidate;
    return 1;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: `keymap: all tests passed`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/keymap.h saturn/src/keymap.c saturn/tests/test_keymap.c
git commit -m "Store a mapping as a sixteen-byte entry carrying a magic and a format version, and validate the magic, the version, the length, every button's range, that no two rows share a button and that no core row is unbound before writing anything, so a damaged config degrades to the defaults instead of to an unplayable pad."
```

---

### Task 4: Raw pad state, and every menu re-based onto it

Pure refactor. With the default mapping installed, behaviour is byte-identical to today — that is the acceptance bar for this task.

**Files:**
- Modify: `saturn/src/input.h`, `saturn/src/system/input_srl.cxx`, `saturn/src/main.c:1235-1257`, `saturn/src/menus/menu.c:143-169`

**Interfaces:**
- Consumes: `keymap_apply`, `keymap_active`, `PAD_BIT_*` from Task 1.
- Produces: `unsigned int input_raw_buttons(void)`. Removes `int input_menu_start(void)`.

- [ ] **Step 1: Change the seam**

In `saturn/src/input.h`, delete the whole `input_menu_start` banner and declaration (lines 46-67) and put this in their place:

```c
/*----------------------
 | input_raw_buttons
 | Description: Port 0's physical button state this frame, before any
 |   mapping, as a PAD_BIT_* mask.
 |
 |   Exists because the key globals stopped being raw. keymap_apply writes
 |   key_a, key_b and key_c from whatever the player bound, and menu_key_mask
 |   used to read key_a as confirm and key_b as cancel -- so without this,
 |   binding Run to X would move the pause menu's confirm button to X too, and
 |   with it the screen that would let the player undo the change. The rule
 |   this seam exists to enforce is that the mapping writes the key globals
 |   and every menu reads raw.
 |
 |   Level, not edge, for the reason input_menu_start gave before it was
 |   folded in here: menu.c owns edge detection for every button it reads, and
 |   mixing the two is what let an earlier debug chord fire on the wrong
 |   frame. Start is PAD_BIT_START in the mask.
 |
 |   Saturn only. menu_pause_poll is behind HOTA_SATURN, which is why
 |   input_sdl.c never defined the chord this replaces either.
 | Author: suinevere
 | Dependencies: keymap.h
 | Globals: N/A
 | Params: N/A
 | Returns: the PAD_BIT_* mask of everything held, or 0 if no pad is connected
 ----------------------*/
unsigned int input_raw_buttons(void);
```

Add `#include "keymap.h"` above the `extern "C"` guard in `input.h`.

- [ ] **Step 2: Rewrite the Saturn backend**

In `saturn/src/system/input_srl.cxx`, add `#include "keymap.h"` beside `#include "input.h"`. Replace the body of `check_events` and the whole of `input_menu_start` with:

```c
void check_events(void)
{
	unsigned int raw = input_raw_buttons();

	key_up    = (raw & PAD_BIT_UP)    ? 1 : 0;
	key_down  = (raw & PAD_BIT_DOWN)  ? 1 : 0;
	key_left  = (raw & PAD_BIT_LEFT)  ? 1 : 0;
	key_right = (raw & PAD_BIT_RIGHT) ? 1 : 0;

	keymap_apply(keymap_active(), raw, &key_a, &key_b, &key_c);
}

extern "C" unsigned int input_raw_buttons(void)
{
	SRL::Input::Digital pad(0);
	unsigned int raw = 0;

	if (!pad.IsConnected())
	{
		return 0;
	}

	if (pad.IsHeld(SRL::Input::Digital::Button::A))     raw |= PAD_BIT_A;
	if (pad.IsHeld(SRL::Input::Digital::Button::B))     raw |= PAD_BIT_B;
	if (pad.IsHeld(SRL::Input::Digital::Button::C))     raw |= PAD_BIT_C;
	if (pad.IsHeld(SRL::Input::Digital::Button::X))     raw |= PAD_BIT_X;
	if (pad.IsHeld(SRL::Input::Digital::Button::Y))     raw |= PAD_BIT_Y;
	if (pad.IsHeld(SRL::Input::Digital::Button::Z))     raw |= PAD_BIT_Z;
	if (pad.IsHeld(SRL::Input::Digital::Button::L))     raw |= PAD_BIT_L;
	if (pad.IsHeld(SRL::Input::Digital::Button::R))     raw |= PAD_BIT_R;
	if (pad.IsHeld(SRL::Input::Digital::Button::Up))    raw |= PAD_BIT_UP;
	if (pad.IsHeld(SRL::Input::Digital::Button::Down))  raw |= PAD_BIT_DOWN;
	if (pad.IsHeld(SRL::Input::Digital::Button::Left))  raw |= PAD_BIT_LEFT;
	if (pad.IsHeld(SRL::Input::Digital::Button::Right)) raw |= PAD_BIT_RIGHT;
	if (pad.IsHeld(SRL::Input::Digital::Button::START)) raw |= PAD_BIT_START;

	return raw;
}
```

Returning 0 for a disconnected pad preserves the existing "mis-plugged pad reads as nothing pressed" behaviour that the file's banner argues for — `keymap_apply` on a zero mask clears all three globals, and the four directions clear above it.

Update the file banner: it currently says "this file maps seven buttons and nothing else" and describes `key_a`/`key_b`/`key_c` mapping by label. Replace those claims with the raw-mask and `keymap_apply` arrangement, and keep the `IsConnected`, port-0 and no-`RefreshPeripherals` paragraphs exactly as they are — all three are still load-bearing.

- [ ] **Step 3: Re-base the boot menu**

In `saturn/src/main.c`, replace `boot_key_mask` (lines 1244-1257) with:

```c
static uint32_t boot_key_mask(void)
{
	unsigned int raw = input_raw_buttons();
	uint32_t mask = 0;

	if (raw & PAD_BIT_UP)    mask |= BOOT_KEY_UP;
	if (raw & PAD_BIT_DOWN)  mask |= BOOT_KEY_DOWN;
	if (raw & PAD_BIT_LEFT)  mask |= BOOT_KEY_LEFT;
	if (raw & PAD_BIT_RIGHT) mask |= BOOT_KEY_RIGHT;
	if (raw & PAD_BIT_A)     mask |= BOOT_KEY_A;
	if (raw & PAD_BIT_B)     mask |= BOOT_KEY_B;
	if (raw & PAD_BIT_C)     mask |= BOOT_KEY_C;

	return mask;
}
```

`BOOT_KEY_SELECT` drops out: it was fed by `key_select`, which `input_srl.cxx` deliberately never writes, so it has always been dead on this platform. Update the banner's `Globals` line from the eight key globals to `N/A`, and its description to say the mask comes from raw pad state so the boot menu cannot be moved by a gameplay remap.

Add `#include "keymap.h"` to `main.c`'s includes if `input.h` does not already pull it in transitively — it does, but state the dependency in the banner.

- [ ] **Step 4: Re-base the pause menu**

In `saturn/src/menus/menu.c`, replace `menu_key_mask` (lines 155-169) with:

```c
static int menu_key_mask(void)
{
    unsigned int raw = input_raw_buttons();
    int mask = 0;

    if (raw & PAD_BIT_UP)    mask |= MENU_BIT_UP;
    if (raw & PAD_BIT_DOWN)  mask |= MENU_BIT_DOWN;
    if (raw & PAD_BIT_LEFT)  mask |= MENU_BIT_LEFT;
    if (raw & PAD_BIT_RIGHT) mask |= MENU_BIT_RIGHT;
    if (raw & PAD_BIT_A)     mask |= MENU_BIT_CONFIRM;
    if (raw & PAD_BIT_B)     mask |= MENU_BIT_CANCEL;
    if (raw & PAD_BIT_START) mask |= MENU_BIT_PAUSE;

    return mask;
}
```

Update its banner: the `Globals` line becomes `N/A`, and the description says confirm and cancel are physical A and B and stay physical A and B however the player remaps the game. Update the `input_menu_start` mention in the file banner at `menu.c:10` to name `input_raw_buttons` instead.

- [ ] **Step 5: Build and verify no behaviour changed**

Run: `cd saturn && ./compile.bat`
Expected: builds clean; `BuildDrop/` produces `.elf`, `.iso`, `.bin`, `.cue`.

Run: `sh saturn/tests/run_tests.sh`
Expected: all pass — no pure module changed, this is a link-level check that nothing broke.

Boot the image in Mednafen and confirm: the boot menu still navigates and selects; the game still responds to A, B and C exactly as before; Start still opens the pause menu; the pause menu still confirms on A and cancels on B.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/input.h saturn/src/system/input_srl.cxx saturn/src/main.c saturn/src/menus/menu.c
git commit -m "Read port zero as one raw button mask and route the three face-button key globals through the mapping, re-basing the boot menu and the pause menu onto that raw mask so that confirm and cancel stay physical A and B however the player remaps the game and no mapping can hide the screen that would undo it."
```

---

### Task 5: The CONTROLS screen in the pure state machine

**Files:**
- Modify: `saturn/src/menus/menu_state.h`, `saturn/src/menus/menu_state.c`, `saturn/tests/run_tests.sh`
- Test: `saturn/tests/test_menu_state.c`

**Interfaces:**
- Consumes: `KeyMap`, `KeymapRow`, `PadButton`, `keymap_assign`, `keymap_defaults`, `keymap_active` from Tasks 1-3.
- Produces: `MENU_CONTROLS` in `MenuScreen`; `MENU_ACT_SAVE_KEYMAP` in `MenuAction`; `MenuState` fields `KeyMap map`, `int capturing`, `int mapDirty`, `int mapRejected`; `MenuInput` field `PadButton captured`; `void menu_state_enter_controls(MenuState *st, MenuScreen back)`; `MENU_CONTROLS_ROWS` = 6, `MENU_CONTROLS_ROW_RESET` = 4, `MENU_CONTROLS_ROW_BACK` = 5.

- [ ] **Step 1: Write the failing test**

Add to `saturn/tests/test_menu_state.c`:

```c
static void enter_controls_from_pause(MenuState *st)
{
    MenuInput in;
    int i;

    memset(st, 0, sizeof(*st));
    menu_state_enter_pause(st);

    for (i = 0; i < 3; i++) {
        in = none();
        in.down = 1;
        menu_state_step(st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(st, &in);
}

static void test_pause_has_a_controls_row(void)
{
    MenuState st;

    enter_controls_from_pause(&st);
    expect_int("pause row 3 opens controls", (int)st.screen, (int)MENU_CONTROLS);
    expect_int("controls opens on the first binding row", st.cursor, 0);
    expect_int("controls opens not capturing", st.capturing, -1);
    expect_int("controls remembers where it came from",
               (int)st.returnScreen, (int)MENU_PAUSE);
}

static void test_pause_return_to_title_moved_to_row_four(void)
{
    MenuState st;
    MenuInput in;
    int i;

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);

    for (i = 0; i < 4; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("pause row 4 is still return to title",
               (int)st.screen, (int)MENU_CONFIRM);
}

static void test_pause_cursor_wraps_over_five_rows(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from row 0 wraps to row 4", st.cursor, 4);
}

static void test_title_has_a_controls_row(void)
{
    MenuState st;
    MenuInput in;

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);

    in = none();
    in.up = 1;
    menu_state_step(&st, &in);
    expect_int("up from row 0 wraps to row 2", st.cursor, 2);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("title row 2 opens controls", (int)st.screen, (int)MENU_CONTROLS);
    expect_int("and cancels back to the title",
               (int)st.returnScreen, (int)MENU_TITLE);
}

static void test_capture_binds_a_button(void)
{
    MenuState st;
    MenuInput in;

    enter_controls_from_pause(&st);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("confirm on a binding row starts a capture", st.capturing, 0);

    in = none();
    in.captured = PAD_X;
    menu_state_step(&st, &in);
    expect_int("the capture ends", st.capturing, -1);
    expect_int("run is now X", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_X);
    expect_int("the map is dirty", st.mapDirty, 1);
}

static void test_start_aborts_a_capture(void)
{
    MenuState st;
    MenuInput in;

    enter_controls_from_pause(&st);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);

    in = none();
    in.pause = 1;
    menu_state_step(&st, &in);
    expect_int("start ends the capture", st.capturing, -1);
    expect_int("run is untouched", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_A);
    expect_int("the map is clean", st.mapDirty, 0);
}

static void test_a_refused_capture_reports_in_use(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_controls_from_pause(&st);

    for (i = 0; i < KEYMAP_ROW_FORWARD; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);

    in = none();
    in.captured = PAD_A;
    menu_state_step(&st, &in);
    expect_int("the capture ends", st.capturing, -1);
    expect_int("the shortcut is still clear",
               (int)st.map.row[KEYMAP_ROW_FORWARD], (int)PAD_NONE);
    expect_int("run kept A", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_A);
    expect_int("the screen is told to say IN USE", st.mapRejected, 1);
    expect_int("the map is clean", st.mapDirty, 0);
}

static void test_left_clears_the_shortcut(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_controls_from_pause(&st);

    for (i = 0; i < KEYMAP_ROW_FORWARD; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.captured = PAD_Z;
    menu_state_step(&st, &in);
    expect_int("the shortcut is Z", (int)st.map.row[KEYMAP_ROW_FORWARD], (int)PAD_Z);

    in = none();
    in.left = 1;
    menu_state_step(&st, &in);
    expect_int("left clears the shortcut",
               (int)st.map.row[KEYMAP_ROW_FORWARD], (int)PAD_NONE);
}

static void test_reset_row_restores_defaults(void)
{
    MenuState st;
    MenuInput in;
    int i;

    enter_controls_from_pause(&st);

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.captured = PAD_X;
    menu_state_step(&st, &in);

    for (i = 0; i < MENU_CONTROLS_ROW_RESET; i++) {
        in = none();
        in.down = 1;
        menu_state_step(&st, &in);
    }

    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    expect_int("reset restores run to A", (int)st.map.row[KEYMAP_ROW_RUN], (int)PAD_A);
    expect_int("reset stays on the screen", (int)st.screen, (int)MENU_CONTROLS);
}

static void test_back_saves_only_when_the_map_changed(void)
{
    MenuState st;
    MenuInput in;

    enter_controls_from_pause(&st);

    in = none();
    in.cancel = 1;
    expect_int("an unchanged map does not ask to be saved",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_NONE);
    expect_int("cancel returns to pause", (int)st.screen, (int)MENU_PAUSE);

    enter_controls_from_pause(&st);
    in = none();
    in.confirm = 1;
    menu_state_step(&st, &in);
    in = none();
    in.captured = PAD_X;
    menu_state_step(&st, &in);

    in = none();
    in.cancel = 1;
    expect_int("a changed map asks to be saved",
               (int)menu_state_step(&st, &in), (int)MENU_ACT_SAVE_KEYMAP);
    expect_int("and returns to pause", (int)st.screen, (int)MENU_PAUSE);
}
```

Call all eleven from `main`, and change the `run_tests_menustate` line in `saturn/tests/run_tests.sh` to link the new module:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system -I../src/menus \
    -o run_tests_menustate test_menu_state.c ../src/menus/menu_state.c \
       ../src/keymap.c
./run_tests_menustate
```

- [ ] **Step 2: Run test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `'MENU_CONTROLS' undeclared`

- [ ] **Step 3: Extend the header**

In `saturn/src/menus/menu_state.h`, add `#include "keymap.h"` beside the `savedata.h` include, then:

- add `MENU_CONTROLS` to `MenuScreen`, after `MENU_CONFIRM`;
- add `MENU_ACT_SAVE_KEYMAP` to `MenuAction`, after `MENU_ACT_RESCAN_SLOTS`, with its banner noting the caller must install `st->map` as active and persist it;
- add `PadButton captured;` to `MenuInput`, and extend that struct's banner: it is the button that became held this frame, `PAD_NONE` for none, and unlike the other fields it names a specific button rather than a role, because a capture has to know which one;
- add to `MenuState`: `KeyMap map; int capturing; int mapDirty; int mapRejected;`, and extend its banner — `map` is the screen's working copy and never the live mapping, `capturing` is the `KeymapRow` being captured or −1, `mapDirty` gates the save on the way out, `mapRejected` asks the layout for `IN USE`;
- add the row constants and the entry point:

```c
/*----------------------
 | MENU_CONTROLS_ROWS / MENU_CONTROLS_ROW_RESET / MENU_CONTROLS_ROW_BACK
 | Description: Six rows: the four bindings in KeymapRow order, then reset,
 |   then back. The bindings share KeymapRow's numbering on purpose, so a
 |   cursor below KEYMAP_ROW_COUNT is a row index and needs no table.
 | Author: suinevere
 ----------------------*/
#define MENU_CONTROLS_ROWS       6
#define MENU_CONTROLS_ROW_RESET  4
#define MENU_CONTROLS_ROW_BACK   5

/*----------------------
 | menu_state_enter_controls
 | Description: Opens the controls screen on the first binding row, taking a
 |   working copy of the live mapping. The copy is what makes cancelling safe:
 |   nothing the player does here reaches the pad until they leave and the
 |   caller installs it.
 | Author: suinevere
 | Dependencies: keymap.h
 | Globals: N/A
 | Params: st -- state; back -- the screen a cancel should return to
 | Returns: N/A
 ----------------------*/
void menu_state_enter_controls(MenuState *st, MenuScreen back);
```

- [ ] **Step 4: Write the implementation**

In `saturn/src/menus/menu_state.c`, add `menu_state_enter_controls` after `menu_state_enter_slots`:

```c
void menu_state_enter_controls(MenuState *st, MenuScreen back)
{
    st->screen       = MENU_CONTROLS;
    st->cursor       = 0;
    st->capturing    = -1;
    st->mapDirty     = 0;
    st->mapRejected  = 0;
    st->map          = *keymap_active();
    st->returnScreen = back;
}
```

Add `leave_controls` and `step_controls` before `menu_state_step`, each with a banner:

```c
static MenuAction leave_controls(MenuState *st)
{
    st->screen = st->returnScreen;
    st->cursor = 0;

    if (st->mapDirty) {
        st->mapDirty = 0;
        return MENU_ACT_SAVE_KEYMAP;
    }
    return MENU_ACT_NONE;
}

static MenuAction step_controls(MenuState *st, const MenuInput *in)
{
    if (st->capturing >= 0) {
        if (in->pause) {
            st->capturing = -1;
            return MENU_ACT_NONE;
        }
        if (in->captured != PAD_NONE) {
            if (keymap_assign(&st->map, (KeymapRow)st->capturing, in->captured)) {
                st->mapDirty = 1;
            } else {
                st->mapRejected = 1;
            }
            st->capturing = -1;
        }
        return MENU_ACT_NONE;
    }

    if (in->up) {
        st->cursor = (st->cursor + MENU_CONTROLS_ROWS - 1) % MENU_CONTROLS_ROWS;
        st->mapRejected = 0;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % MENU_CONTROLS_ROWS;
        st->mapRejected = 0;
        return MENU_ACT_NONE;
    }
    if (in->left && st->cursor == KEYMAP_ROW_FORWARD) {
        if (keymap_assign(&st->map, KEYMAP_ROW_FORWARD, PAD_NONE)) {
            st->mapDirty = 1;
        }
        st->mapRejected = 0;
        return MENU_ACT_NONE;
    }
    if (in->cancel) {
        return leave_controls(st);
    }
    if (in->confirm) {
        st->mapRejected = 0;

        if (st->cursor < KEYMAP_ROW_COUNT) {
            st->capturing = st->cursor;
            return MENU_ACT_NONE;
        }
        if (st->cursor == MENU_CONTROLS_ROW_RESET) {
            keymap_defaults(&st->map);
            st->mapDirty = 1;
            return MENU_ACT_NONE;
        }
        return leave_controls(st);
    }
    return MENU_ACT_NONE;
}
```

`step_controls`' banner must say why Start is the only abort: every one of the eight bindable buttons has to be capturable including B, so B cannot also mean cancel here, and Start is already non-capturable for being the pause button.

Add `case MENU_CONTROLS: return step_controls(st, in);` to `menu_state_step`'s switch.

In `step_title`, replace `st->cursor = 1 - st->cursor;` with three-row arithmetic and add the new branch:

```c
    if (in->up) {
        st->cursor = (st->cursor + 2) % 3;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % 3;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        if (st->cursor == 0) {
            return MENU_ACT_START_GAME;
        }
        if (st->cursor == 1) {
            menu_state_enter_slots(st, 0, MENU_TITLE);
            return MENU_ACT_RESCAN_SLOTS;
        }
        menu_state_enter_controls(st, MENU_TITLE);
        return MENU_ACT_NONE;
    }
```

In `step_pause`, change both `% 4` to `% 5`, both `+ 3` to `+ 4`, and replace the confirm tail:

```c
        if (st->cursor == 3) {
            menu_state_enter_controls(st, MENU_PAUSE);
            return MENU_ACT_NONE;
        }
        st->screen = MENU_CONFIRM;
        st->pending = MENU_ACT_RETURN_TO_TITLE;
        st->confirmYes = 0;
        return MENU_ACT_NONE;
```

Update `step_title` and `step_pause` banners to name the new row.

- [ ] **Step 5: Run tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: all pass, including every pre-existing `test_menu_state.c` case — `MenuInput` grew a field but `none()` memsets to zero and `PAD_NONE` is 0.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/menus/menu_state.h saturn/src/menus/menu_state.c saturn/tests/test_menu_state.c saturn/tests/run_tests.sh
git commit -m "Add the controls screen to the state machine, reached from both the pause menu and the sub-title card, editing a working copy of the mapping so cancelling cannot change the pad, capturing a button on confirm with Start as the only abort because every bindable button including cancel's own has to be capturable, and asking to be saved on the way out only when something actually changed."
```

---

### Task 6: Drawing the CONTROLS screen

**Files:**
- Modify: `saturn/src/menus/menu_layout.c`, `saturn/tests/run_tests.sh`
- Test: `saturn/tests/test_menu_layout.c`

**Interfaces:**
- Consumes: `MenuState.map`, `.cursor`, `.capturing`, `.mapRejected`; `MENU_CONTROLS_ROWS`.
- Produces: nothing new in the header — `menu_layout_build` gains a `MENU_CONTROLS` case. Geometry constants stay private to the `.c`.

Reuses `MENU_PANEL_SLOTS`. `MENUPANS.ART` is 272×168 at (24, 28), so the interior after the 2 px border runs x 26..294, y 30..194. Six rows at 18 px from y=64 end at 164, and the hint line at 176 ends at 186 — both inside it. A fourth panel would have cost a new art asset, a `disc_manifest.h` entry, an art-tool run and a bump to `MENU_ART_PANEL_COUNT`, for geometry identical to this one.

- [ ] **Step 1: Write the failing test**

Add to `saturn/tests/test_menu_layout.c`. That file has `expect_int`, `expect_str`, `count_kind` and `ramp_of` but nothing that matches a run of text, so add this helper — the name is free.

**It must skip spaces.** `put_text` (`menu_layout.c:139-152`) emits no glyph for `' '` but still advances `x` by `MENU_GLYPH_W`, which the existing `test_no_spaces_are_emitted` case pins down. A helper that looked for a space glyph would fail on `JUMP FORWARD`, `RESET DEFAULTS`, `START CANCELS` and `IN USE`. The literal 8 below is `MENU_GLYPH_W`, which is private to `menu_layout.c`; the cell width is already fixed at 8 by `MENU_ROW_CHARS`' reasoning in `menu_layout.h`.

```c
static int has_text(const MenuItem *items, int n, int x, int y, const char *s)
{
    int i;
    int k;

    for (k = 0; s[k] != '\0'; k++) {
        int found = 0;

        if (s[k] == ' ') {
            continue;
        }
        for (i = 0; i < n; i++) {
            if (items[i].kind == MENU_ITEM_GLYPH
                && items[i].x == (short)(x + k * 8)
                && items[i].y == (short)y
                && items[i].id == (unsigned char)s[k]) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}
```

Then:

```c
static void test_controls_rows_and_values(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_controls(&st, MENU_PAUSE);

    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("the screen fits the command list", n <= MENU_LAYOUT_MAX_ITEMS, 1);
    expect_int("the run row is labelled",
               has_text(items, n, 44, 64, "RUN/SHOOT/SHIELD"), 1);
    expect_int("the run row shows A",   has_text(items, n, 220, 64,  "A"), 1);
    expect_int("the whip row shows B",  has_text(items, n, 220, 82,  "B"), 1);
    expect_int("the jump row shows C",  has_text(items, n, 220, 100, "C"), 1);
    expect_int("the shortcut shows NONE",
               has_text(items, n, 220, 118, "NONE"), 1);
    expect_int("the reset row is there",
               has_text(items, n, 44, 136, "RESET DEFAULTS"), 1);
    expect_int("the back row is there", has_text(items, n, 44, 154, "BACK"), 1);
}

static void test_controls_capture_prompt(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_controls(&st, MENU_PAUSE);
    st.capturing = KEYMAP_ROW_JUMP;

    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("the captured row prompts",
               has_text(items, n, 220, 100, "PRESS?"), 1);
    expect_int("and the hint says how to escape",
               has_text(items, n, 44, 176, "START CANCELS"), 1);
}

static void test_controls_reports_a_refusal(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    int n;

    memset(&st, 0, sizeof(st));
    menu_state_enter_controls(&st, MENU_PAUSE);
    st.cursor = KEYMAP_ROW_FORWARD;
    st.mapRejected = 1;

    n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
    expect_int("the row says IN USE", has_text(items, n, 220, 118, "IN USE"), 1);
}

static void test_controls_every_button_name_renders(void)
{
    MenuState st;
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    static const char *NAMES[] = { "A", "B", "C", "X", "Y", "Z", "L", "R" };
    int b;
    int n;

    for (b = PAD_A; b <= PAD_R; b++) {
        memset(&st, 0, sizeof(st));
        menu_state_enter_controls(&st, MENU_PAUSE);
        st.map.row[KEYMAP_ROW_FORWARD] = (PadButton)b;

        n = menu_layout_build(&st, 0, items, MENU_LAYOUT_MAX_ITEMS);
        expect_int("the shortcut renders its button name",
                   has_text(items, n, 220, 118, NAMES[b - PAD_A]), 1);
    }
}
```

Call all four from `main`, and add `../src/keymap.c` to the `run_tests_menulayout` link line in `saturn/tests/run_tests.sh`:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system -I../src/menus \
    -o run_tests_menulayout test_menu_layout.c stub_saturn_backup.c \
       ../src/menus/menu_layout.c ../src/menus/menu_state.c ../src/savedata.c \
       ../src/keymap.c
./run_tests_menulayout
```

- [ ] **Step 2: Run test to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `menu_layout_build` returns a zero-item list for `MENU_CONTROLS`, so `has_text` finds nothing.

- [ ] **Step 3: Add the geometry and the builder**

In `saturn/src/menus/menu_layout.c`, add beside the other screens' geometry blocks:

```c
/*----------------------
 | MENU_CTRL_*
 | Description: The controls screen's geometry, on MENU_PANEL_SLOTS. That
 |   panel is 272x168 at (24, 28), so its interior after the art tool's 2 px
 |   border is x 26..294 and y 30..194. Six rows at MENU_CTRL_ROW_DY from
 |   MENU_CTRL_ROW0_Y end at 164 and the hint ends at 186, both inside it.
 |
 |   MENU_CTRL_VALUE_X is where the longest value has to fit: "PRESS?" is six
 |   cells, ending at 220 + 48 = 268, inside 294. That is why the capture
 |   prompt is abbreviated here and spelled out on the hint line instead --
 |   "PRESS BUTTON" from this column would end at 316, off the panel and off
 |   the screen.
 | Author: suinevere
 ----------------------*/
#define MENU_CTRL_HEADING_Y 40
#define MENU_CTRL_LABEL_X   44
#define MENU_CTRL_VALUE_X   220
#define MENU_CTRL_CURSOR_X  32
#define MENU_CTRL_ROW0_Y    64
#define MENU_CTRL_ROW_DY    18
#define MENU_CTRL_HINT_Y    176
```

Add the button-name helper and the builder:

```c
/*----------------------
 | button_name
 | Description: The label for a binding.
 | Author: suinevere
 | Dependencies: keymap.h
 | Globals: N/A
 | Params: b -- the button
 | Returns: a static string, "NONE" for an empty binding
 ----------------------*/
static const char *button_name(PadButton b)
{
    switch (b) {
    case PAD_A: return "A";
    case PAD_B: return "B";
    case PAD_C: return "C";
    case PAD_X: return "X";
    case PAD_Y: return "Y";
    case PAD_Z: return "Z";
    case PAD_L: return "L";
    case PAD_R: return "R";
    default:    return "NONE";
    }
}

/*----------------------
 | build_controls
 | Description: Lays out the controls screen: a heading, four binding rows,
 |   reset, back, and a hint line that changes with what the player is doing.
 | Author: suinevere
 | Dependencies: keymap.h
 | Globals: N/A
 | Params: st -- state to lay out; out -- destination; cap -- its capacity;
 |         n -- running item count, advanced in place
 | Returns: N/A
 ----------------------*/
static void build_controls(const MenuState *st, MenuItem *out, int cap, int *n)
{
    static const char *LABELS[MENU_CONTROLS_ROWS] = {
        "RUN/SHOOT/SHIELD", "WHIP", "JUMP", "JUMP FORWARD",
        "RESET DEFAULTS", "BACK"
    };
    const char *hint;
    int i;
    int y;

    put_panel(out, cap, n, MENU_PANEL_SLOTS, MENU_SLOTS_PANEL_X,
              MENU_SLOTS_PANEL_Y);
    put_text(out, cap, n, centre_x("CONTROLS"), MENU_CTRL_HEADING_Y,
             "CONTROLS", MENU_RAMP_DIM);

    for (i = 0; i < MENU_CONTROLS_ROWS; i++) {
        y = MENU_CTRL_ROW0_Y + i * MENU_CTRL_ROW_DY;
        put_text(out, cap, n, MENU_CTRL_LABEL_X, y, LABELS[i],
                 st->cursor == i ? MENU_RAMP_SEL : MENU_RAMP_DIM);

        if (i >= KEYMAP_ROW_COUNT) {
            continue;
        }
        if (st->capturing == i) {
            put_text(out, cap, n, MENU_CTRL_VALUE_X, y, "PRESS?",
                     MENU_RAMP_SEL);
        } else if (st->mapRejected && st->cursor == i) {
            put_text(out, cap, n, MENU_CTRL_VALUE_X, y, "IN USE",
                     MENU_RAMP_SEL);
        } else {
            put_text(out, cap, n, MENU_CTRL_VALUE_X, y,
                     button_name(st->map.row[i]),
                     st->cursor == i ? MENU_RAMP_SEL : MENU_RAMP_DIM);
        }
    }

    put_text(out, cap, n, MENU_CTRL_CURSOR_X,
             MENU_CTRL_ROW0_Y + st->cursor * MENU_CTRL_ROW_DY, ">",
             MENU_RAMP_SEL);

    if (st->capturing >= 0) {
        hint = "START CANCELS";
    } else if (st->cursor == KEYMAP_ROW_FORWARD) {
        hint = "LEFT CLEARS";
    } else {
        hint = "";
    }

    if (hint[0] != '\0') {
        put_text(out, cap, n, MENU_CTRL_LABEL_X, MENU_CTRL_HINT_Y, hint,
                 MENU_RAMP_DIM);
    }
}
```

Add `case MENU_CONTROLS: build_controls(st, out, cap, &n); break;` to `menu_layout_build`'s switch.

Add the third title row to `build_title` — `CONTROLS` at y=193, following `MENU_TITLE_START_Y` 161 and `MENU_TITLE_LOAD_Y` 177 with the same 16 px step, and extend the cursor's ternary to three positions. Add `"CONTROLS"` as the fifth entry of `build_pause`'s `ROWS[]` in position 3, moving `"RETURN TO TITLE"` to 4, and change its loop bound from 4 to 5.

- [ ] **Step 4: Run tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/menus/menu_layout.c saturn/tests/test_menu_layout.c saturn/tests/run_tests.sh
git commit -m "Draw the controls screen on the slots panel rather than a fourth panel asset, since the geometry it needs is the one that panel already has, with the capture prompt abbreviated in the value column because the spelled-out form would run off the panel and the full wording moved to a hint line that also tells the player how to clear the shortcut."
```

---

### Task 7: Wiring the screen into `menu_run`

**Files:**
- Modify: `saturn/src/menus/menu.c`

**Interfaces:**
- Consumes: `MENU_ACT_SAVE_KEYMAP`, `MenuInput.captured`, `keymap_set_active`, `input_raw_buttons`.
- Produces: nothing exported. `saturn_keymap_save` is called here and stubbed out in Task 8; until then this task calls only `keymap_set_active`.

- [ ] **Step 1: Add the raw edge and the capture**

In `saturn/src/menus/menu.c`, add beside `menu_edges`:

```c
/*----------------------
 | first_pressed
 | Description: Which bindable button became held this frame.
 |
 |   A second edge is needed rather than the MENU_BIT_* one menu_run already
 |   computes, because that mask has no bit for X, Y, Z, L or R -- it carries
 |   roles, and a capture needs an identity. Ties go to PadButton order, which
 |   only matters if two buttons land in the same 16 ms and either answer
 |   would be defensible.
 | Author: suinevere
 | Dependencies: keymap.h
 | Globals: N/A
 | Params: rawPressed -- PAD_BIT_* mask of buttons that became held
 | Returns: the button, or PAD_NONE if none of the eight did
 ----------------------*/
static PadButton first_pressed(unsigned int rawPressed)
{
    int b;

    for (b = (int)PAD_A; b <= (int)PAD_R; b++) {
        if (rawPressed & keymap_button_bit((PadButton)b)) {
            return (PadButton)b;
        }
    }
    return PAD_NONE;
}
```

- [ ] **Step 2: Track the raw mask in `menu_run`**

Add to `menu_run`'s locals beside `previous`, `current` and `pressed`:

```c
    unsigned int previousRaw;
    unsigned int currentRaw;
    unsigned int pressedRaw;
```

At the prime before the loop (`menu.c:493-494`), after `previous = menu_key_mask();` add:

```c
    previousRaw = input_raw_buttons();
```

At the top of the loop (`menu.c:506-511`), after `previous = current;` add:

```c
        currentRaw = input_raw_buttons();
        pressedRaw = currentRaw & ~previousRaw;
        previousRaw = currentRaw;
```

and after `menu_edges(pressed, &in);`:

```c
        in.captured = first_pressed(pressedRaw);
```

Do the same at the two other re-prime points — the attract-mode `continue` at `menu.c:582` and any other `previous = menu_key_mask();` — adding `previousRaw = input_raw_buttons();` immediately after each, for the reason the existing re-prime exists: the player is very likely still holding the button that got them here.

- [ ] **Step 3: Handle the save action**

In `menu_run`'s action chain, after the `MENU_ACT_LOAD_SLOT` branch and before the `else if (action != MENU_ACT_NONE)` catch-all:

```c
        else if (action == MENU_ACT_SAVE_KEYMAP)
        {
            keymap_set_active(&st->map);
            err = saturn_keymap_save(&st->map);
            status = menu_layout_status_text(err, st->device);
            action = MENU_ACT_NONE;
        }
```

The branch must sit before the catch-all, or `MENU_ACT_SAVE_KEYMAP` breaks out of `menu_run` and closes the menu instead of returning to the screen the player came from.

For this task only, until Task 8 creates it, declare `saturn_keymap_save` as returning `SAT_BUP_OK`; Task 8 replaces the declaration with the real header include. If that split is awkward, do Task 8 first — the two have no other ordering constraint.

Add `#include "keymap.h"` to `menu.c`.

- [ ] **Step 4: Build**

Run: `cd saturn && ./compile.bat`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/menus/menu.c
git commit -m "Wire the controls screen into the menu loop with a second edge over the raw pad mask, because the mask the loop already tracks carries roles rather than identities and has no bit at all for the five buttons this feature exists to make useful."
```

---

### Task 8: Persisting the mapping

**Files:**
- Create: `saturn/src/system/saturn_keymap.h`, `saturn/src/system/saturn_keymap.cxx`
- Modify: `saturn/src/main.c:1561`, `saturn/src/menus/menu.c`

**Interfaces:**
- Consumes: `keymap_serialise`, `keymap_parse`, `keymap_defaults`, `keymap_set_active`, `KEYMAP_ENTRY_BYTES`; `saturn_backup.h`'s `SAT_BUP_INTERNAL`, `SAT_BUP_CART`, `SAT_BUP_OK`, `SAT_BUP_ERR_*` and its read/write/probe functions.
- Produces: `void saturn_keymap_load(void)`, `int saturn_keymap_save(const KeyMap *m)` returning a `SAT_BUP_*` code.

Read `saturn/src/system/saturn_backup.h` in full before writing this file and match the exact signatures of its read and write functions — this plan names the constants, which are verified, but not those prototypes.

- [ ] **Step 1: Write the header**

Create `saturn/src/system/saturn_keymap.h` with the banner conventions and:

```c
void saturn_keymap_load(void);
int  saturn_keymap_save(const KeyMap *m);
```

`saturn_keymap_load` returns nothing because there is no failure the caller can act on: every failure means the defaults, which `keymap_active` already gives. Its banner must say so.

- [ ] **Step 2: Write the implementation**

Create `saturn/src/system/saturn_keymap.cxx`. It stores a `KEYMAP_ENTRY_BYTES` entry named `HOTA_CFG`.

`saturn_keymap_load`: read from `SAT_BUP_INTERNAL`; on `SAT_BUP_ERR_NOT_FOUND` or `SAT_BUP_ERR_UNFORMAT`, try `SAT_BUP_CART`. On a successful read, `keymap_parse` it and `keymap_set_active` on a 1. Anything else leaves the active map at its default. The buffer is a `KEYMAP_ENTRY_BYTES` local, not an LWRAM allocation — sixteen bytes on the stack costs nothing and removes a failure mode.

`saturn_keymap_save`: `keymap_serialise`, write to `SAT_BUP_INTERNAL`, and only on `SAT_BUP_ERR_NO_SPACE`, `SAT_BUP_ERR_UNFORMAT` or `SAT_BUP_ERR_PROTECTED` retry on `SAT_BUP_CART`. Return the last code. If the entry already exists the write must overwrite it — check whether `saturn_backup.h`'s write refuses with `SAT_BUP_ERR_EXISTS` and delete first if so.

The banner must record why this is not part of `saturn_saveslot.cxx`: a slot is per-playthrough game state and a mapping is a per-console preference, so folding them together would mean a slot-format bump for a preference change.

- [ ] **Step 3: Load it at startup**

In `saturn/src/main.c`, immediately after the `saturn_saveslot_init()` check at line 1561:

```c
	saturn_keymap_load();
```

No error branch: a failed load means defaults, which is a playable pad.

- [ ] **Step 4: Use the real header in `menu.c`**

Replace Task 7's local declaration of `saturn_keymap_save` with `#include "saturn_keymap.h"`.

- [ ] **Step 5: Build**

Run: `cd saturn && ./compile.bat`
Expected: builds clean.

Run: `sh saturn/tests/run_tests.sh`
Expected: all pass — nothing pure changed.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/system/saturn_keymap.h saturn/src/system/saturn_keymap.cxx saturn/src/main.c saturn/src/menus/menu.c
git commit -m "Persist the mapping as its own sixteen-byte backup RAM entry preferring internal memory and falling back to a cart, kept out of the save slots because a slot is per-playthrough game state while a control mapping belongs to the console, and treating every read failure as the defaults so a missing or damaged config still leaves a playable pad."
```

---

### Task 9: Verification against the acceptance criteria

No code. This task runs the spec's ten criteria on the emulator and then on hardware, and fixes what fails.

**Files:**
- Modify: whatever fails.

- [ ] **Step 1: Build a release image**

Run: `cd saturn && ./compile.bat release`
Expected: `BuildDrop/` holds a fresh `.iso`/`.cue`.

- [ ] **Step 2: Work the list in Mednafen**

With no `HOTA_CFG` present, confirm criteria 1-8 from `docs/superpowers/specs/2026-08-16-hota-saturn-controller-menu-design.md`:

1. Play is identical to the previous build.
2. *CONTROLS* opens from the pause menu and the sub-title card, and cancel returns to whichever opened it.
3. Bind Jump to Z; Z jumps.
4. Run on X, Jump on Y, shortcut NONE; X+Y performs jump forward.
5. Shortcut on Z; Z alone performs jump forward.
6. Capturing a button another row holds swaps them; with the shortcut clear, capturing a core row's button for it changes nothing and shows `IN USE`.
7. Start aborts a capture with the row unchanged.
8. After binding Run to X, the pause menu still confirms on A, and the boot menu is unaffected.

- [ ] **Step 3: Check the two things only a real machine settles**

Criteria 9 and 10 need actual backup RAM:

9. Power-cycle and confirm the mapping survives; *RESET DEFAULTS*, then power-cycle, and confirm the defaults survive too.
10. With internal backup RAM unformatted and no cart, the screen still operates, reports the failure on the status line, and the mapping applies for the session.

- [ ] **Step 4: Check the two layout positions that were reasoned, not measured**

The pause panel is 168×96 from y=64, so its interior ends at 158 and the fifth row occupies 140-150. The title card's third row sits at y=193 over artwork rather than a panel. Both were derived from `tools/mkmenuart.py:52-56` rather than seen. Confirm on screen that the fifth pause row is inside the panel and the title's third row is legible against the art, and adjust `MENU_PAUSE_ROW_DY` or `MENU_TITLE_*` if either is wrong.

- [ ] **Step 5: Commit any fixes and update the spec's status**

Change the spec's `**Status:**` line from `Draft, pending review` to `Implemented`.

```bash
git add -A
git commit -m "Verify the controller menu against its ten acceptance criteria on emulator and hardware, and mark the design spec implemented."
```

---

## Self-Review

**Spec coverage.** Every section maps to a task: the four actions and the free chord to Task 1; the swap rule and its rejection to Task 2; the stored entry to Task 3; the raw-pad rule, `input_raw_buttons` and the removal of `input_menu_start` to Task 4; the screen, capture and the Start-only escape to Task 5; the layout and the panel-reuse decision to Task 6; the `menu_run` wiring to Task 7; `HOTA_CFG` and the internal-first device order to Task 8; all ten acceptance criteria to Task 9. The exclusion list needs no task by definition.

**Two deliberate deviations from the spec as first written**, both already corrected in the spec itself so the pair do not disagree: `CONTROLS` reuses `MENU_PANEL_SLOTS` instead of adding a fourth panel id, and Left on the shortcut row is the mechanism for clearing it to `NONE`.

**One thing this plan does not pin down.** Task 8 names `saturn_backup.h`'s constants, which are verified, but not its read and write prototypes, and it says so in the task. That is the only place an implementer must read a header before writing code rather than copying from here.
