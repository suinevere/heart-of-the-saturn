/*----------------------
 | test_savedata.c
 | Description: Host unit tests for savedata.c, linked against
 |   stub_saturn_backup.c rather than the real BUP layer. Built and run by
 |   run_tests.sh with the host gcc.
 | Author: suinevere
 | Dependencies: savedata.h, stub_saturn_backup.h, stdio.h, string.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "savedata.h"
#include "stub_saturn_backup.h"

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

static void test_slot_names(void)
{
    char name[12];

    savedata_slot_name(0, name);
    expect_str("slot 0 name", name, "HOTASAVE1");
    savedata_slot_name(1, name);
    expect_str("slot 1 name", name, "HOTASAVE2");
    savedata_slot_name(2, name);
    expect_str("slot 2 name", name, "HOTASAVE3");
}

static void test_header_roundtrip(void)
{
    unsigned char buf[SAVE_HEADER_SIZE];
    unsigned short ver = 0, room = 0;
    unsigned short payloadLen = 0;
    unsigned char flags = 0;
    unsigned long date = 0;

    savedata_write_header(buf, 23, 0x11223344UL, 1, 4321);
    expect_int("header parses", savedata_read_header(buf, &ver, &room, &date,
                                                     &flags, &payloadLen), 1);
    expect_int("version", (int)ver, SAVE_FORMAT_VERSION);
    expect_int("room", (int)room, 23);
    expect_int("date high", (int)(date >> 16), 0x1122);
    expect_int("date low", (int)(date & 0xFFFF), 0x3344);
    expect_int("flags", (int)flags, 1);
    expect_int("payload length", (int)payloadLen, 4321);
}

static void test_header_rejects_bad_magic(void)
{
    unsigned char buf[SAVE_HEADER_SIZE];
    unsigned short ver = 7, room = 7, payloadLen = 7;
    unsigned char flags = 7;
    unsigned long date = 7;

    savedata_write_header(buf, 1, 2, 0, 10);
    buf[2] = 'X';
    expect_int("bad magic rejected",
               savedata_read_header(buf, &ver, &room, &date, &flags, &payloadLen), 0);
    expect_int("outputs untouched on rejection", (int)room, 7);
}

static void test_probe_empty_slot(void)
{
    unsigned char scratch[SAVE_MAX_BYTES];
    SlotInfo info;

    stub_bup_reset();
    expect_int("empty slot", (int)savedata_probe(SAT_BUP_INTERNAL, 0, &info,
                                                 scratch, SAVE_MAX_BYTES),
               (int)SLOT_EMPTY);
}

static void test_probe_ok_slot(void)
{
    unsigned char scratch[SAVE_MAX_BYTES];
    unsigned char file[SAVE_HEADER_SIZE + 4];
    SlotInfo info;

    stub_bup_reset();
    savedata_write_header(file, 42, 0x00010203UL, 0, 4);
    memset(file + SAVE_HEADER_SIZE, 0, 4);
    stub_bup_place(SAT_BUP_INTERNAL, "HOTASAVE1", file, (int)sizeof(file));

    expect_int("good slot", (int)savedata_probe(SAT_BUP_INTERNAL, 0, &info,
                                                scratch, SAVE_MAX_BYTES),
               (int)SLOT_OK);
    expect_int("room reported", (int)info.roomId, 42);
}

static void test_probe_damaged_slot(void)
{
    unsigned char scratch[SAVE_MAX_BYTES];
    unsigned char file[SAVE_HEADER_SIZE + 4];
    SlotInfo info;

    stub_bup_reset();
    savedata_write_header(file, 42, 1, 0, 4);
    file[0] = 'Z';
    stub_bup_place(SAT_BUP_INTERNAL, "HOTASAVE1", file, (int)sizeof(file));

    expect_int("bad magic is damaged",
               (int)savedata_probe(SAT_BUP_INTERNAL, 0, &info, scratch,
                                   SAVE_MAX_BYTES),
               (int)SLOT_DAMAGED);
}

static void test_probe_old_version(void)
{
    unsigned char scratch[SAVE_MAX_BYTES];
    unsigned char file[SAVE_HEADER_SIZE + 4];
    SlotInfo info;

    stub_bup_reset();
    savedata_write_header(file, 42, 1, 0, 4);
    file[4] = 0;
    file[5] = 0;
    stub_bup_place(SAT_BUP_INTERNAL, "HOTASAVE1", file, (int)sizeof(file));

    expect_int("older version flagged",
               (int)savedata_probe(SAT_BUP_INTERNAL, 0, &info, scratch,
                                   SAVE_MAX_BYTES),
               (int)SLOT_OLD_VERSION);
}

static void test_probe_read_failure_is_damaged(void)
{
    unsigned char scratch[SAVE_MAX_BYTES];
    unsigned char file[SAVE_HEADER_SIZE + 4];
    SlotInfo info;

    stub_bup_reset();
    savedata_write_header(file, 42, 1, 0, 4);
    stub_bup_place(SAT_BUP_INTERNAL, "HOTASAVE1", file, (int)sizeof(file));
    stub_bup_fail_read(SAT_BUP_ERR_BROKEN);

    expect_int("read failure is damaged",
               (int)savedata_probe(SAT_BUP_INTERNAL, 0, &info, scratch,
                                   SAVE_MAX_BYTES),
               (int)SLOT_DAMAGED);
}

static void test_device_defaulting(void)
{
    SatBupDev internal, cart;

    memset(&internal, 0, sizeof(internal));
    memset(&cart, 0, sizeof(cart));
    internal.present = 1;
    internal.formatted = 1;

    expect_int("no cart at all",
               (int)savedata_pick_default_device(&internal, &cart, 0, 0),
               SAT_BUP_INTERNAL);

    cart.present = 1;
    cart.formatted = 1;
    expect_int("cart present but empty",
               (int)savedata_pick_default_device(&internal, &cart, 0, 0),
               SAT_BUP_INTERNAL);
    expect_int("cart has saves, internal does not",
               (int)savedata_pick_default_device(&internal, &cart, 0, 1),
               SAT_BUP_CART);
    expect_int("both have saves prefers internal",
               (int)savedata_pick_default_device(&internal, &cart, 1, 1),
               SAT_BUP_INTERNAL);

    cart.formatted = 0;
    expect_int("unformatted cart is ignored",
               (int)savedata_pick_default_device(&internal, &cart, 0, 1),
               SAT_BUP_INTERNAL);
}

static void test_date_split(void)
{
    int month = 0, day = 0, hour = 0, min = 0;

    savedata_date_split(0, &month, &day, &hour, &min);
    expect_int("epoch month", month, 1);
    expect_int("epoch day", day, 1);
    expect_int("epoch hour", hour, 0);
    expect_int("epoch minute", min, 0);

    savedata_date_split(1440UL * 31UL, &month, &day, &hour, &min);
    expect_int("one month on, month", month, 2);
    expect_int("one month on, day", day, 1);

    savedata_date_split(1440UL * 59UL, &month, &day, &hour, &min);
    expect_int("leap day 1980, month", month, 2);
    expect_int("leap day 1980, day", day, 29);

    savedata_date_split(1440UL * 366UL, &month, &day, &hour, &min);
    expect_int("first day of 1981, month", month, 1);
    expect_int("first day of 1981, day", day, 1);

    savedata_date_split(754UL, &month, &day, &hour, &min);
    expect_int("time of day, hour", hour, 12);
    expect_int("time of day, minute", min, 34);

    savedata_date_split(60UL, &month, NULL, &hour, NULL);
    expect_int("null outputs tolerated", hour, 1);
}

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

int main(void)
{
    test_slot_names();
    test_header_roundtrip();
    test_header_rejects_bad_magic();
    test_probe_empty_slot();
    test_probe_ok_slot();
    test_probe_damaged_slot();
    test_probe_old_version();
    test_probe_read_failure_is_damaged();
    test_device_defaulting();
    test_date_split();
    test_probe_rejects_undersized_file();

    if (g_fail != 0) {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_savedata: all passed\n");
    return 0;
}
