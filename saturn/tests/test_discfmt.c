/*----------------------
 | test_discfmt.c
 | Description: Host unit tests for discfmt.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: discfmt.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "discfmt.h"

#ifdef _WIN32
/* Only the guard-page regression below needs this -- discfmt.h/.c stay free
   of it. VirtualAlloc lets that one test place its buffer directly against
   an unmapped page, so an out-of-bounds read faults deterministically
   instead of merely reading unrelated-but-mapped memory and getting lucky. */
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
    /* 12 bytes of sync plus a 4-byte header precede the 2048 bytes of user
       data in every MODE1/2352 sector. Sector 0 is the Sega CD header. */
    CHECK_EQ(discfmt_mode1_user_offset(0),  16);

    /* The ISO9660 Primary Volume Descriptor. */
    CHECK_EQ(discfmt_mode1_user_offset(16), 16 * 2352 + 16);

    /* The root directory of this disc. */
    CHECK_EQ(discfmt_mode1_user_offset(20), 20 * 2352 + 16);

    /* END1.BIN, the first entry of the validation manifest. */
    CHECK_EQ(discfmt_mode1_user_offset(2593), 2593 * 2352 + 16);

    /* The last sector of the data track: 5132 sectors total. Computed in 32
       bits without overflow -- 5131 * 2352 is ~12 M, comfortably inside. */
    CHECK_EQ(discfmt_mode1_user_offset(5131), 5131 * 2352 + 16);
}

static void test_sector_span(void)
{
    /* Reads are whole sectors even when the file is not a multiple of one.
       Getting this wrong by a sector truncates the tail of every odd-sized
       file -- of which ROOMS7.BIN is the one that matters. */
    CHECK_EQ(discfmt_sector_span(0),      0);
    CHECK_EQ(discfmt_sector_span(1),      1);
    CHECK_EQ(discfmt_sector_span(2047),   1);
    CHECK_EQ(discfmt_sector_span(2048),   1);
    CHECK_EQ(discfmt_sector_span(2049),   2);

    /* GAME2.BIN: 409600 bytes, exactly 200 sectors. */
    CHECK_EQ(discfmt_sector_span(409600), 200);

    /* ROOMS7.BIN: 160826 bytes, NOT sector-aligned -- 78.5 sectors. */
    CHECK_EQ(discfmt_sector_span(160826), 79);

    /* MAKE2MB.BIN, the largest blob: 436224 bytes. */
    CHECK_EQ(discfmt_sector_span(436224), 213);
}

static void test_iso_name_eq(void)
{
    /* ISO9660 stores a version suffix. A plain strcmp against "END1.BIN"
       matches nothing on this disc, which presents as "every file missing". */
    CHECK(discfmt_iso_name_eq("END1.BIN;1", 10, "END1.BIN"));

    /* Some masterings omit the suffix. Both must work. */
    CHECK(discfmt_iso_name_eq("END1.BIN", 8, "END1.BIN"));

    /* Case-insensitive: the engine asks in uppercase and so does this disc,
       but nothing guarantees that of a remaster. */
    CHECK(discfmt_iso_name_eq("end1.bin;1", 10, "END1.BIN"));

    /* A shared prefix must NOT match. This is the check that stops a lookup
       for ROOMS1.BIN silently returning ROOMS11.BIN on a disc that has one. */
    CHECK(!discfmt_iso_name_eq("ROOMS11.BIN;1", 13, "ROOMS1.BIN"));
    CHECK(!discfmt_iso_name_eq("ROOMS1.BIN;1", 12, "ROOMS11.BIN"));

    /* Different files entirely. */
    CHECK(!discfmt_iso_name_eq("END2.BIN;1", 10, "END1.BIN"));

    /* The record length is authoritative, not a NUL: ISO9660 names are not
       NUL-terminated in the record. Passing a short length must truncate. */
    CHECK(!discfmt_iso_name_eq("END1.BINXX", 4, "END1.BIN"));
}

