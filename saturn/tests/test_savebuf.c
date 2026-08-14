/*----------------------
 | test_savebuf.c
 | Description: Host unit tests for savebuf.c. Built and run by run_tests.sh
 |   with the host gcc.
 | Author: suinevere
 | Dependencies: savebuf.h, stdio.h
 ----------------------*/
#include <stdio.h>
#include "savebuf.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void test_write_then_read_back(void)
{
    unsigned char store[8];
    savebuf w, r;
    int i;

    savebuf_open_write(&w, store, (int)sizeof(store));
    for (i = 0; i < 5; i++) {
        expect_int("putc returns the byte", savebuf_putc(&w, 0x10 + i), 0x10 + i);
    }
    expect_int("length after five writes", savebuf_len(&w), 5);
    expect_int("no error after five writes", savebuf_error(&w), 0);

    savebuf_open_read(&r, store, 5);
    for (i = 0; i < 5; i++) {
        expect_int("getc returns what was written", savebuf_getc(&r), 0x10 + i);
    }
    expect_int("no error after five reads", savebuf_error(&r), 0);
}

static void test_getc_at_eof(void)
{
    unsigned char store[2];
    savebuf r;

    store[0] = 0x99;
    savebuf_open_read(&r, store, 1);
    expect_int("first read succeeds", savebuf_getc(&r), 0x99);
    expect_int("read past end returns -1", savebuf_getc(&r), -1);
    expect_int("read past end sets error", savebuf_error(&r) != 0, 1);
}

static void test_putc_overflow_does_not_scribble(void)
{
    unsigned char store[2];
    savebuf w;

    store[0] = 0;
    store[1] = 0;
    savebuf_open_write(&w, store, 2);
    expect_int("first write", savebuf_putc(&w, 0xAA), 0xAA);
    expect_int("second write", savebuf_putc(&w, 0xBB), 0xBB);
    expect_int("third write refused", savebuf_putc(&w, 0xCC), -1);
    expect_int("overflow sets error", savebuf_error(&w) != 0, 1);
    expect_int("length stays at capacity", savebuf_len(&w), 2);
    expect_int("byte 0 intact", (int)store[0], 0xAA);
    expect_int("byte 1 intact", (int)store[1], 0xBB);
}

static void test_write_is_masked_to_a_byte(void)
{
    unsigned char store[2];
    savebuf w, r;

    savebuf_open_write(&w, store, 2);
    savebuf_putc(&w, 0x141);
    savebuf_open_read(&r, store, 1);
    expect_int("value masked to eight bits", savebuf_getc(&r), 0x41);
}

static void test_read_mode_rejects_writes(void)
{
    unsigned char store[2];
    savebuf r;

    store[0] = 1;
    savebuf_open_read(&r, store, 1);
    expect_int("putc on a read buffer refused", savebuf_putc(&r, 0x22), -1);
    expect_int("putc on a read buffer sets error", savebuf_error(&r) != 0, 1);
}

static void test_write_mode_rejects_reads(void)
{
    unsigned char store[2];
    savebuf w;

    savebuf_open_write(&w, store, 2);
    savebuf_putc(&w, 0x33);
    expect_int("getc on a write buffer refused", savebuf_getc(&w), -1);
    expect_int("getc on a write buffer sets error", savebuf_error(&w) != 0, 1);
}

int main(void)
{
    test_write_then_read_back();
    test_getc_at_eof();
    test_putc_overflow_does_not_scribble();
    test_write_is_masked_to_a_byte();
    test_read_mode_rejects_writes();
    test_write_mode_rejects_reads();

    if (g_fail != 0) {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_savebuf: all passed\n");
    return 0;
}
