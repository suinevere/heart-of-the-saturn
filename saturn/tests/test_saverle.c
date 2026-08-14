/*----------------------
 | test_saverle.c
 | Description: Host unit tests for saverle.c. Built and run by run_tests.sh
 |   with the host gcc, never by the Saturn makefile -- that globs src/ under
 |   saturn/, so this directory is excluded automatically.
 | Author: suinevere
 | Dependencies: saverle.h, stdio.h, string.h
 ----------------------*/
#include <stdio.h>
#include <string.h>
#include "saverle.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

static void expect_bytes(const char *what, const unsigned char *got,
                         const unsigned char *want, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (got[i] != want[i]) {
            g_fail++;
            printf("FAIL %s\n  byte %d actual = %02x expected = %02x\n",
                   what, i, got[i], want[i]);
            return;
        }
    }
}

static void roundtrip(const char *what, const unsigned char *src, int srcLen)
{
    unsigned char enc[8192];
    unsigned char dec[8192];
    int encLen = saverle_encode(src, srcLen, enc, (int)sizeof(enc));
    int decLen;

    if (encLen < 0) {
        return;
    }
    decLen = saverle_decode(enc, encLen, dec, (int)sizeof(dec));
    expect_int(what, decLen, srcLen);
    if (decLen == srcLen) {
        expect_bytes(what, dec, src, srcLen);
    }
}

static void test_all_zeros(void)
{
    unsigned char src[4096];
    unsigned char enc[4096];
    int encLen;

    memset(src, 0, sizeof(src));
    encLen = saverle_encode(src, (int)sizeof(src), enc, (int)sizeof(enc));
    if (encLen < 0 || encLen >= (int)sizeof(src)) {
        g_fail++;
        printf("FAIL all zeros must compress\n  actual   = %d\n"
               "  expected = a length under %d\n", encLen, (int)sizeof(src));
    }
    roundtrip("all zeros roundtrip", src, (int)sizeof(src));
}

static void test_no_runs(void)
{
    unsigned char src[512];
    int i;
    for (i = 0; i < (int)sizeof(src); i++) {
        src[i] = (unsigned char)(i * 7 + (i >> 3));
    }
    roundtrip("no runs roundtrip", src, (int)sizeof(src));
}

static void test_alternating(void)
{
    unsigned char src[256];
    int i;
    for (i = 0; i < (int)sizeof(src); i++) {
        src[i] = (unsigned char)(i & 1 ? 0xAA : 0x55);
    }
    roundtrip("alternating roundtrip", src, (int)sizeof(src));
}

static void test_boundary_lengths(void)
{
    unsigned char src[600];
    int lens[6];
    int n, i;

    lens[0] = 1;   lens[1] = 2;   lens[2] = 128;
    lens[3] = 129; lens[4] = 130; lens[5] = 131;

    for (n = 0; n < 6; n++) {
        for (i = 0; i < lens[n]; i++) {
            src[i] = 0x42;
        }
        roundtrip("boundary run roundtrip", src, lens[n]);
        for (i = 0; i < lens[n]; i++) {
            src[i] = (unsigned char)i;
        }
        roundtrip("boundary literal roundtrip", src, lens[n]);
    }
}

static void test_encode_declines_on_expansion(void)
{
    unsigned char src[64];
    unsigned char enc[64];
    int i;
    for (i = 0; i < (int)sizeof(src); i++) {
        src[i] = (unsigned char)(i * 31);
    }
    expect_int("incompressible input declines",
               saverle_encode(src, (int)sizeof(src), enc, (int)sizeof(enc)), -1);
}

static void test_encode_declines_on_small_dst(void)
{
    unsigned char src[256];
    unsigned char enc[4];
    memset(src, 0, sizeof(src));
    expect_int("dst too small declines",
               saverle_encode(src, (int)sizeof(src), enc, (int)sizeof(enc)), -1);
}

static void test_decode_rejects_truncated(void)
{
    unsigned char enc[2];
    unsigned char dec[64];

    enc[0] = 0x05;
    expect_int("truncated literal rejected",
               saverle_decode(enc, 1, dec, (int)sizeof(dec)), -1);

    enc[0] = 0x80;
    expect_int("truncated repeat rejected",
               saverle_decode(enc, 1, dec, (int)sizeof(dec)), -1);
}

static void test_decode_rejects_overflow(void)
{
    unsigned char enc[2];
    unsigned char dec[4];

    enc[0] = 0xFF;
    enc[1] = 0x11;
    expect_int("output overflow rejected",
               saverle_decode(enc, 2, dec, (int)sizeof(dec)), -1);
}

static void test_decode_rejects_zero_length(void)
{
    unsigned char dec[4];
    expect_int("empty input rejected", saverle_decode(dec, 0, dec, 4), -1);
}

int main(void)
{
    test_all_zeros();
    test_no_runs();
    test_alternating();
    test_boundary_lengths();
    test_encode_declines_on_expansion();
    test_encode_declines_on_small_dst();
    test_decode_rejects_truncated();
    test_decode_rejects_overflow();
    test_decode_rejects_zero_length();

    if (g_fail != 0) {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_saverle: all passed\n");
    return 0;
}
