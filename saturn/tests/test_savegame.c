#include <stdio.h>
#include <string.h>
#include "savegame.h"
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

static void fill_compressible(unsigned char *p, int len)
{
    memset(p, 0, (size_t)len);
    p[0] = 0x11;
    p[len - 1] = 0x22;
}

static void fill_incompressible(unsigned char *p, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        p[i] = (unsigned char)(i * 31 + (i >> 5));
    }
}

static void test_upstream_size_matches_the_engine(void)
{
    expect_int("upstream payload size",
               SAVE_UPSTREAM_BYTES,
               3 + 512 + (64 * 32 * 2) + (64 * 4 * 2) + (64 * 16) + 3);
}

static void test_trailer_roundtrip(void)
{
    unsigned char t[SAVE_TRAILER_BYTES];
    int track = 0, loop = 0;

    savegame_pack_trailer(t, 7, 1);
    savegame_unpack_trailer(t, &track, &loop);
    expect_int("trailer track", track, 7);
    expect_int("trailer loop", loop, 1);

    savegame_pack_trailer(t, -1, 0);
    savegame_unpack_trailer(t, &track, &loop);
    expect_int("trailer no track", track, -1);
    expect_int("trailer no loop", loop, 0);
}

static void test_compressible_roundtrip(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char back[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];
    unsigned char stored[SAVE_MAX_BYTES];
    unsigned short room = 0;
    int len = 0;
    int storedLen;

    stub_bup_reset();
    fill_compressible(src, (int)sizeof(src));

    expect_int("write a compressible save",
               savegame_write(SAT_BUP_INTERNAL, 0, src, (int)sizeof(src), 23,
                              work, (int)sizeof(work)), SAT_BUP_OK);

    storedLen = stub_bup_fetch(SAT_BUP_INTERNAL, "HOTASAVE1", stored,
                               (int)sizeof(stored));
    if (storedLen >= (int)sizeof(src)) {
        g_fail++;
        printf("FAIL compressible save should be smaller than raw\n"
               "  actual   = %d\n  expected = under %d\n",
               storedLen, (int)sizeof(src));
    }
    expect_int("stored flag says RLE", (int)(stored[6] & SAVE_FLAG_RLE), 1);

    memset(back, 0xEE, sizeof(back));
    expect_int("read it back",
               savegame_read(SAT_BUP_INTERNAL, 0, back, (int)sizeof(back), &len,
                             &room, work, (int)sizeof(work)), SAT_BUP_OK);
    expect_int("length restored", len, (int)sizeof(src));
    expect_int("room restored", (int)room, 23);
    expect_int("bytes restored", memcmp(back, src, sizeof(src)), 0);
}

static void test_incompressible_is_stored_raw(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char back[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];
    unsigned char stored[SAVE_MAX_BYTES];
    unsigned short room = 0;
    int len = 0;

    stub_bup_reset();
    fill_incompressible(src, (int)sizeof(src));

    expect_int("write an incompressible save",
               savegame_write(SAT_BUP_INTERNAL, 0, src, (int)sizeof(src), 5,
                              work, (int)sizeof(work)), SAT_BUP_OK);
    stub_bup_fetch(SAT_BUP_INTERNAL, "HOTASAVE1", stored, (int)sizeof(stored));
    expect_int("stored flag says raw", (int)(stored[6] & SAVE_FLAG_RLE), 0);

    expect_int("read it back",
               savegame_read(SAT_BUP_INTERNAL, 0, back, (int)sizeof(back), &len,
                             &room, work, (int)sizeof(work)), SAT_BUP_OK);
    expect_int("bytes restored", memcmp(back, src, sizeof(src)), 0);
}

static void test_missing_slot(void)
{
    unsigned char back[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];
    unsigned short room = 0;
    int len = 0;

    stub_bup_reset();
    memset(back, 0xEE, sizeof(back));
    expect_int("missing slot",
               savegame_read(SAT_BUP_INTERNAL, 0, back, (int)sizeof(back), &len,
                             &room, work, (int)sizeof(work)),
               SAT_BUP_ERR_NOT_FOUND);
    expect_int("payload untouched", (int)back[0], 0xEE);
}