static void test_cue_track_for_music(void)
{
    /* The disc has 41 audio tracks, TRACK 02 through TRACK 42, reached from
       engine indices 1..41. The index already counts the data track. */
    CHECK_EQ(discfmt_cue_track_for_music(1),  2);
    CHECK_EQ(discfmt_cue_track_for_music(2),  3);
    CHECK_EQ(discfmt_cue_track_for_music(41), 42);

    /* anm_files' three intro entries, pinned against the game itself: the
       intro plays cue 32, then 33, then 34. Getting this wrong is not subtle
       to a listener and was not caught by any amount of reasoning about how
       the deleted music.c numbered its mp3 rip. */
    CHECK_EQ(discfmt_cue_track_for_music(31), 32);  /* INTRO1.BIN */
    CHECK_EQ(discfmt_cue_track_for_music(32), 33);  /* INTRO2.BIN */
    CHECK_EQ(discfmt_cue_track_for_music(33), 34);  /* INTRO3.BIN */

    /* TRACK 01 is the DATA track: noise or a hang on hardware, and often just
       silence in an emulator, so it survives casual testing. Nothing may ever
       return 1 -- index 0 is refused rather than mapped onto it. */
    CHECK_EQ(discfmt_cue_track_for_music(0), 0);

    for (int i = 1; i <= 41; i++) {
        CHECK(discfmt_cue_track_for_music(i) >= 2);
        CHECK(discfmt_cue_track_for_music(i) <= 42);
    }

    /* Out of range is refused rather than clamped: a bytecode operand that
       lands here is a bug worth seeing, not one worth papering over. */
    CHECK_EQ(discfmt_cue_track_for_music(-1), 0);
    CHECK_EQ(discfmt_cue_track_for_music(42), 0);
}

/*----------------------
 | build_iso_record
 | Description: Writes one ISO9660 directory record at buf[pos] -- record
 |   length, LBA and size each stored LE then BE (matching the real format,
 |   even though this code only ever reads the LE copy), name length and
 |   name -- and returns pos advanced past it. A hand-built record documents
 |   the field layout at the call site instead of hiding it in a binary blob.
 | Author: suinevere
 ----------------------*/
