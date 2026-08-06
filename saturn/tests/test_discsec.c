/*----------------------
 | test_discsec.c
 | Description: Host unit tests for discsec.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: discsec.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "discsec.h"

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

#define SECTOR 2048

typedef struct {
    const char *name;
    int32_t bytes;
    int32_t whole;
    int32_t tail;
    int32_t sectors;
    int32_t last_sector_size;
} manifest_row;

/* Every distinct size in saturn/src/disc_manifest.h, plus the sectors and
   last-sector-size SRL reports for each. The last two columns are what
   disc_read_file_body cross-checks the split against, so they are pinned here
   in the same table rather than reasoned about at the call site. */
static const manifest_row MANIFEST[] = {
    { "INTRO1.BIN and the seven other 432,128-byte animations",
      432128, 211, 0, 211, SECTOR },
    { "GAME2.BIN", 409600, 200, 0, 200, SECTOR },
    { "MAKE2MB.BIN", 436224, 213, 0, 213, SECTOR },
    { "ROOMS1.BIN and the six other 370,688-byte rooms",
      370688, 181, 0, 181, SECTOR },

    /* The row that matters. ROOMS7.BIN is the only manifest file with a tail,
       so if the split ever silently becomes a round-up, this is what fails.
       78 * 2048 = 159,744, leaving 1,082 bytes in a 79th sector whose other
       966 bytes belong to nobody. */
    { "ROOMS7.BIN -- the only file with a tail",
      160826, 78, 1082, 79, 1082 },
};

static void test_manifest_sizes(void)
{
    size_t i;

    for (i = 0; i < sizeof(MANIFEST) / sizeof(MANIFEST[0]); i++) {
        const manifest_row *r = &MANIFEST[i];
        int32_t whole = discsec_whole_sectors(r->bytes, SECTOR);
        int32_t tail = discsec_tail_bytes(r->bytes, SECTOR);

        if (whole != r->whole || tail != r->tail) {
            g_fail++;
            printf("FAIL %s\n  actual   = %d whole + %d tail\n"
                   "  expected = %d whole + %d tail\n",
                   r->name, (int)whole, (int)tail,
                   (int)r->whole, (int)r->tail);
            continue;
        }

        /* The split must reconstruct the byte count exactly. */
        CHECK_EQ(whole * SECTOR + tail, r->bytes);

        /* And it must agree with the pair SRL reports, because that agreement
           is the guard disc_read_file_body refuses on. */
        CHECK_EQ(whole + (tail != 0 ? 1 : 0), r->sectors);
        CHECK_EQ(tail != 0 ? tail : SECTOR, r->last_sector_size);
    }
}

