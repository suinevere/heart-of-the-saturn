#include <stdio.h>
#include <string.h>
#include "discfmt.h"

#ifdef _WIN32
#include <windows.h>
#endif

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n"                      \
                   "  expected = %lld\n",                                     \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static void test_mode1_user_offset(void)
{
    CHECK_EQ(discfmt_mode1_user_offset(0),  16);

    CHECK_EQ(discfmt_mode1_user_offset(16), 16 * 2352 + 16);

    CHECK_EQ(discfmt_mode1_user_offset(20), 20 * 2352 + 16);

    CHECK_EQ(discfmt_mode1_user_offset(2593), 2593 * 2352 + 16);

    CHECK_EQ(discfmt_mode1_user_offset(5131), 5131 * 2352 + 16);
}

static void test_sector_span(void)
{
    CHECK_EQ(discfmt_sector_span(0),      0);
    CHECK_EQ(discfmt_sector_span(1),      1);
    CHECK_EQ(discfmt_sector_span(2047),   1);
    CHECK_EQ(discfmt_sector_span(2048),   1);
    CHECK_EQ(discfmt_sector_span(2049),   2);

    CHECK_EQ(discfmt_sector_span(409600), 200);

    CHECK_EQ(discfmt_sector_span(160826), 79);

    CHECK_EQ(discfmt_sector_span(436224), 213);
}

static void test_iso_name_eq(void)
{
    CHECK(discfmt_iso_name_eq("END1.BIN;1", 10, "END1.BIN"));

    CHECK(discfmt_iso_name_eq("END1.BIN", 8, "END1.BIN"));

    CHECK(discfmt_iso_name_eq("end1.bin;1", 10, "END1.BIN"));

    CHECK(!discfmt_iso_name_eq("ROOMS11.BIN;1", 13, "ROOMS1.BIN"));
    CHECK(!discfmt_iso_name_eq("ROOMS1.BIN;1", 12, "ROOMS11.BIN"));

    CHECK(!discfmt_iso_name_eq("END2.BIN;1", 10, "END1.BIN"));

    CHECK(!discfmt_iso_name_eq("END1.BINXX", 4, "END1.BIN"));
}

static void test_cue_track_for_music(void)
{
    CHECK_EQ(discfmt_cue_track_for_music(1),  2);
    CHECK_EQ(discfmt_cue_track_for_music(2),  3);
    CHECK_EQ(discfmt_cue_track_for_music(41), 42);

    CHECK_EQ(discfmt_cue_track_for_music(31), 32);
    CHECK_EQ(discfmt_cue_track_for_music(32), 33);
    CHECK_EQ(discfmt_cue_track_for_music(33), 34);

    CHECK_EQ(discfmt_cue_track_for_music(0), 0);

    for (int i = 1; i <= 41; i++) {
        CHECK(discfmt_cue_track_for_music(i) >= 2);
        CHECK(discfmt_cue_track_for_music(i) <= 42);
    }

    CHECK_EQ(discfmt_cue_track_for_music(-1), 0);
    CHECK_EQ(discfmt_cue_track_for_music(42), 0);
}

static size_t build_iso_record(uint8_t *buf, size_t pos, uint32_t lba,
                                uint32_t size, const char *name)
{
    size_t name_len = strlen(name);
    size_t rec_len = 33 + name_len;

    if (rec_len % 2 != 0) {
        rec_len++;
    }

    memset(buf + pos, 0, rec_len);

    buf[pos + 0] = (uint8_t)rec_len;

    buf[pos + 2] = (uint8_t)(lba & 0xFF);
    buf[pos + 3] = (uint8_t)((lba >> 8) & 0xFF);
    buf[pos + 4] = (uint8_t)((lba >> 16) & 0xFF);
    buf[pos + 5] = (uint8_t)((lba >> 24) & 0xFF);
    buf[pos + 6] = buf[pos + 5];
    buf[pos + 7] = buf[pos + 4];
    buf[pos + 8] = buf[pos + 3];
    buf[pos + 9] = buf[pos + 2];

    buf[pos + 10] = (uint8_t)(size & 0xFF);
    buf[pos + 11] = (uint8_t)((size >> 8) & 0xFF);
    buf[pos + 12] = (uint8_t)((size >> 16) & 0xFF);
    buf[pos + 13] = (uint8_t)((size >> 24) & 0xFF);
    buf[pos + 14] = buf[pos + 13];
    buf[pos + 15] = buf[pos + 12];
    buf[pos + 16] = buf[pos + 11];
    buf[pos + 17] = buf[pos + 10];

    buf[pos + 32] = (uint8_t)name_len;
    memcpy(buf + pos + 33, name, name_len);

    return pos + rec_len;
}

