/*----------------------
 | test_bup_devmap.c
 | Description: Host unit tests for bup_devmap.c: naming the responding BIOS
 |   backup device indices. The first two cases are the two states measured on
 |   real hardware. Built and run by run_tests.sh with the host gcc.
 | Author: suinevere
 | Dependencies: bup_devmap.h, stdio.h
 ----------------------*/
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

/*----------------------
 | test_cartridge_fitted
 | Description: Measured with a cartridge fitted -- BUP_Stat over indices 0..2
 |   answered OK / OK / BUP_NON.
 | Author: suinevere
 ----------------------*/
static void test_cartridge_fitted(void)
{
    const int present[3] = { 1, 1, 0 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("cart fitted internal", internalIdx, 0);
    expect_int("cart fitted cart", cartIdx, 1);
}

/*----------------------
 | test_no_cartridge
 | Description: Measured with no cartridge -- OK / BUP_NON / BUP_NON. This is
 |   the case that used to fail outright: the port addressed index 1 as its
 |   internal device, so with no cart there was no working save device at all.
 | Author: suinevere
 ----------------------*/
static void test_no_cartridge(void)
{
    const int present[3] = { 1, 0, 0 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("no cart internal", internalIdx, 0);
    expect_int("no cart cart", cartIdx, BUP_DEVMAP_NONE);
}

/*----------------------
 | test_nothing_answers_still_names_an_internal
 | Description: A machine where nothing answered still gets a usable internal
 |   index; the individual calls report the failure.
 | Author: suinevere
 ----------------------*/
static void test_nothing_answers_still_names_an_internal(void)
{
    const int present[3] = { 0, 0, 0 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("nothing internal", internalIdx, 0);
    expect_int("nothing cart", cartIdx, BUP_DEVMAP_NONE);
}

/*----------------------
 | test_does_not_assume_index_zero
 | Description: The search takes the first index that answers, whichever it is.
 | Author: suinevere
 ----------------------*/
static void test_does_not_assume_index_zero(void)
{
    const int present[3] = { 0, 1, 1 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("shifted internal", internalIdx, 1);
    expect_int("shifted cart", cartIdx, 2);
}

/*----------------------
 | test_skips_a_gap_between_responders
 | Description: A silent index between two responders is skipped, not counted.
 | Author: suinevere
 ----------------------*/
static void test_skips_a_gap_between_responders(void)
{
    const int present[3] = { 1, 0, 1 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    expect_int("gap internal", internalIdx, 0);
    expect_int("gap cart", cartIdx, 2);
}

/*----------------------
 | test_a_third_responder_is_ignored
 | Description: Only two logical devices exist, so a third responder is dropped.
 | Author: suinevere
 ----------------------*/
static void test_a_third_responder_is_ignored(void)
{
    const int present[4] = { 1, 1, 1, 1 };
    int internalIdx = -99;
    int cartIdx = -99;

    bupDevmapResolve(present, 4, &internalIdx, &cartIdx);

    expect_int("third internal", internalIdx, 0);
    expect_int("third cart", cartIdx, 1);
}

/*----------------------
 | test_tolerates_a_short_table
 | Description: count bounds the search; a one-entry table is not read past.
 | Author: suinevere
 ----------------------*/
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
