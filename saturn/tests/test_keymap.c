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

static void test_reassigning_the_same_button_is_a_no_op(void)
{
    KeyMap m;
    KeyMap before;

    keymap_defaults(&m);
    before = m;

    expect_int("rebinding a row to the button it already holds reports no change",
               keymap_assign(&m, KEYMAP_ROW_RUN, PAD_A), 0);
    expect_int("and leaves the map bit-identical",
               memcmp(&m, &before, sizeof(m)), 0);
}

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

int main(void)
{
    test_defaults_match_the_current_hardwiring();
    test_apply_routes_each_button();
    test_shortcut_sets_both_globals();
    test_chord_emerges_from_remapped_buttons();
    test_assign_to_a_free_button();
    test_assign_swaps_with_the_row_that_held_it();
    test_swap_is_reversible();
    test_shortcut_may_not_steal_a_core_button();
    test_shortcut_takes_a_free_button_and_can_be_cleared();
    test_core_rows_refuse_none();
    test_swap_with_a_bound_shortcut_is_allowed();
    test_reassigning_the_same_button_is_a_no_op();
    test_round_trip();
    test_parse_refuses_damaged_entries();

    if (g_fail == 0) {
        printf("keymap: all tests passed\n");
        return 0;
    }
    printf("keymap: %d failure(s)\n", g_fail);
    return 1;
}