static void test_iso_root(void)
{
    uint8_t pvd[2048];
    uint32_t lba = 0;
    uint32_t len = 0;

    memset(pvd, 0, sizeof(pvd));
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[156 + 0] = 34;
    pvd[156 + 2] = 20;
    pvd[156 + 10] = 0x00;
    pvd[156 + 11] = 0x10;
    pvd[156 + 32] = 1;
    pvd[156 + 33] = 0;

    CHECK(discfmt_iso_root(pvd, &lba, &len));
    CHECK_EQ(lba, 20);
    CHECK_EQ(len, 4096);
}

static void test_iso_root_rejects_non_pvd(void)
{
    uint8_t pvd[2048];
    uint32_t lba = 0;
    uint32_t len = 0;

    memset(pvd, 0, sizeof(pvd));
    pvd[156 + 0] = 34;
    pvd[156 + 2] = 20;
    pvd[156 + 10] = 0x00;
    pvd[156 + 11] = 0x10;
    pvd[156 + 32] = 1;
    pvd[156 + 33] = 0;

    CHECK(!discfmt_iso_root(pvd, &lba, &len));

    memset(pvd, 0, sizeof(pvd));
    pvd[0] = 2;
    memcpy(pvd + 1, "CD001", 5);
    pvd[156 + 0] = 34;
    pvd[156 + 2] = 20;
    pvd[156 + 10] = 0x00;
    pvd[156 + 11] = 0x10;
    pvd[156 + 32] = 1;
    pvd[156 + 33] = 0;

    CHECK(!discfmt_iso_root(pvd, &lba, &len));
}

static void test_iso_find(void)
{
    uint8_t dir[4096];
    size_t pos = 0;
    uint32_t lba = 0;
    uint32_t size = 0;

    memset(dir, 0, sizeof(dir));

    pos = build_iso_record(dir, pos, 20, 4096, ".");
    pos = build_iso_record(dir, pos, 20, 4096, "..");
    pos = build_iso_record(dir, pos, 100, 12345, "FIRST.BIN;1");

    pos = 2048;
    pos = build_iso_record(dir, pos, 200, 67890, "SECOND.BIN;1");

    CHECK(discfmt_iso_find(dir, sizeof(dir), "FIRST.BIN", &lba, &size));
    CHECK_EQ(lba, 100);
    CHECK_EQ(size, 12345);

    lba = 0;
    size = 0;
    CHECK(discfmt_iso_find(dir, sizeof(dir), "SECOND.BIN", &lba, &size));
    CHECK_EQ(lba, 200);
    CHECK_EQ(size, 67890);

    CHECK(!discfmt_iso_find(dir, sizeof(dir), "NOPE.BIN", &lba, &size));

    CHECK(!discfmt_iso_find(dir, 2048 + 10, "SECOND.BIN", &lba, &size));

    CHECK(!discfmt_iso_find(dir, 0, "FIRST.BIN", &lba, &size));
}

