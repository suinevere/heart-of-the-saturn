#include <stdio.h>
#include <string.h>
#include "checkpoints.h"

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
    if (strcmp(got, want) != 0) {
        g_fail++;
        printf("FAIL %s\n  actual   = %s\n  expected = %s\n", what, got, want);
    }
}

static int decode_room(int code)
{
    int ns = ((code / 10) % 10) + 1;

    if (ns == 10) {
        return 8;
    }
    if (ns == 7) {
        return 6;
    }
    if (ns == 6) {
        return 7;
    }
    return ns;
}

static void test_the_table_is_twenty_long(void)
{
    expect_int("twenty of the twenty-three words name a place to play from",
               CHECKPOINT_COUNT, 20);
    expect_str("and the first is where a new game begins",
               checkpoint_word(0), "BDXF");
    expect_int("whose code is the one menu.c already open-codes",
               checkpoint_code(0), 17000);
}

static void test_every_word_is_four_glyphs_from_the_alphabet(void)
{
    int i;
    int j;

    for (i = 0; i < CHECKPOINT_COUNT; i++) {
        const char *w = checkpoint_word(i);

        expect_int("a word is four letters", (int)strlen(w), 4);
        for (j = 0; j < CHECKPOINT_WORD_LEN; j++) {
            expect_int("and every letter is in the password alphabet",
                       strchr("BCDFGHJKLRTX", w[j]) != NULL, 1);
        }
    }
}

static void test_room_and_entry_are_what_decode_derives(void)
{
    int i;

    for (i = 0; i < CHECKPOINT_COUNT; i++) {
        int code = checkpoint_code(i);

        expect_int("a code is a load_screen operand",
                   code >= 17000 && code <= 17100, 1);
        expect_int("the room is decode.c's", checkpoint_room(i),
                   decode_room(code));
        expect_int("the entry is the code's last digit", checkpoint_entry(i),
                   code % 10);
    }
}

static void test_codes_and_words_are_distinct(void)
{
    int i;
    int j;

    for (i = 0; i < CHECKPOINT_COUNT; i++) {
        for (j = i + 1; j < CHECKPOINT_COUNT; j++) {
            expect_int("no code appears twice",
                       checkpoint_code(i) == checkpoint_code(j), 0);
            expect_int("no word appears twice",
                       strcmp(checkpoint_word(i), checkpoint_word(j)) == 0, 0);
        }
    }
}

static void test_the_dead_words_and_unreachable_stubs_are_absent(void)
{
    int i;

    for (i = 0; i < CHECKPOINT_COUNT; i++) {
        expect_int("JBGH is refused by the switch",
                   strcmp(checkpoint_word(i), "JBGH") == 0, 0);
        expect_int("KHFK is refused by the switch",
                   strcmp(checkpoint_word(i), "KHFK") == 0, 0);
        expect_int("no word reaches the 17002 stub",
                   checkpoint_code(i) == 17002, 0);
        expect_int("no word reaches the 17011 stub",
                   checkpoint_code(i) == 17011, 0);
        expect_int("and the credits are not a place to play from",
                   checkpoint_code(i) == 17050, 0);
    }
    expect_int("so room 1 entry 2 is not a checkpoint",
               checkpoint_find(1, 2), -1);
    expect_int("and neither is room 2 entry 1", checkpoint_find(2, 1), -1);
    expect_int("nor is the password room, which the port replaces",
               checkpoint_find(7, 0), -1);
}

static void test_find_round_trips_every_row(void)
{
    int i;

    for (i = 0; i < CHECKPOINT_COUNT; i++) {
        expect_int("find returns the row it was built from",
                   checkpoint_find(checkpoint_room(i), checkpoint_entry(i)), i);
    }
    expect_int("a room the rooms accept and no word reaches is not one",
               checkpoint_find(3, 9), -1);
    expect_int("and neither is a room that does not exist",
               checkpoint_find(99, 0), -1);
}

static void test_rows_group_by_room(void)
{
    int seen[16];
    int i;
    int previous = -1;

    memset(seen, 0, sizeof seen);

    for (i = 0; i < CHECKPOINT_COUNT; i++) {
        int room = checkpoint_room(i);

        if (room != previous) {
            expect_int("a room's rows are contiguous", seen[room], 0);
            seen[room] = 1;
            previous = room;
        } else {
            expect_int("and its entries climb inside it",
                       checkpoint_entry(i) > checkpoint_entry(i - 1), 1);
        }
    }

    expect_int("seven rooms carry a checkpoint", seen[1] + seen[2] + seen[3]
               + seen[4] + seen[5] + seen[6] + seen[8], 7);
    expect_int("and the password room is not one of them", seen[7], 0);
}

