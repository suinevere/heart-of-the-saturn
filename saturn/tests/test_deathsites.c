#include <stdio.h>
#include "deathsites.h"

static int g_fail;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        printf("  FAIL %s: got %d, want %d\n", what, got, want);
        g_fail++;
    }
}

static void test_every_row_answers_for_its_own_room(void)
{
    int i;

    for (i = 0; i < DEATH_SITE_COUNT; i++) {
        expect_int("a row is a real death site",
                   death_site_is_real(death_site_room(i), death_site_pc(i)), 1);
    }
}

static void test_rows_are_the_offsets_read_off_the_discs(void)
{
    expect_int("ROOMS1 first of the chained pair",
               death_site_is_real(1, 0x25ab), 1);
    expect_int("ROOMS1 second of the chained pair",
               death_site_is_real(1, 0x25ad), 1);
    expect_int("ROOMS6 has its one site",
               death_site_is_real(6, 0x10f6), 1);
    expect_int("ROOMS8 has its low site",
               death_site_is_real(8, 0x005c), 1);
    expect_int("ROOMS5 has its high site",
               death_site_is_real(5, 0xfe27), 1);
}

static void test_the_whips_cutscene_is_not_a_death(void)
{
    expect_int("the offset the whip cutscene played 0x21 from",
               death_site_is_real(1, 0x0333), 0);
    expect_int("and neither is the pc the handler held there",
               death_site_is_real(1, 0x0335), 0);
}

static void test_a_room_does_not_answer_for_another_rooms_offset(void)
{
    expect_int("ROOMS1 does not own ROOMS3's site",
               death_site_is_real(1, 0x2708), 0);
    expect_int("ROOMS3 does not own ROOMS1's site",
               death_site_is_real(3, 0x25ab), 0);
    expect_int("room 7 owns nothing, the port replaces it",
               death_site_is_real(7, 0x25ab), 0);
    expect_int("nor does a room that does not exist",
               death_site_is_real(99, 0x25ab), 0);
}

static void test_out_of_range_rows_are_refused(void)
{
    expect_int("a row below the table has no room", death_site_room(-1), 0);
    expect_int("a row past the table has no room",
               death_site_room(DEATH_SITE_COUNT), 0);
    expect_int("a row below the table has no offset", death_site_pc(-1), -1);
    expect_int("a row past the table has no offset",
               death_site_pc(DEATH_SITE_COUNT), -1);
}

int main(void)
{
    test_every_row_answers_for_its_own_room();
    test_rows_are_the_offsets_read_off_the_discs();
    test_the_whips_cutscene_is_not_a_death();
    test_a_room_does_not_answer_for_another_rooms_offset();
    test_out_of_range_rows_are_refused();

    if (g_fail == 0) {
        printf("deathsites: all tests passed\n");
        return 0;
    }
    printf("deathsites: %d failure(s)\n", g_fail);
    return 1;
}