static void test_cue_parse_multi_file(void)
{
    static char cue[1024 * 32];
    size_t off = 0;
    int n;
    DiscCue disc_cue;
    int single_file = -1;
    int i;

    off += (size_t)sprintf(cue + off,
        "FILE \"Heart of the Alien (Track 01).bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2352\r\n"
        "    INDEX 01 00:00:00\r\n");

    for (i = 2; i <= 42; i++) {
        n = sprintf(cue + off,
            "FILE \"Heart of the Alien (Track %02d).bin\" BINARY\r\n"
            "  TRACK %02d AUDIO\r\n"
            "    INDEX 00 00:00:00\r\n"
            "    INDEX 01 00:02:00\r\n",
            i, i);
        off += (size_t)n;
    }

    CHECK(discfmt_cue_parse(cue, off, &disc_cue, &single_file));
    CHECK_EQ(single_file, 0);
    CHECK_EQ(disc_cue.count, 42);

    CHECK_EQ(disc_cue.tracks[0].number, 1);
    CHECK_EQ(disc_cue.tracks[0].is_audio, 0);
    CHECK(strcmp(disc_cue.tracks[0].filename,
                 "Heart of the Alien (Track 01).bin") == 0);

    for (i = 1; i < 42; i++) {
        char want[64];
        CHECK_EQ(disc_cue.tracks[i].number, i + 1);
        CHECK_EQ(disc_cue.tracks[i].is_audio, 1);
        sprintf(want, "Heart of the Alien (Track %02d).bin", i + 1);
        CHECK(strcmp(disc_cue.tracks[i].filename, want) == 0);

        CHECK_EQ(disc_cue.tracks[i].pregap_sectors, 150);
    }

    CHECK_EQ(disc_cue.tracks[0].pregap_sectors, 0);
}