static size_t build_iso_record(uint8_t *buf, size_t pos, uint32_t lba,
                                uint32_t size, const char *name)
{
    size_t name_len = strlen(name);
    size_t rec_len = 33 + name_len;

    if (rec_len % 2 != 0) {
        rec_len++;  /* ISO9660 pads records to an even length. */
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
    /* The PVD's root directory record: 34 bytes at offset 156 of the
       2048-byte user area. Root's own name is a single 0x00 byte. */
    uint8_t pvd[2048];
    uint32_t lba = 0;
    uint32_t len = 0;

    /* Root's own name is a single NUL byte (name_len 1), which can't be
       expressed as a C string through build_iso_record's strlen -- so this
       one record is written directly instead of through the builder. */
    memset(pvd, 0, sizeof(pvd));
    pvd[0] = 1;                    /* PVD type code */
    memcpy(pvd + 1, "CD001", 5);   /* PVD standard identifier */
    pvd[156 + 0] = 34;   /* record length: 33 fixed bytes + 1-byte name */
    pvd[156 + 2] = 20;   /* LBA, LE: 20 */
    pvd[156 + 10] = 0x00; /* size, LE: 4096 = 0x00001000 */
    pvd[156 + 11] = 0x10;
    pvd[156 + 32] = 1;   /* name length */
    pvd[156 + 33] = 0;   /* name: the single NUL byte identifying "." */

    CHECK(discfmt_iso_root(pvd, &lba, &len));
    CHECK_EQ(lba, 20);
    CHECK_EQ(len, 4096);
}

static void test_iso_root_rejects_non_pvd(void)
{
    /* Important-severity regression: discfmt_iso_root must not trust offset
       156 of an arbitrary sector. A buffer that is not actually a Primary
       Volume Descriptor -- wrong type code, or missing the "CD001"
       identifier -- must be rejected outright, rather than returning a
       plausible-looking LBA/size read from the wrong place. Tasks 4 and 7
       both trust this return value to locate every file on the disc, so a
       silent bogus success here would surface as "the whole disc is
       corrupt" rather than "we read the wrong sector". */
    uint8_t pvd[2048];
    uint32_t lba = 0;
    uint32_t len = 0;

    /* Otherwise identical to test_iso_root's valid record -- byte 0 and
       bytes 1..5 are left zero, so there is neither the type code nor the
       "CD001" identifier. This is what a directory record sitting at 156
       of a random, non-PVD sector looks like. */
    memset(pvd, 0, sizeof(pvd));
    pvd[156 + 0] = 34;
    pvd[156 + 2] = 20;
    pvd[156 + 10] = 0x00;
    pvd[156 + 11] = 0x10;
    pvd[156 + 32] = 1;
    pvd[156 + 33] = 0;

    CHECK(!discfmt_iso_root(pvd, &lba, &len));

    /* The identifier alone is not enough either -- the type code must also
       be 1, not (say) 2, a Supplementary Volume Descriptor. */
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
    /* A two-block (4096-byte) root directory: "." and ".." in block 0,
       then one real file, then the rest of block 0 is zero padding, then
       another real file starting exactly at the block-1 boundary. This is
       the disc's actual shape and the one that breaks a walk that treats a
       zero record length as end-of-directory. */
    uint8_t dir[4096];
    size_t pos = 0;
    uint32_t lba = 0;
    uint32_t size = 0;

    memset(dir, 0, sizeof(dir));

    pos = build_iso_record(dir, pos, 20, 4096, ".");
    pos = build_iso_record(dir, pos, 20, 4096, "..");
    pos = build_iso_record(dir, pos, 100, 12345, "FIRST.BIN;1");

    /* Everything from pos to 2048 is left zero -- the record-length-0 pad
       that must be skipped, not treated as EOF. */

    pos = 2048;
    pos = build_iso_record(dir, pos, 200, 67890, "SECOND.BIN;1");

    /* Found in block 0. */
    CHECK(discfmt_iso_find(dir, sizeof(dir), "FIRST.BIN", &lba, &size));
    CHECK_EQ(lba, 100);
    CHECK_EQ(size, 12345);

    /* Found in block 1, only reachable by crossing the zero-length pad
       at the end of block 0 rather than stopping there. */
    lba = 0;
    size = 0;
    CHECK(discfmt_iso_find(dir, sizeof(dir), "SECOND.BIN", &lba, &size));
    CHECK_EQ(lba, 200);
    CHECK_EQ(size, 67890);

    /* Absent name: not found, not garbage. */
    CHECK(!discfmt_iso_find(dir, sizeof(dir), "NOPE.BIN", &lba, &size));

    /* Truncated directory: SECOND.BIN's record starts at byte 2048 but
       dir_len is cut a few bytes into it. The walk must fail closed
       instead of reading past dir_len to find it. */
    CHECK(!discfmt_iso_find(dir, 2048 + 10, "SECOND.BIN", &lba, &size));

    /* dir_len of 0: nothing to walk, must not touch dir at all. */
    CHECK(!discfmt_iso_find(dir, 0, "FIRST.BIN", &lba, &size));
}

static void test_cue_parse_multi_file(void)
{
    /* 42 tracks, one FILE per TRACK -- this disc's actual layout. Every
       filename here has both a space and parentheses, which a whitespace
       tokeniser would mangle; the parser must take the quoted string
       between the first and last '"' on the line, whole. */
    static char cue[1024 * 32];
    size_t off = 0;
    int n;
    DiscCue disc_cue;
    int single_file = -1;  /* poison: must come back 0 on a clean parse */
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

        /* Two seconds between INDEX 00 and INDEX 01, the real disc's layout. */
        CHECK_EQ(disc_cue.tracks[i].pregap_sectors, 150);
    }

    /* The data track declares INDEX 01 alone, so it has no pregap to skip. */
    CHECK_EQ(disc_cue.tracks[0].pregap_sectors, 0);
}

static void test_cue_parse_pregap_widths(void)
{
    /* Three shapes that all appear on this disc's own cue sheet, plus the
       no-INDEX-00 case that every generated cue in this repo emits. The
       00:02:01 tracks (21, 26 and 40 on the real rip) are why this is read
       off the cue instead of assumed to be a flat 150 everywhere. */
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

    /* 1 minute 3 seconds 2 frames = (60 + 3) * 75 + 2. Not a shape this disc
       uses; here to pin the minutes term, which 00:02:00 cannot. */
    CHECK_EQ(disc_cue.tracks[4].pregap_sectors, 63 * 75 + 2);
}