static void test_there_is_only_one_slot(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char back[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];
    unsigned short room = 0;
    int len = 0;

    stub_bup_reset();
    fill_compressible(src, (int)sizeof(src));
    expect_int("slot 1 is outside the one slot a device holds",
               savegame_write(SAT_BUP_INTERNAL, 1, src, (int)sizeof(src), 9,
                              work, (int)sizeof(work)),
               SAVE_ERR_BAD_SLOT);
    expect_int("and reading it is refused the same way",
               savegame_read(SAT_BUP_INTERNAL, 1, back, (int)sizeof(back),
                             &len, &room, work, (int)sizeof(work)),
               SAVE_ERR_BAD_SLOT);
    expect_int("as is a checkpoint write",
               savegame_write_checkpoint(SAT_BUP_INTERNAL, 1, 4, 3, work,
                                         (int)sizeof(work)),
               SAVE_ERR_BAD_SLOT);
}

static void test_bad_magic_leaves_payload_untouched(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char back[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];
    unsigned char stored[SAVE_MAX_BYTES];
    unsigned short room = 0;
    int len = 0;
    int storedLen;

    stub_bup_reset();
    fill_compressible(src, (int)sizeof(src));
    savegame_write(SAT_BUP_INTERNAL, 0, src, (int)sizeof(src), 1, work,
                   (int)sizeof(work));
    storedLen = stub_bup_fetch(SAT_BUP_INTERNAL, "HOTASAVE1", stored,
                               (int)sizeof(stored));
    stored[1] = 'X';
    stub_bup_place(SAT_BUP_INTERNAL, "HOTASAVE1", stored, storedLen);

    memset(back, 0xEE, sizeof(back));
    expect_int("bad magic refused",
               savegame_read(SAT_BUP_INTERNAL, 0, back, (int)sizeof(back), &len,
                             &room, work, (int)sizeof(work)),
               SAVE_ERR_BAD_MAGIC);
    expect_int("payload untouched", (int)back[0], 0xEE);
}

static void test_bad_version_leaves_payload_untouched(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char back[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];
    unsigned char stored[SAVE_MAX_BYTES];
    unsigned short room = 0;
    int len = 0;
    int storedLen;

    stub_bup_reset();
    fill_compressible(src, (int)sizeof(src));
    savegame_write(SAT_BUP_INTERNAL, 0, src, (int)sizeof(src), 1, work,
                   (int)sizeof(work));
    storedLen = stub_bup_fetch(SAT_BUP_INTERNAL, "HOTASAVE1", stored,
                               (int)sizeof(stored));
    stored[5] = (unsigned char)(SAVE_FORMAT_VERSION + 1);
    stub_bup_place(SAT_BUP_INTERNAL, "HOTASAVE1", stored, storedLen);

    memset(back, 0xEE, sizeof(back));
    expect_int("wrong version refused",
               savegame_read(SAT_BUP_INTERNAL, 0, back, (int)sizeof(back), &len,
                             &room, work, (int)sizeof(work)),
               SAVE_ERR_BAD_VERSION);
    expect_int("payload untouched", (int)back[0], 0xEE);
}

static void test_corrupt_rle_stream_is_refused(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char back[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];
    unsigned char stored[SAVE_MAX_BYTES];
    unsigned short room = 0;
    int len = 0;
    int fileLen;

    stub_bup_reset();
    fill_compressible(src, (int)sizeof(src));
    savegame_write(SAT_BUP_INTERNAL, 0, src, (int)sizeof(src), 1, work,
                   (int)sizeof(work));
    fileLen = stub_bup_fetch(SAT_BUP_INTERNAL, "HOTASAVE1", stored,
                             (int)sizeof(stored));
    stored[fileLen - 2] = 0xFF;
    stub_bup_place(SAT_BUP_INTERNAL, "HOTASAVE1", stored, fileLen);

    expect_int("corrupt RLE stream refused",
               savegame_read(SAT_BUP_INTERNAL, 0, back, (int)sizeof(back), &len,
                             &room, work, (int)sizeof(work)),
               SAVE_ERR_BAD_PAYLOAD);
}

static void test_device_full(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];
    unsigned char stored[SAVE_MAX_BYTES];
    int before;

    stub_bup_reset();
    fill_compressible(src, (int)sizeof(src));
    savegame_write(SAT_BUP_INTERNAL, 0, src, (int)sizeof(src), 9, work,
                   (int)sizeof(work));
    before = stub_bup_fetch(SAT_BUP_INTERNAL, "HOTASAVE1", stored,
                            (int)sizeof(stored));

    stub_bup_set_device(SAT_BUP_INTERNAL, 1, 1, 0, 8);
    expect_int("full device refuses",
               savegame_write(SAT_BUP_INTERNAL, 0, src, (int)sizeof(src), 9,
                              work, (int)sizeof(work)),
               SAT_BUP_ERR_NO_SPACE);
    expect_int("and the save that was already there survives the refusal",
               stub_bup_fetch(SAT_BUP_INTERNAL, "HOTASAVE1", stored,
                              (int)sizeof(stored)),
               before);
}