static void test_cue_parse_pregap_widths(void)
{
    static const char cue[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2352\r\n"
        "    INDEX 01 00:00:00\r\n"
        "FILE \"t02.bin\" BINARY\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 00:02:00\r\n"
        "FILE \"t03.bin\" BINARY\r\n"
        "  TRACK 03 AUDIO\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 00:02:01\r\n"
        "FILE \"t04.bin\" BINARY\r\n"
        "  TRACK 04 AUDIO\r\n"
        "    INDEX 01 00:00:00\r\n"
        "FILE \"t05.bin\" BINARY\r\n"
        "  TRACK 05 AUDIO\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 01:03:02\r\n";
    DiscCue disc_cue;
    int single_file = -1;

    CHECK(discfmt_cue_parse(cue, sizeof(cue) - 1, &disc_cue, &single_file));
    CHECK_EQ(disc_cue.count, 5);

    CHECK_EQ(disc_cue.tracks[0].pregap_sectors, 0);
    CHECK_EQ(disc_cue.tracks[1].pregap_sectors, 150);
    CHECK_EQ(disc_cue.tracks[2].pregap_sectors, 151);
    CHECK_EQ(disc_cue.tracks[3].pregap_sectors, 0);

    CHECK_EQ(disc_cue.tracks[4].pregap_sectors, 63 * 75 + 2);
}

static void test_cue_parse_pregap_keyword_is_not_a_pregap(void)
{
    static const char cue[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2352\r\n"
        "    INDEX 01 00:00:00\r\n"
        "FILE \"t02.bin\" BINARY\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    PREGAP   00:02:00\r\n"
        "    INDEX 01 00:00:00\r\n";
    DiscCue disc_cue;
    int single_file = -1;

    CHECK(discfmt_cue_parse(cue, sizeof(cue) - 1, &disc_cue, &single_file));
    CHECK_EQ(disc_cue.count, 2);
    CHECK_EQ(disc_cue.tracks[1].pregap_sectors, 0);
}

static void test_cue_parse_higher_indexes_ignored(void)
{
    static const char cue[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 00:02:00\r\n"
        "    INDEX 02 01:30:00\r\n"
        "    INDEX 03 02:45:15\r\n";
    DiscCue disc_cue;
    int single_file = -1;

    CHECK(discfmt_cue_parse(cue, sizeof(cue) - 1, &disc_cue, &single_file));
    CHECK_EQ(disc_cue.count, 1);
    CHECK_EQ(disc_cue.tracks[0].pregap_sectors, 150);
}

static void test_cue_parse_rejects_bad_indexes(void)
{
    static const char backwards[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 00 00:02:00\r\n"
        "    INDEX 01 00:00:00\r\n";

    static const char orphan[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "    INDEX 01 00:00:00\r\n";

    static const char overflow[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 00:01:75\r\n";

    static const char truncated[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 01 00:02\r\n";

    DiscCue disc_cue;
    int single_file = -1;

    CHECK(!discfmt_cue_parse(backwards, sizeof(backwards) - 1, &disc_cue, &single_file));
    CHECK_EQ(single_file, 0);
    CHECK(!discfmt_cue_parse(orphan, sizeof(orphan) - 1, &disc_cue, &single_file));
    CHECK(!discfmt_cue_parse(overflow, sizeof(overflow) - 1, &disc_cue, &single_file));
    CHECK(!discfmt_cue_parse(truncated, sizeof(truncated) - 1, &disc_cue, &single_file));
}

static void test_cue_parse_pregap_does_not_leak_between_tracks(void)
{
    static const char cue[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 00:02:00\r\n"
        "FILE \"t02.bin\" BINARY\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 01 00:00:00\r\n";
    DiscCue disc_cue;
    int single_file = -1;

    CHECK(discfmt_cue_parse(cue, sizeof(cue) - 1, &disc_cue, &single_file));
    CHECK_EQ(disc_cue.count, 2);
    CHECK_EQ(disc_cue.tracks[0].pregap_sectors, 150);
    CHECK_EQ(disc_cue.tracks[1].pregap_sectors, 0);
}

static void test_cue_parse_single_file_rejected(void)
{
    static const char cue[] =
        "FILE \"Heart of the Alien.bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2352\r\n"
        "    INDEX 01 00:00:00\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 00 00:04:30\r\n"
        "    INDEX 01 00:06:30\r\n";
    DiscCue disc_cue;
    int single_file = 0;

    CHECK(!discfmt_cue_parse(cue, sizeof(cue) - 1, &disc_cue, &single_file));
    CHECK_EQ(single_file, 1);
}

static void test_cue_parse_malformed_not_single_file(void)
{
    static const char cue[] = "  TRACK 01 MODE1/2352\r\n";
    DiscCue disc_cue;
    int single_file = -1;

    CHECK(!discfmt_cue_parse(cue, sizeof(cue) - 1, &disc_cue, &single_file));
    CHECK_EQ(single_file, 0);
}

#ifdef _WIN32
static void test_cue_parse_no_trailing_newline_track_number_bound(void)
{

    static const char cue[] = "FILE \"x\" BINARY\r\n  TRACK 01";
    size_t cue_len = sizeof(cue) - 1;
    SYSTEM_INFO si;
    size_t page_size;
    uint8_t *region;
    uint8_t *first_page;
    uint8_t *dst;
    DiscCue disc_cue;
    int single_file = -1;

    GetSystemInfo(&si);
    page_size = (size_t)si.dwPageSize;

    region = (uint8_t *)VirtualAlloc(NULL, page_size * 2, MEM_RESERVE, PAGE_NOACCESS);
    CHECK(region != NULL);
    if (region == NULL)
    {
        return;
    }

    first_page = (uint8_t *)VirtualAlloc(region, page_size, MEM_COMMIT, PAGE_READWRITE);
    CHECK(first_page == region);

    dst = region + page_size - cue_len;
    memcpy(dst, cue, cue_len);

    CHECK(!discfmt_cue_parse((const char *)dst, cue_len, &disc_cue, &single_file));
    CHECK_EQ(single_file, 0);

    VirtualFree(region, 0, MEM_RELEASE);
}
#endif

int main(void)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif

    test_mode1_user_offset();
    test_sector_span();
    test_iso_name_eq();
    test_cue_track_for_music();
    test_iso_root();
    test_iso_root_rejects_non_pvd();
    test_iso_find();
    test_cue_parse_multi_file();
    test_cue_parse_single_file_rejected();
    test_cue_parse_malformed_not_single_file();
    test_cue_parse_pregap_widths();
    test_cue_parse_pregap_keyword_is_not_a_pregap();
    test_cue_parse_higher_indexes_ignored();
    test_cue_parse_rejects_bad_indexes();
    test_cue_parse_pregap_does_not_leak_between_tracks();
#ifdef _WIN32
    test_cue_parse_no_trailing_newline_track_number_bound();
#endif

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }

    printf("%d check(s) failed\n", g_fail);
    return 1;
}