static void test_cue_parse_pregap_keyword_is_not_a_pregap(void)
{
    /* A PREGAP line means silence the burner generates and the image does NOT
       contain. Counting it would strip real audio off the front of the track,
       so it must leave pregap_sectors alone -- the opposite of INDEX 00. */
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
    /* INDEX 02 and up are legal subdivisions inside a track. They are not
       pregap and must not disturb the value INDEX 01 established. */
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
    /* INDEX 01 before INDEX 00 would yield a negative pregap. Silently
       clamping it to 0 would hide a corrupt cue behind audio that merely
       sounds late, which is the exact failure this whole field exists to
       stop being invisible. */
    static const char backwards[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 00 00:02:00\r\n"
        "    INDEX 01 00:00:00\r\n";

    /* An INDEX line with no TRACK to attach it to. */
    static const char orphan[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "    INDEX 01 00:00:00\r\n";

    /* Frames field at 75 and above is not a valid MSF -- 75 frames is one
       second, so it should have been carried. */
    static const char overflow[] =
        "FILE \"t01.bin\" BINARY\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 00:01:75\r\n";

    /* Truncated MSF: two fields where three are required. */
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
    /* A track with no INDEX 00 following one that had one must report 0, not
       inherit its neighbour's. The generated cues in this repo are all of the
       second kind, so an inherited value would strip audio off every one. */
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
    /* Two TRACK lines under one FILE: a single-file image. Accepting this
       silently would yield 42 tracks all pointing at the same file at
       offset zero -- universal data corruption, not a load failure at the
       one place that caused it. Must be a distinct, named failure. */
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
    /* A TRACK line before any FILE line is a different kind of malformed
       cue -- must fail, but must NOT be reported as the single-file case. */
    static const char cue[] = "  TRACK 01 MODE1/2352\r\n";
    DiscCue disc_cue;
    int single_file = -1;

    CHECK(!discfmt_cue_parse(cue, sizeof(cue) - 1, &disc_cue, &single_file));
    CHECK_EQ(single_file, 0);
}

#ifdef _WIN32
static void test_cue_parse_no_trailing_newline_track_number_bound(void)
{
    /* Critical regression: discfmt_cue_track_number had no bound of its
       own and relied on hitting a non-digit byte to know when to stop.
       A TRACK line's number sitting at the very end of the input, with no
       trailing newline, walked straight past text+len looking for one --
       ordinary input, since Task 4's disc_open reads a cue with fread into
       a sized buffer and hands over that exact length, and nothing
       guarantees a cue file's last line ends in a newline.

       A plain stack or heap buffer sitting next to unrelated-but-mapped
       bytes would not reliably fault even with the bug present, which is
       exactly why it survived the original test suite -- every existing
       fixture and the real disc's cue both end in a newline, so every scan
       always found its terminator before any bound would have mattered.
       This places the content at the very end of a committed page,
       immediately followed by a reserved-but-uncommitted page: any read
       past `len` here is guaranteed to raise an access violation, not
       merely read garbage. */
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

    /* Reserve two pages with no access, then commit only the first. The
       second stays reserved-only, so it acts as a guard page immediately
       after whatever is placed at the end of the first. */
    region = (uint8_t *)VirtualAlloc(NULL, page_size * 2, MEM_RESERVE, PAGE_NOACCESS);
    CHECK(region != NULL);
    if (region == NULL)
    {
        return;
    }

    first_page = (uint8_t *)VirtualAlloc(region, page_size, MEM_COMMIT, PAGE_READWRITE);
    CHECK(first_page == region);

    /* The cue text's last byte lands on the last byte of the committed
       page -- the guard page begins at the very next address. */
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
    /* Suppress the WER crash dialog so a genuine access violation in the
       guard-page regression below terminates the process instead of
       blocking forever on a dialog with no one to click it. */
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