static void test_boundaries(void)
{
    CHECK_EQ(discsec_whole_sectors(0, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(0, SECTOR), 0);

    CHECK_EQ(discsec_whole_sectors(1, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(1, SECTOR), 1);

    /* One byte under a sector boundary: no whole sector at all. */
    CHECK_EQ(discsec_whole_sectors(2047, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(2047, SECTOR), 2047);

    /* Exactly one sector: no tail, and specifically not a second sector. */
    CHECK_EQ(discsec_whole_sectors(2048, SECTOR), 1);
    CHECK_EQ(discsec_tail_bytes(2048, SECTOR), 0);

    /* One byte over: one whole sector and a one-byte tail. */
    CHECK_EQ(discsec_whole_sectors(2049, SECTOR), 1);
    CHECK_EQ(discsec_tail_bytes(2049, SECTOR), 1);

    /* One byte under two sectors. */
    CHECK_EQ(discsec_whole_sectors(4095, SECTOR), 1);
    CHECK_EQ(discsec_tail_bytes(4095, SECTOR), 2047);
}

static void test_degenerate_inputs(void)
{
    /* A non-positive sector size is a divide by zero or worse. Both functions
       answer 0, so the caller reads nothing and fails its own length check. */
    CHECK_EQ(discsec_whole_sectors(160826, 0), 0);
    CHECK_EQ(discsec_tail_bytes(160826, 0), 0);
    CHECK_EQ(discsec_whole_sectors(160826, -1), 0);
    CHECK_EQ(discsec_tail_bytes(160826, -1), 0);

    /* A negative byte count is a size field that was never filled in.
       C99 truncates toward zero, so -1 / 2048 would be 0 and -1 % 2048 would
       be -1 -- a negative memcpy length. Refused explicitly instead. */
    CHECK_EQ(discsec_whole_sectors(-1, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(-1, SECTOR), 0);
    CHECK_EQ(discsec_whole_sectors(-160826, SECTOR), 0);
    CHECK_EQ(discsec_tail_bytes(-160826, SECTOR), 0);
}

static void test_sector_ceiling(void)
{
    /* The bounce buffer is built for this many bytes, so the constant and the
       Mode 1 sector size the split is exercised at must not drift apart. */
    CHECK_EQ(DISC_MAX_SECTOR_BYTES, 2048);
    CHECK_EQ(DISC_MAX_SECTOR_BYTES, SECTOR);

    /* A Mode 2 sector still splits correctly -- the arithmetic has no ceiling
       of its own. Refusing it is disc_read_file_body's job, against
       DISC_MAX_SECTOR_BYTES, because that is where the fixed buffer lives. */
    CHECK_EQ(discsec_whole_sectors(4672, 2336), 2);
    CHECK_EQ(discsec_tail_bytes(4672, 2336), 0);
    CHECK_EQ(discsec_whole_sectors(4673, 2336), 2);
    CHECK_EQ(discsec_tail_bytes(4673, 2336), 1);
}

static int drain(int32_t total, int32_t max_chunk, int32_t *out_requests)
{
    int32_t remaining = total;
    int32_t summed = 0;
    int32_t requests = 0;

    while (remaining > 0 && requests < 1000) {
        int32_t take = discsec_request_sectors(remaining, max_chunk);

        if (take <= 0) {
            break;
        }

        summed += take;
        remaining -= take;
        requests++;
    }

    *out_requests = requests;
    return summed;
}

static void test_request_chunking(void)
{
    int32_t requests = 0;

    /* Every manifest sector count drains exactly, with no chunk lost and none
       invented. A loop that terminates but sums short is the failure that
       would truncate a room and surface far from here. */
    CHECK_EQ(drain(211, DISC_MAX_REQUEST_SECTORS, &requests), 211);
    CHECK_EQ(requests, 2);
    CHECK_EQ(drain(213, DISC_MAX_REQUEST_SECTORS, &requests), 213);
    CHECK_EQ(requests, 2);
    CHECK_EQ(drain(200, DISC_MAX_REQUEST_SECTORS, &requests), 200);
    CHECK_EQ(requests, 2);
    CHECK_EQ(drain(181, DISC_MAX_REQUEST_SECTORS, &requests), 181);
    CHECK_EQ(requests, 2);
    CHECK_EQ(drain(79, DISC_MAX_REQUEST_SECTORS, &requests), 79);
    CHECK_EQ(requests, 1);
    CHECK_EQ(drain(1, DISC_MAX_REQUEST_SECTORS, &requests), 1);
    CHECK_EQ(requests, 1);

    /* 211 against 128 is the animation case that hung, and it must be the two
       requests the ceiling implies -- 128 then the 83 that are left. */
    CHECK_EQ(discsec_request_sectors(211, 128), 128);
    CHECK_EQ(discsec_request_sectors(83, 128), 83);

    /* A request never exceeds the ceiling or the remainder, whichever binds. */
    CHECK_EQ(discsec_request_sectors(1000, 128), 128);
    CHECK_EQ(discsec_request_sectors(5, 128), 5);
    CHECK_EQ(discsec_request_sectors(128, 128), 128);

    /* Degenerate inputs return 0 so the caller's loop terminates instead of
       spinning on a request it can never satisfy. */
    CHECK_EQ(discsec_request_sectors(0, 128), 0);
    CHECK_EQ(discsec_request_sectors(-1, 128), 0);
    CHECK_EQ(discsec_request_sectors(211, 0), 0);
    CHECK_EQ(discsec_request_sectors(211, -1), 0);
    CHECK_EQ(discsec_request_sectors(0, 0), 0);

    /* The ceiling is the figure the emulator run justified, not a round
       number chosen for looks: below the 200 that worked, above half of it. */
    CHECK_EQ(DISC_MAX_REQUEST_SECTORS, 128);
}

int main(void)
{
    test_manifest_sizes();
    test_boundaries();
    test_degenerate_inputs();
    test_sector_ceiling();
    test_request_chunking();

    if (g_fail != 0) {
        printf("%d discsec check(s) failed\n", g_fail);
        return 1;
    }

    printf("discsec: all checks passed\n");
    return 0;
}
