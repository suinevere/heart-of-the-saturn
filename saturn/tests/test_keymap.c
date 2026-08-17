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
