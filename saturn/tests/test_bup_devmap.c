#include <stdio.h>
#include "bup_devmap.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void test_cartridge_fitted(void)
{
    const int present[3] = { 1, 1, 0 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("cart fitted internal", internalIdx, 0);
    expect_int("cart fitted cart", cartIdx, 1);
}

static void test_no_cartridge(void)
{
    const int present[3] = { 1, 0, 0 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("no cart internal", internalIdx, 0);
    expect_int("no cart cart", cartIdx, BUP_DEVMAP_NONE);
}

static void test_nothing_answers_still_names_an_internal(void)
{
    const int present[3] = { 0, 0, 0 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("nothing internal", internalIdx, 0);
    expect_int("nothing cart", cartIdx, BUP_DEVMAP_NONE);
}

static void test_does_not_assume_index_zero(void)
{
    const int present[3] = { 0, 1, 1 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("shifted internal", internalIdx, 1);
    expect_int("shifted cart", cartIdx, 2);
}

static void test_skips_a_gap_between_responders(void)
{
    const int present[3] = { 1, 0, 1 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("gap internal", internalIdx, 0);
    expect_int("gap cart", cartIdx, 2);
}

static void test_a_third_responder_is_ignored(void)
{
    const int present[4] = { 1, 1, 1, 1 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 4, &internalIdx, &cartIdx);

    expect_int("third internal", internalIdx, 0);
    expect_int("third cart", cartIdx, 1);
}

static void test_tolerates_a_short_table(void)
{
    const int present[1] = { 1 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 1, &internalIdx, &cartIdx);

    expect_int("short internal", internalIdx, 0);
    expect_int("short cart", cartIdx, BUP_DEVMAP_NONE);
}

int main(void)
{
    test_cartridge_fitted();
    test_no_cartridge();
    test_nothing_answers_still_names_an_internal();
    test_does_not_assume_index_zero();
    test_skips_a_gap_between_responders();
    test_a_third_responder_is_ignored();
    test_tolerates_a_short_table();

    if (g_fail != 0) {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_bup_devmap: all passed\n");
    return 0;
}