static void test_visible_always_offers_the_first_checkpoint(void)
{
    unsigned char out[CHECKPOINT_COUNT];
    int n;

    n = checkpoint_visible(0UL, 0, out, CHECKPOINT_COUNT);
    expect_int("a fresh machine sees one row", n, 1);
    expect_int("and it is the new game", (int)out[0], 0);

    n = checkpoint_visible(1UL << 5, 0, out, CHECKPOINT_COUNT);
    expect_int("one reached checkpoint adds one row", n, 2);
    expect_int("the first is still the new game", (int)out[0], 0);
    expect_int("the second is the one reached", (int)out[1], 5);

    n = checkpoint_visible(0UL, 1, out, CHECKPOINT_COUNT);
    expect_int("and an unlocked session sees every row", n, CHECKPOINT_COUNT);
}

static void test_visible_never_writes_past_its_cap(void)
{
    unsigned char out[4];
    int n;

    memset(out, 0xEE, sizeof out);
    n = checkpoint_visible(~0UL, 1, out, 3);
    expect_int("the cap is honoured", n, 3);
    expect_int("and the byte past it is untouched", (int)out[3], 0xEE);
}

static void test_progress_round_trips(void)
{
    unsigned char buf[CHECKPOINT_PROGRESS_BYTES];
    unsigned long mask = 0UL;

    checkpoint_progress_serialise(0x0015A3UL, buf);
    expect_int("a stored mask reads back",
               checkpoint_progress_parse(buf, sizeof buf, &mask), 1);
    expect_int("with its bits intact", (int)mask, 0x0015A3);
}

static void test_progress_refuses_anything_else(void)
{
    unsigned char buf[CHECKPOINT_PROGRESS_BYTES];
    unsigned long mask = 0xFFFFUL;

    memset(buf, 0, sizeof buf);
    expect_int("an erased entry is not progress",
               checkpoint_progress_parse(buf, sizeof buf, &mask), 0);
    expect_int("and reads as none", (int)mask, 0);

    checkpoint_progress_serialise(0xFFUL, buf);
    buf[4] = CHECKPOINT_PROGRESS_VERSION + 1;
    mask = 0xFFFFUL;
    expect_int("a later version is refused",
               checkpoint_progress_parse(buf, sizeof buf, &mask), 0);
    expect_int("and reads as none", (int)mask, 0);

    checkpoint_progress_serialise(0xFFUL, buf);
    mask = 0xFFFFUL;
    expect_int("so is a short entry",
               checkpoint_progress_parse(buf, CHECKPOINT_PROGRESS_BYTES - 1,
                                         &mask), 0);
    expect_int("and reads as none", (int)mask, 0);
}

static void test_progress_cannot_carry_a_bit_no_checkpoint_owns(void)
{
    unsigned char buf[CHECKPOINT_PROGRESS_BYTES];
    unsigned long mask = 0UL;

    checkpoint_progress_serialise(0xFFFFFFFFUL, buf);
    expect_int("a full word parses",
               checkpoint_progress_parse(buf, sizeof buf, &mask), 1);
    expect_int("with the bits past the table masked away",
               (int)(mask >> CHECKPOINT_COUNT), 0);
}

int main(void)
{
    test_the_table_is_twenty_long();
    test_every_word_is_four_glyphs_from_the_alphabet();
    test_room_and_entry_are_what_decode_derives();
    test_codes_and_words_are_distinct();
    test_the_dead_words_and_unreachable_stubs_are_absent();
    test_find_round_trips_every_row();
    test_rows_group_by_room();
    test_visible_always_offers_the_first_checkpoint();
    test_visible_never_writes_past_its_cap();
    test_progress_round_trips();
    test_progress_refuses_anything_else();
    test_progress_cannot_carry_a_bit_no_checkpoint_owns();

    if (g_fail == 0) {
        printf("checkpoints: all tests passed\n");
        return 0;
    }
    printf("checkpoints: %d failure(s)\n", g_fail);
    return 1;
}