static void test_write_rejects_oversized_payload(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];

    stub_bup_reset();
    fill_compressible(src, (int)sizeof(src));
    expect_int("payload longer than the format allows",
               savegame_write(SAT_BUP_INTERNAL, 0, src, SAVE_PAYLOAD_MAX + 1,
                              9, work, (int)sizeof(work)),
               SAVE_ERR_TOO_LARGE);
}

static void test_write_rejects_small_work_buffer(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char work[16];

    stub_bup_reset();
    fill_compressible(src, (int)sizeof(src));
    expect_int("work buffer too small",
               savegame_write(SAT_BUP_INTERNAL, 0, src, (int)sizeof(src), 9,
                              work, (int)sizeof(work)),
               SAVE_ERR_TOO_LARGE);
}

static void test_checkpoint_slot_roundtrip(void)
{
    unsigned char work[SAVE_MAX_BYTES];
    unsigned short room = 0;
    unsigned char entry = 0;

    stub_bup_reset();
    expect_int("a checkpoint slot writes",
               savegame_write_checkpoint(SAT_BUP_INTERNAL, 0, 4, 3, work,
                                         (int)sizeof(work)),
               SAT_BUP_OK);
    expect_int("and reads back",
               savegame_read_checkpoint(SAT_BUP_INTERNAL, 0, &room, &entry,
                                        work, (int)sizeof(work)),
               SAT_BUP_OK);
    expect_int("with its room", (int)room, 4);
    expect_int("and its entry", (int)entry, 3);
}

static void test_the_two_slot_kinds_refuse_each_other(void)
{
    unsigned char src[SAVE_PAYLOAD_MAX];
    unsigned char work[SAVE_MAX_BYTES];
    unsigned char payload[SAVE_PAYLOAD_MAX];
    unsigned short room = 0;
    unsigned char entry = 0;
    int payloadLen = 0;

    stub_bup_reset();
    fill_compressible(src, (int)sizeof(src));
    expect_int("a state save writes",
               savegame_write(SAT_BUP_INTERNAL, 0, src, (int)sizeof(src), 9,
                              work, (int)sizeof(work)),
               SAT_BUP_OK);
    expect_int("and is not a checkpoint",
               savegame_read_checkpoint(SAT_BUP_INTERNAL, 0, &room, &entry,
                                        work, (int)sizeof(work)),
               SAVE_ERR_BAD_PAYLOAD);

    stub_bup_reset();
    expect_int("a checkpoint save writes",
               savegame_write_checkpoint(SAT_BUP_INTERNAL, 0, 5, 1, work,
                                         (int)sizeof(work)),
               SAT_BUP_OK);
    expect_int("and is not a state save",
               savegame_read(SAT_BUP_INTERNAL, 0, payload, (int)sizeof(payload),
                             &payloadLen, &room, work, (int)sizeof(work)),
               SAVE_ERR_BAD_PAYLOAD);
}

static void test_checkpoint_refuses_a_missing_slot(void)
{
    unsigned char work[SAVE_MAX_BYTES];
    unsigned short room = 7;
    unsigned char entry = 7;

    stub_bup_reset();
    expect_int("nothing there",
               savegame_read_checkpoint(SAT_BUP_INTERNAL, 0, &room, &entry,
                                        work, (int)sizeof(work)),
               SAT_BUP_ERR_NOT_FOUND);
    expect_int("and the outputs are untouched", (int)room, 7);
}

int main(void)
{
    test_upstream_size_matches_the_engine();
    test_trailer_roundtrip();
    test_compressible_roundtrip();
    test_incompressible_is_stored_raw();
    test_missing_slot();
    test_there_is_only_one_slot();
    test_bad_magic_leaves_payload_untouched();
    test_bad_version_leaves_payload_untouched();
    test_corrupt_rle_stream_is_refused();
    test_device_full();
    test_write_rejects_oversized_payload();
    test_write_rejects_small_work_buffer();
    test_checkpoint_slot_roundtrip();
    test_the_two_slot_kinds_refuse_each_other();
    test_checkpoint_refuses_a_missing_slot();

    if (g_fail != 0) {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_savegame: all passed\n");
    return 0;
}
