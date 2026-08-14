# Backup RAM Saves Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the Saturn build real saves in backup RAM, so a player can write a slot, power off, and load back both the game state and the CD-DA track that was playing.

**Architecture:** The engine's existing `quicksave()`/`quickload()` stay byte-for-byte unmodified and keep writing through `FILE*`; a RAM-backed `FILE` in the stdio stub layer points that stream at a buffer instead of a filesystem. Around it sit four pure-C modules — RLE, the buffer, slot metadata, and the header/trailer orchestration — all reachable by the host test harness, plus one `.cxx` that is the only file touching SGL's BUP vector table.

**Tech Stack:** C99 for everything host-testable; C++ only for `system/saturn_backup.cxx` (SRL + `sega_bup.h`). Host tests are plain gcc. Saturn build is the existing `compile.bat`.

**Spec:** `docs/superpowers/specs/2026-08-13-hota-saturn-backup-saves-design.md`

## Global Constraints

- **Every method, constant and file gets a banner comment** in the project's form (`| name`, `| Description:`, `| Author: suinevere`, `| Dependencies:`, `| Globals:`, `| Params:`, `| Returns:`, `N/A` where inapplicable). Tests get a file banner only.
- **No comments inside functions.** Prose is one sentence; say the non-obvious thing once.
- **Author of record is `suinevere`.** Commit messages are one sentence, no body, no bullets, no trailers. Never mention Claude, AI, or the session.
- **New C files are `.c`, new C++ files are `.cxx`.** The Saturn makefile's pattern rules only cover those two extensions (`saturn/makefile:68-77`); a `.cpp` silently never builds.
- **Anything a host test reaches must be pure C99** with no `srl.hpp`, no `sega_bup.h`, and no engine headers beyond what is listed. Host tests build with `gcc -std=c99 -Wall -Wextra -Werror -O1 -g`, so an unused parameter is a build failure.
- **`quicksave()`, `quickload()`, `quicksave_sprites()` and `quickload_sprites()` must end this plan unmodified.** Verified in Task 8.
- **New bulk buffers go in LWRAM** via `saturn_lwram_alloc` (`saturn_compat.h:196`), never HWRAM BSS. HWRAM is 770,048 bytes against 1,882,592 of measured `.bss`; the boot sequence already missed SDDRVS.TSK by 366 bytes, silently.
- **`SAVE_MAX_BYTES` is defined exactly once**, in `savedata.h`. The reference repo declared it twice with different values; do not repeat that.

## File Structure

| File | Responsibility | Reached by tests |
| --- | --- | --- |
| `saturn/src/saverle.h` / `.c` | PackBits-style RLE encode/decode with an explicit decline | Yes |
| `saturn/src/savebuf.h` / `.c` | Cursor over a byte buffer: sequential put/get with overflow and EOF | Yes |
| `saturn/src/savedata.h` / `.c` | Slot names, 48-byte header pack/unpack, slot probing, device defaulting, BUP date split | Yes |
| `saturn/src/savegame.h` / `.c` | Header + payload + trailer assembly, compression choice, backup read/write, validation order | Yes |
| `saturn/src/system/saturn_backup.h` | C interface to backup RAM; pure declarations, includable from C | Types only |
| `saturn/src/system/saturn_backup.cxx` | The only file including `sega_bup.h` | No |
| `saturn/src/system/saturn_saveslot.cxx` | Glue: LWRAM buffers, `quicksave()`/`quickload()`, the trailer's track | No |
| `saturn/src/system/saturn_filestub.c` | *Modified.* stdio file half now backed by `savebuf` | No |
| `saturn/src/system/disc_srl.cxx` | *Modified.* Getter for `g_musicTrack` / `g_musicLoop` | No |
| `saturn/src/disc.h` | *Modified.* Declares that getter | No |
| `saturn/src/main.c` | *Modified.* `sat_bup_init()`, and the per-frame save poll | No |
| `saturn/src/system/input_srl.cxx` | *Modified.* Debug chord read | No |
| `saturn/tests/stub_saturn_backup.c` | Host stand-in for the BUP layer, with failure injection | — |
| `saturn/tests/test_saverle.c` … `test_savegame.c` | Four new suites | — |

---

### Task 1: RLE codec

**Files:**
- Create: `saturn/src/saverle.h`, `saturn/src/saverle.c`
- Test: `saturn/tests/test_saverle.c`
- Modify: `saturn/tests/run_tests.sh` (append one suite)

**Interfaces:**
- Consumes: nothing.
- Produces: `int saverle_encode(const unsigned char *src, int srcLen, unsigned char *dst, int dstCap)` returning the encoded length or `-1`; `int saverle_decode(const unsigned char *src, int srcLen, unsigned char *dst, int dstCap)` returning the decoded length or `-1`.

The format, fixed here and referenced by later tasks. One control byte, then data:

| Control | Meaning |
| --- | --- |
| `0x00`–`0x7F` | Literal run: the next `c + 1` bytes are copied verbatim (1–128) |
| `0x80`–`0xFF` | Repeat run: the next single byte repeats `(c - 0x80) + 3` times (3–130) |

Minimum repeat length is 3, so a repeat is always strictly smaller than the literals it replaces.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_saverle.c`:

```c
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

static void roundtrip_must_compress(const char *what, const unsigned char *src,
                                    int srcLen)
{
    unsigned char enc[8192];
    unsigned char dec[8192];
    int encLen = saverle_encode(src, srcLen, enc, (int)sizeof(enc));
    int decLen;

    if (encLen <= 0) {
        g_fail++;
        printf("FAIL %s\n  actual   = encode declined (%d)\n"
               "  expected = a compressed length\n", what, encLen);
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

static void test_mixed_runs_and_literals(void)
{
    unsigned char src[300];
    int i;

    memset(src, 0x00, sizeof(src));
    for (i = 100; i < 110; i++) {
        src[i] = (unsigned char)(0x40 + i);
    }
    for (i = 200; i < 205; i++) {
        src[i] = (unsigned char)(0x90 + i);
    }
    roundtrip_must_compress("mixed runs and literals roundtrip", src,
                            (int)sizeof(src));
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
    unsigned char enc[3];
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
    test_mixed_runs_and_literals();
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
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src -o run_tests_saverle test_saverle.c ../src/saverle.c`
Expected: FAIL — `saverle.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/saverle.h`:

```c
/*----------------------
 | saverle.h
 | Description: A PackBits-style run-length codec for save payloads. One
 |   control byte then data: 0x00-0x7F is a literal run of c+1 bytes, 0x80-0xFF
 |   repeats the following byte (c-0x80)+3 times. The minimum repeat is three
 |   so a repeat is always strictly smaller than the literals it replaces.
 |
 |   Pure C with no engine, SRL or SGL dependency, so run_tests.sh can build it
 |   with the host gcc.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef SAVERLE_H
#define SAVERLE_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAVERLE_MAX_LITERAL / SAVERLE_MIN_RUN / SAVERLE_MAX_RUN
 | Description: The three lengths the control byte can express.
 | Author: suinevere
 ----------------------*/
#define SAVERLE_MAX_LITERAL 128
#define SAVERLE_MIN_RUN       3
#define SAVERLE_MAX_RUN     130

/*----------------------
 | saverle_encode
 | Description: Compresses src into dst, declining rather than growing. A
 |   return of -1 is a normal outcome and means the caller should store the
 |   input uncompressed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: src -- input; srcLen -- its length; dst -- output; dstCap -- output
 |         capacity
 | Returns: the encoded length, or -1 if the encoding would not fit in dstCap,
 |          would not be smaller than srcLen, or srcLen is not positive
 ----------------------*/
int saverle_encode(const unsigned char *src, int srcLen,
                   unsigned char *dst, int dstCap);

/*----------------------
 | saverle_decode
 | Description: Expands src into dst. Every length is checked against both
 |   ends before it is used, so malformed or truncated input is refused rather
 |   than read or written past a buffer.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: src -- encoded input; srcLen -- its length; dst -- output; dstCap --
 |         output capacity
 | Returns: the decoded length, or -1 on truncated input, an overrun of dstCap,
 |          or a non-positive srcLen
 ----------------------*/
int saverle_decode(const unsigned char *src, int srcLen,
                   unsigned char *dst, int dstCap);

#ifdef __cplusplus
}
#endif

#endif /* SAVERLE_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/saverle.c`:

```c
/*----------------------
 | saverle.c
 | Description: The codec described by saverle.h.
 | Author: suinevere
 | Dependencies: saverle.h
 ----------------------*/
#include "saverle.h"

/*----------------------
 | run_length_at
 | Description: How many times the byte at src[pos] repeats, capped at the
 |   longest run a control byte can express.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: src -- input; srcLen -- its length; pos -- where to look
 | Returns: a count of at least 1
 ----------------------*/
static int run_length_at(const unsigned char *src, int srcLen, int pos)
{
    int n = 1;
    while (pos + n < srcLen && src[pos + n] == src[pos] && n < SAVERLE_MAX_RUN) {
        n++;
    }
    return n;
}

/*----------------------
 | emit_literals
 | Description: Writes one literal control byte and its payload.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: src -- input; from -- first literal; count -- how many, 1 to 128;
 |         dst -- output; dstCap -- capacity; outPos -- write cursor, advanced
 | Returns: 0 on success, -1 if it would not fit
 ----------------------*/
static int emit_literals(const unsigned char *src, int from, int count,
                         unsigned char *dst, int dstCap, int *outPos)
{
    int i;
    if (*outPos + 1 + count > dstCap) {
        return -1;
    }
    dst[(*outPos)++] = (unsigned char)(count - 1);
    for (i = 0; i < count; i++) {
        dst[(*outPos)++] = src[from + i];
    }
    return 0;
}

int saverle_encode(const unsigned char *src, int srcLen,
                   unsigned char *dst, int dstCap)
{
    int inPos = 0;
    int outPos = 0;
    int litStart = 0;
    int litCount = 0;

    if (srcLen <= 0 || dstCap <= 0) {
        return -1;
    }

    while (inPos < srcLen) {
        int run = run_length_at(src, srcLen, inPos);

        if (run >= SAVERLE_MIN_RUN) {
            if (litCount > 0) {
                if (emit_literals(src, litStart, litCount, dst, dstCap, &outPos) < 0) {
                    return -1;
                }
                litCount = 0;
            }
            if (outPos + 2 > dstCap) {
                return -1;
            }
            dst[outPos++] = (unsigned char)(0x80 + (run - SAVERLE_MIN_RUN));
            dst[outPos++] = src[inPos];
            inPos += run;
            litStart = inPos;
        } else {
            if (litCount == 0) {
                litStart = inPos;
            }
            litCount++;
            inPos++;
            if (litCount == SAVERLE_MAX_LITERAL) {
                if (emit_literals(src, litStart, litCount, dst, dstCap, &outPos) < 0) {
                    return -1;
                }
                litCount = 0;
                litStart = inPos;
            }
        }
        if (outPos >= srcLen) {
            return -1;
        }
    }

    if (litCount > 0) {
        if (emit_literals(src, litStart, litCount, dst, dstCap, &outPos) < 0) {
            return -1;
        }
    }

    if (outPos >= srcLen) {
        return -1;
    }
    return outPos;
}

int saverle_decode(const unsigned char *src, int srcLen,
                   unsigned char *dst, int dstCap)
{
    int inPos = 0;
    int outPos = 0;

    if (srcLen <= 0 || dstCap <= 0) {
        return -1;
    }

    while (inPos < srcLen) {
        unsigned char c = src[inPos++];

        if (c < 0x80) {
            int count = (int)c + 1;
            int i;
            if (inPos + count > srcLen || outPos + count > dstCap) {
                return -1;
            }
            for (i = 0; i < count; i++) {
                dst[outPos++] = src[inPos++];
            }
        } else {
            int count = (int)(c - 0x80) + SAVERLE_MIN_RUN;
            unsigned char value;
            int i;
            if (inPos + 1 > srcLen || outPos + count > dstCap) {
                return -1;
            }
            value = src[inPos++];
            for (i = 0; i < count; i++) {
                dst[outPos++] = value;
            }
        }
    }
    return outPos;
}
```

- [ ] **Step 5: Run the tests and make sure they pass**

Run: `cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src -o run_tests_saverle test_saverle.c ../src/saverle.c && ./run_tests_saverle`
Expected: `test_saverle: all passed`

- [ ] **Step 6: Append the suite to the harness**

In `saturn/tests/run_tests.sh`, after the `run_tests_bootmenu` block, add:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_saverle test_saverle.c ../src/saverle.c
./run_tests_saverle
```

- [ ] **Step 7: Run the whole harness**

Run: `sh saturn/tests/run_tests.sh`
Expected: nine suites, all passing.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/saverle.h saturn/src/saverle.c saturn/tests/test_saverle.c saturn/tests/run_tests.sh
git commit -m "Add a run-length codec for save payloads that declines rather than growing, so an incompressible save is stored raw instead of larger."
```

---

### Task 2: The buffer cursor

**Files:**
- Create: `saturn/src/savebuf.h`, `saturn/src/savebuf.c`
- Test: `saturn/tests/test_savebuf.c`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct savebuf` with fields `data`, `cap`, `len`, `pos`, `writing`, `err`; `savebuf_open_write(savebuf *, unsigned char *, int)`, `savebuf_open_read(savebuf *, const unsigned char *, int)`, `savebuf_putc(savebuf *, int)`, `savebuf_getc(savebuf *)`, `savebuf_len(const savebuf *)`, `savebuf_error(const savebuf *)`.

This is deliberately not named `fopen`/`fputc`. The host tests link against real libc, so a module using those names could not be tested. Task 6 maps the stdio names onto this in a Saturn-only file.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_savebuf.c`:

```c
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
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src -o run_tests_savebuf test_savebuf.c ../src/savebuf.c`
Expected: FAIL — `savebuf.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/savebuf.h`:

```c
/*----------------------
 | savebuf.h
 | Description: A sequential cursor over a byte buffer, with the one-byte-at-
 |   a-time shape quicksave() and quickload() already use through fputc and
 |   fgetc. Deliberately not named after stdio: the host tests link real libc,
 |   so a module claiming fopen could not be built by run_tests.sh.
 |   saturn_filestub.c maps the stdio names onto this on Saturn.
 |
 |   Pure C with no engine, SRL or SGL dependency.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef SAVEBUF_H
#define SAVEBUF_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | savebuf
 | Description: One open buffer. A buffer is either writing or reading, never
 |   both, and err latches on the first refusal so a caller may check once at
 |   the end rather than after every byte.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char *data;
    int cap;
    int len;
    int pos;
    int writing;
    int err;
} savebuf;

/*----------------------
 | savebuf_open_write
 | Description: Opens a buffer for writing from position zero.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer to initialise; data -- storage; cap -- its capacity
 | Returns: N/A
 ----------------------*/
void savebuf_open_write(savebuf *b, unsigned char *data, int cap);

/*----------------------
 | savebuf_open_read
 | Description: Opens a buffer for reading from position zero.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer to initialise; data -- storage; len -- how many bytes
 |         are readable
 | Returns: N/A
 ----------------------*/
void savebuf_open_read(savebuf *b, const unsigned char *data, int len);

/*----------------------
 | savebuf_putc
 | Description: Appends one byte, masked to eight bits.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer; c -- value
 | Returns: the byte written, or -1 if the buffer is full or is a read buffer,
 |          in which case err is set and nothing is written
 ----------------------*/
int savebuf_putc(savebuf *b, int c);

/*----------------------
 | savebuf_getc
 | Description: Reads the next byte.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer
 | Returns: the byte, or -1 at end of data or on a write buffer, in which case
 |          err is set
 ----------------------*/
int savebuf_getc(savebuf *b);

/*----------------------
 | savebuf_len
 | Description: How many bytes have been written, or are readable.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer
 | Returns: the length
 ----------------------*/
int savebuf_len(const savebuf *b);

/*----------------------
 | savebuf_error
 | Description: Whether any operation on this buffer has been refused.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer
 | Returns: non-zero once a refusal has happened
 ----------------------*/
int savebuf_error(const savebuf *b);

#ifdef __cplusplus
}
#endif

#endif /* SAVEBUF_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/savebuf.c`:

```c
/*----------------------
 | savebuf.c
 | Description: The cursor described by savebuf.h.
 | Author: suinevere
 | Dependencies: savebuf.h
 ----------------------*/
#include "savebuf.h"

void savebuf_open_write(savebuf *b, unsigned char *data, int cap)
{
    b->data = data;
    b->cap = cap;
    b->len = 0;
    b->pos = 0;
    b->writing = 1;
    b->err = 0;
}

void savebuf_open_read(savebuf *b, const unsigned char *data, int len)
{
    b->data = (unsigned char *)data;
    b->cap = len;
    b->len = len;
    b->pos = 0;
    b->writing = 0;
    b->err = 0;
}

int savebuf_putc(savebuf *b, int c)
{
    if (!b->writing || b->pos >= b->cap) {
        b->err = 1;
        return -1;
    }
    b->data[b->pos++] = (unsigned char)(c & 0xFF);
    b->len = b->pos;
    return c & 0xFF;
}

int savebuf_getc(savebuf *b)
{
    if (b->writing || b->pos >= b->len) {
        b->err = 1;
        return -1;
    }
    return (int)b->data[b->pos++];
}

int savebuf_len(const savebuf *b)
{
    return b->len;
}

int savebuf_error(const savebuf *b)
{
    return b->err;
}
```

- [ ] **Step 5: Run the tests and make sure they pass**

Run: `cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src -o run_tests_savebuf test_savebuf.c ../src/savebuf.c && ./run_tests_savebuf`
Expected: `test_savebuf: all passed`

- [ ] **Step 6: Append the suite to run_tests.sh**

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_savebuf test_savebuf.c ../src/savebuf.c
./run_tests_savebuf
```

- [ ] **Step 7: Commit**

```bash
git add saturn/src/savebuf.h saturn/src/savebuf.c saturn/tests/test_savebuf.c saturn/tests/run_tests.sh
git commit -m "Add a sequential byte-buffer cursor with the one-byte shape quicksave already writes through, kept off the stdio names so the host tests can build it."
```

---

### Task 3: Slot metadata

**Files:**
- Create: `saturn/src/system/saturn_backup.h`, `saturn/src/savedata.h`, `saturn/src/savedata.c`, `saturn/tests/stub_saturn_backup.c`
- Test: `saturn/tests/test_savedata.c`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: from `saturn_backup.h` — `SAT_BUP_INTERNAL` (1), `SAT_BUP_CART` (2), `SAT_BUP_OK` (0), `SAT_BUP_ERR_NONE` (1), `SAT_BUP_ERR_UNFORMAT` (2), `SAT_BUP_ERR_PROTECTED` (3), `SAT_BUP_ERR_NO_SPACE` (4), `SAT_BUP_ERR_NOT_FOUND` (5), `SAT_BUP_ERR_BROKEN` (6), `SAT_BUP_ERR_EXISTS` (7), `SatBupDev {int present; int formatted; int writeProtected; unsigned long freeBytes;}`, `SatBupEntry {int exists; unsigned long size; unsigned long date;}`, and the six `sat_bup_*` prototypes. From `savedata.h` — `SAVE_NUM_SLOTS` (3), `SAVE_HEADER_SIZE` (48), `SAVE_UPSTREAM_BYTES` (6150), `SAVE_TRAILER_BYTES` (4), `SAVE_PAYLOAD_MAX`, `SAVE_MAX_BYTES`, `SAVE_FORMAT_VERSION` (1), `SlotState`, `SlotInfo`, `savedata_slot_name`, `savedata_write_header`, `savedata_read_header`, `savedata_probe`, `savedata_pick_default_device`, `savedata_date_split`.

Three deliberate departures from the reference repo, each named in the spec:

1. `sat_bup_date_split` becomes `savedata_date_split` in pure C, so the tests exercise the shipping copy instead of a duplicate living in the stub.
2. `savedata_probe` takes a caller-supplied scratch buffer instead of owning a `SAVE_MAX_BYTES` static, which would be 6 KB of HWRAM.
3. `SAVE_MAX_BYTES` is defined once, here.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_savedata.c`:

```c
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

    if (g_fail != 0) {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_savedata: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src -I../src/system -o run_tests_savedata test_savedata.c stub_saturn_backup.c ../src/savedata.c`
Expected: FAIL — `savedata.h: No such file or directory`.

- [ ] **Step 3: Write the backup interface header**

Create `saturn/src/system/saturn_backup.h`. This is a straight adaptation of `..\Another-Saturn\saturn\src\system\saturn_backup.h`, with `sat_bup_date_split` removed — it now lives in `savedata.c` — and `stdint.h` replaced by plain C types so the header stays includable from every translation unit in this build:

```c
/*----------------------
 | saturn_backup.h
 | Description: A small C interface for Saturn backup RAM, backed by SGL's BUP
 |   vector table. It exists so savedata.c and savegame.c can read and write
 |   saves without pulling <srl.hpp> into an engine translation unit -- the
 |   engine's headers wrap SGL's C headers in extern "C" and mixing the two
 |   include orders is fragile. Same seam saturn_bootart.h draws for artwork.
 |
 |   Every entry point takes the device explicitly. There is deliberately no
 |   implicit "current device" here: the menu owns that choice.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef SATURN_BACKUP_H
#define SATURN_BACKUP_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAT_BUP_INTERNAL / SAT_BUP_CART
 | Description: Device ids, matching SGL's BUP_MAIN_UNIT and BUP_CURTRIDGE.
 | Author: suinevere
 ----------------------*/
#define SAT_BUP_INTERNAL 1
#define SAT_BUP_CART     2

/*----------------------
 | SAT_BUP_*
 | Description: Return codes, distinct from SGL's so callers need not include
 |   sega_bup.h.
 | Author: suinevere
 ----------------------*/
#define SAT_BUP_OK              0
#define SAT_BUP_ERR_NONE        1
#define SAT_BUP_ERR_UNFORMAT    2
#define SAT_BUP_ERR_PROTECTED   3
#define SAT_BUP_ERR_NO_SPACE    4
#define SAT_BUP_ERR_NOT_FOUND   5
#define SAT_BUP_ERR_BROKEN      6
#define SAT_BUP_ERR_EXISTS      7

/*----------------------
 | SatBupDev
 | Description: What sat_bup_probe found on one device.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int present;
    int formatted;
    int writeProtected;
    unsigned long freeBytes;
} SatBupDev;

/*----------------------
 | SatBupEntry
 | Description: What sat_bup_dir found for one filename.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int exists;
    unsigned long size;
    unsigned long date;
} SatBupEntry;

/*----------------------
 | sat_bup_init
 | Description: Brings up the BIOS backup library. Call once, after
 |   platform_init and before any other sat_bup_* call.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_bup_init(void);

/*----------------------
 | sat_bup_probe
 | Description: Reports whether a device is present, formatted, writable, and
 |   how much room it has left.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; out -- filled in
 | Returns: SAT_BUP_OK, or an error code with out zeroed
 ----------------------*/
int sat_bup_probe(unsigned long device, SatBupDev *out);

/*----------------------
 | sat_bup_dir
 | Description: Looks a save up by name without reading its contents.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- device id; name -- BUP filename; out -- filled in
 | Returns: SAT_BUP_OK whether or not the file exists; check out->exists. An
 |          error code means the lookup itself failed.
 ----------------------*/
int sat_bup_dir(unsigned long device, const char *name, SatBupEntry *out);

/*----------------------
 | sat_bup_read
 | Description: Reads a whole save into dst, refusing before the read if the
 |   stored file is larger than dst.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- device id; name -- BUP filename; dst -- destination;
 |         size -- capacity of dst
 | Returns: SAT_BUP_OK, SAT_BUP_ERR_NOT_FOUND, or SAT_BUP_ERR_BROKEN
 ----------------------*/
int sat_bup_read(unsigned long device, const char *name, void *dst, long size);

/*----------------------
 | sat_bup_write
 | Description: Writes a save, stamping it with the current RTC time.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- device id; name -- BUP filename; comment -- up to 10
 |         characters shown by the Saturn's Backup Manager; src -- the bytes;
 |         size -- how many; overwrite -- non-zero to replace
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_EXISTS / _NO_SPACE / _PROTECTED /
 |          _UNFORMAT
 ----------------------*/
int sat_bup_write(unsigned long device, const char *name, const char *comment,
                  const void *src, long size, int overwrite);

/*----------------------
 | sat_bup_delete
 | Description: Removes a save.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- device id; name -- BUP filename
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_NOT_FOUND
 ----------------------*/
int sat_bup_delete(unsigned long device, const char *name);

/*----------------------
 | sat_bup_date_now
 | Description: The current RTC time as a BUP date word.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: the packed word
 ----------------------*/
unsigned long sat_bup_date_now(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_BACKUP_H */
```

- [ ] **Step 4: Write the savedata header**

Create `saturn/src/savedata.h`:

```c
/*----------------------
 | savedata.h
 | Description: Save slot metadata between the engine and saturn_backup.h's
 |   raw BUP wrapper: slot naming, header packing, slot probing, device
 |   defaulting and BUP date arithmetic. Must not include srl.hpp or
 |   sega_bup.h, so it stays safe to include from any translation unit and
 |   buildable by run_tests.sh.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#ifndef SAVEDATA_H
#define SAVEDATA_H

#include "saturn_backup.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAVE_NUM_SLOTS / SAVE_HEADER_SIZE / SAVE_FORMAT_VERSION
 | Description: How many slots a device holds, the packed header size, and the
 |   format version a save must carry to be loadable.
 | Author: suinevere
 ----------------------*/
#define SAVE_NUM_SLOTS      3
#define SAVE_HEADER_SIZE    48
#define SAVE_FORMAT_VERSION 1

/*----------------------
 | SAVE_UPSTREAM_BYTES
 | Description: What quicksave() writes, and therefore what quickload() must
 |   be given back: 3 bytes of room, backdrop and palette, 512 of main
 |   variables, 4096 of aux task banks, 512 of task program counters and
 |   enables, and 1027 of sprite records. Fixed by the upstream format, not by
 |   this port -- if it ever disagrees with main.c, quickload will read
 |   garbage rather than fail, so test_savegame asserts it.
 | Author: suinevere
 ----------------------*/
#define SAVE_UPSTREAM_BYTES 6150

/*----------------------
 | SAVE_TRAILER_BYTES
 | Description: The Saturn-only tail: CD-DA track index as a big-endian
 |   signed 16-bit value with -1 for none, the loop flag, and one reserved
 |   byte.
 | Author: suinevere
 ----------------------*/
#define SAVE_TRAILER_BYTES 4

/*----------------------
 | SAVE_PAYLOAD_MAX / SAVE_MAX_BYTES
 | Description: The uncompressed payload, and a whole save at its worst case.
 |   SAVE_MAX_BYTES is the size of both staging buffers, the capacity checked
 |   before a read, and the BUP_Stat datasize -- defined here once and nowhere
 |   else.
 | Author: suinevere
 ----------------------*/
#define SAVE_PAYLOAD_MAX (SAVE_UPSTREAM_BYTES + SAVE_TRAILER_BYTES)
#define SAVE_MAX_BYTES   (SAVE_HEADER_SIZE + SAVE_PAYLOAD_MAX)

/*----------------------
 | SAVE_FLAG_RLE
 | Description: Set in the header when the payload is run-length encoded.
 | Author: suinevere
 ----------------------*/
#define SAVE_FLAG_RLE 0x01

/*----------------------
 | SlotState
 | Description: What savedata_probe found in one backup RAM slot.
 | Author: suinevere
 ----------------------*/
typedef enum {
    SLOT_EMPTY,
    SLOT_OK,
    SLOT_DAMAGED,
    SLOT_OLD_VERSION
} SlotState;

/*----------------------
 | SlotInfo
 | Description: What a slot list row needs to show for one slot.
 | Author: suinevere
 ----------------------*/
typedef struct {
    SlotState state;
    unsigned short roomId;
    unsigned long date;
} SlotInfo;

/*----------------------
 | savedata_slot_name
 | Description: Builds the BUP filename for a slot index.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- 0-based slot index; out -- destination, must hold 12 bytes
 | Returns: N/A
 ----------------------*/
void savedata_slot_name(int slot, char *out);

/*----------------------
 | savedata_write_header
 | Description: Packs a SAVE_HEADER_SIZE-byte header in place, big-endian.
 |   Layout: 0 magic 'HOTA', 4 version, 6 flags, 7 reserved, 8 payload length,
 |   10 room id, 12 date, 16 description[32].
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- destination, must hold SAVE_HEADER_SIZE bytes; roomId -- the
 |         room the save was made in; date -- packed BUP date; flags --
 |         SAVE_FLAG_* bits; payloadLen -- stored payload length in bytes
 | Returns: N/A
 ----------------------*/
void savedata_write_header(unsigned char *buf, unsigned short roomId,
                           unsigned long date, unsigned char flags,
                           unsigned short payloadLen);

/*----------------------
 | savedata_read_header
 | Description: Unpacks a header, checking the magic before writing any
 |   output.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- source, at least SAVE_HEADER_SIZE bytes; ver, roomId, date,
 |         flags, payloadLen -- outputs, all left untouched on failure
 | Returns: 0 on magic mismatch, 1 otherwise
 ----------------------*/
int savedata_read_header(const unsigned char *buf, unsigned short *ver,
                         unsigned short *roomId, unsigned long *date,
                         unsigned char *flags, unsigned short *payloadLen);

/*----------------------
 | savedata_probe
 | Description: Reads a whole slot into the caller's scratch buffer and
 |   classifies it. The buffer is the caller's because the only one this port
 |   can afford lives in LWRAM; owning a SAVE_MAX_BYTES static here would put
 |   6 KB in HWRAM BSS.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0-based index;
 |         out -- filled in; scratch -- working buffer; scratchCap -- its
 |         capacity, which must be at least SAVE_MAX_BYTES
 | Returns: the same state written to out->state
 ----------------------*/
SlotState savedata_probe(unsigned long device, int slot, SlotInfo *out,
                         unsigned char *scratch, int scratchCap);

/*----------------------
 | savedata_pick_default_device
 | Description: Chooses which backup device the save menus open on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: internal, cart -- probe results; internalHasSaves, cartHasSaves --
 |         non-zero if that device already holds a HOTASAVE* file
 | Returns: SAT_BUP_CART only when the cart is present, formatted and holds
 |          saves while internal does not; SAT_BUP_INTERNAL otherwise
 ----------------------*/
unsigned long savedata_pick_default_device(const SatBupDev *internal,
                                           const SatBupDev *cart,
                                           int internalHasSaves,
                                           int cartHasSaves);

/*----------------------
 | savedata_date_split
 | Description: Unpacks a BUP date word, which counts minutes from 1 January
 |   1980, into the fields a slot row shows. Pure arithmetic, which is why it
 |   lives here rather than in saturn_backup.cxx -- here the host tests reach
 |   the shipping copy instead of a duplicate.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: date -- packed word; month, day, hour, min -- outputs, any may be
 |         NULL
 | Returns: N/A
 ----------------------*/
void savedata_date_split(unsigned long date, int *month, int *day, int *hour,
                         int *min);

#ifdef __cplusplus
}
#endif

#endif /* SAVEDATA_H */
```

- [ ] **Step 5: Write the savedata implementation**

Create `saturn/src/savedata.c`:

```c
/*----------------------
 | savedata.c
 | Description: The slot metadata described by savedata.h.
 | Author: suinevere
 | Dependencies: savedata.h, string.h
 ----------------------*/
#include <string.h>
#include "savedata.h"

void savedata_slot_name(int slot, char *out)
{
    strcpy(out, "HOTASAVE");
    out[8] = (char)('1' + slot);
    out[9] = 0;
}

void savedata_write_header(unsigned char *buf, unsigned short roomId,
                           unsigned long date, unsigned char flags,
                           unsigned short payloadLen)
{
    buf[0] = 'H';
    buf[1] = 'O';
    buf[2] = 'T';
    buf[3] = 'A';
    buf[4] = (unsigned char)((SAVE_FORMAT_VERSION >> 8) & 0xFF);
    buf[5] = (unsigned char)(SAVE_FORMAT_VERSION & 0xFF);
    buf[6] = flags;
    buf[7] = 0;
    buf[8] = (unsigned char)((payloadLen >> 8) & 0xFF);
    buf[9] = (unsigned char)(payloadLen & 0xFF);
    buf[10] = (unsigned char)((roomId >> 8) & 0xFF);
    buf[11] = (unsigned char)(roomId & 0xFF);
    buf[12] = (unsigned char)((date >> 24) & 0xFF);
    buf[13] = (unsigned char)((date >> 16) & 0xFF);
    buf[14] = (unsigned char)((date >> 8) & 0xFF);
    buf[15] = (unsigned char)(date & 0xFF);
    memset(buf + 16, 0, SAVE_HEADER_SIZE - 16);
}

int savedata_read_header(const unsigned char *buf, unsigned short *ver,
                         unsigned short *roomId, unsigned long *date,
                         unsigned char *flags, unsigned short *payloadLen)
{
    if (buf[0] != 'H' || buf[1] != 'O' || buf[2] != 'T' || buf[3] != 'A') {
        return 0;
    }
    *ver = (unsigned short)((buf[4] << 8) | buf[5]);
    *flags = buf[6];
    *payloadLen = (unsigned short)((buf[8] << 8) | buf[9]);
    *roomId = (unsigned short)((buf[10] << 8) | buf[11]);
    *date = ((unsigned long)buf[12] << 24) | ((unsigned long)buf[13] << 16) |
            ((unsigned long)buf[14] << 8) | (unsigned long)buf[15];
    return 1;
}

SlotState savedata_probe(unsigned long device, int slot, SlotInfo *out,
                         unsigned char *scratch, int scratchCap)
{
    char name[12];
    SatBupEntry entry;
    unsigned short ver = 0;
    unsigned short roomId = 0;
    unsigned short payloadLen = 0;
    unsigned char flags = 0;
    unsigned long date = 0;

    savedata_slot_name(slot, name);
    out->state = SLOT_EMPTY;
    out->roomId = 0;
    out->date = 0;

    if (scratchCap < SAVE_MAX_BYTES) {
        out->state = SLOT_DAMAGED;
        return SLOT_DAMAGED;
    }
    if (sat_bup_dir(device, name, &entry) != SAT_BUP_OK || !entry.exists) {
        return SLOT_EMPTY;
    }
    if (sat_bup_read(device, name, scratch, (long)scratchCap) != SAT_BUP_OK) {
        out->state = SLOT_DAMAGED;
        return SLOT_DAMAGED;
    }
    if (!savedata_read_header(scratch, &ver, &roomId, &date, &flags,
                              &payloadLen)) {
        out->state = SLOT_DAMAGED;
        return SLOT_DAMAGED;
    }

    out->roomId = roomId;
    out->date = date;
    out->state = (ver < SAVE_FORMAT_VERSION) ? SLOT_OLD_VERSION : SLOT_OK;
    return out->state;
}

unsigned long savedata_pick_default_device(const SatBupDev *internal,
                                           const SatBupDev *cart,
                                           int internalHasSaves,
                                           int cartHasSaves)
{
    (void)internal;
    if (cart->present && cart->formatted && cartHasSaves && !internalHasSaves) {
        return SAT_BUP_CART;
    }
    return SAT_BUP_INTERNAL;
}

void savedata_date_split(unsigned long date, int *month, int *day, int *hour,
                         int *min)
{
    static const int len[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned long days = date / 1440UL;
    unsigned long rem = date % 1440UL;
    int year = 1980;
    int mo = 0;

    for (;;) {
        int inYear = ((year % 4) == 0) ? 366 : 365;
        if (days < (unsigned long)inYear) {
            break;
        }
        days -= (unsigned long)inYear;
        year++;
    }
    for (;;) {
        int inMonth = len[mo];
        if (mo == 1 && (year % 4) == 0) {
            inMonth = 29;
        }
        if (days < (unsigned long)inMonth) {
            break;
        }
        days -= (unsigned long)inMonth;
        mo++;
    }

    if (month) *month = mo + 1;
    if (day)   *day = (int)days + 1;
    if (hour)  *hour = (int)(rem / 60UL);
    if (min)   *min = (int)(rem % 60UL);
}
```

- [ ] **Step 6: Write the backup stub for tests**

Create `saturn/tests/stub_saturn_backup.h`:

```c
/*----------------------
 | stub_saturn_backup.h
 | Description: Test-only controls for the host stand-in of the backup RAM
 |   layer. Not built into the Saturn binary.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#ifndef STUB_SATURN_BACKUP_H
#define STUB_SATURN_BACKUP_H

#include "saturn_backup.h"

void stub_bup_reset(void);
void stub_bup_place(unsigned long device, const char *name,
                    const unsigned char *data, int len);
int  stub_bup_fetch(unsigned long device, const char *name,
                    unsigned char *out, int cap);
void stub_bup_fail_read(int code);
void stub_bup_fail_write(int code);
void stub_bup_set_device(unsigned long device, int present, int formatted,
                         int writeProtected, unsigned long freeBytes);

#endif /* STUB_SATURN_BACKUP_H */
```

Create `saturn/tests/stub_saturn_backup.c`:

```c
/*----------------------
 | stub_saturn_backup.c
 | Description: A host stand-in for saturn_backup.cxx, holding up to four
 |   files in memory and able to fail any call on demand, so the failure rows
 |   in the save design's error table are reachable from run_tests.sh.
 | Author: suinevere
 | Dependencies: stub_saturn_backup.h, savedata.h, string.h
 ----------------------*/
#include <string.h>
#include "stub_saturn_backup.h"
#include "savedata.h"

#define STUB_FILES 8

static struct {
    int used;
    unsigned long device;
    char name[12];
    unsigned char data[SAVE_MAX_BYTES];
    int len;
} s_files[STUB_FILES];

static int s_readFail;
static int s_writeFail;
static SatBupDev s_dev[3];
static unsigned long s_now;

void stub_bup_reset(void)
{
    memset(s_files, 0, sizeof(s_files));
    s_readFail = SAT_BUP_OK;
    s_writeFail = SAT_BUP_OK;
    memset(s_dev, 0, sizeof(s_dev));
    s_dev[SAT_BUP_INTERNAL].present = 1;
    s_dev[SAT_BUP_INTERNAL].formatted = 1;
    s_dev[SAT_BUP_INTERNAL].freeBytes = 32768;
    s_now = 0;
}

/*----------------------
 | find
 | Description: Locates a placed file by device and name.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: s_files
 | Params: device -- device id; name -- BUP filename
 | Returns: the index into s_files, or -1 when no such file is placed
 ----------------------*/
static int find(unsigned long device, const char *name)
{
    int i;
    for (i = 0; i < STUB_FILES; i++) {
        if (s_files[i].used && s_files[i].device == device &&
            strcmp(s_files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void stub_bup_place(unsigned long device, const char *name,
                    const unsigned char *data, int len)
{
    int i = find(device, name);
    if (i < 0) {
        for (i = 0; i < STUB_FILES; i++) {
            if (!s_files[i].used) {
                break;
            }
        }
        if (i == STUB_FILES) {
            return;
        }
    }
    s_files[i].used = 1;
    s_files[i].device = device;
    strcpy(s_files[i].name, name);
    memcpy(s_files[i].data, data, (size_t)len);
    s_files[i].len = len;
}

int stub_bup_fetch(unsigned long device, const char *name,
                   unsigned char *out, int cap)
{
    int i = find(device, name);
    if (i < 0 || s_files[i].len > cap) {
        return -1;
    }
    memcpy(out, s_files[i].data, (size_t)s_files[i].len);
    return s_files[i].len;
}

void stub_bup_fail_read(int code)  { s_readFail = code; }
void stub_bup_fail_write(int code) { s_writeFail = code; }

void stub_bup_set_device(unsigned long device, int present, int formatted,
                         int writeProtected, unsigned long freeBytes)
{
    s_dev[device].present = present;
    s_dev[device].formatted = formatted;
    s_dev[device].writeProtected = writeProtected;
    s_dev[device].freeBytes = freeBytes;
}

void sat_bup_init(void) { stub_bup_reset(); }

int sat_bup_probe(unsigned long device, SatBupDev *out)
{
    *out = s_dev[device];
    if (!out->present)        return SAT_BUP_ERR_NONE;
    if (!out->formatted)      return SAT_BUP_ERR_UNFORMAT;
    if (out->writeProtected)  return SAT_BUP_ERR_PROTECTED;
    return SAT_BUP_OK;
}

int sat_bup_dir(unsigned long device, const char *name, SatBupEntry *out)
{
    int i = find(device, name);
    memset(out, 0, sizeof(*out));
    if (i >= 0) {
        out->exists = 1;
        out->size = (unsigned long)s_files[i].len;
        out->date = 0;
    }
    return SAT_BUP_OK;
}

int sat_bup_read(unsigned long device, const char *name, void *dst, long size)
{
    int i;
    if (s_readFail != SAT_BUP_OK) {
        return s_readFail;
    }
    i = find(device, name);
    if (i < 0) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    if ((long)s_files[i].len > size) {
        return SAT_BUP_ERR_BROKEN;
    }
    memcpy(dst, s_files[i].data, (size_t)s_files[i].len);
    return SAT_BUP_OK;
}

int sat_bup_write(unsigned long device, const char *name, const char *comment,
                  const void *src, long size, int overwrite)
{
    (void)comment;
    (void)overwrite;
    if (s_writeFail != SAT_BUP_OK) {
        return s_writeFail;
    }
    if ((unsigned long)size > s_dev[device].freeBytes) {
        return SAT_BUP_ERR_NO_SPACE;
    }
    stub_bup_place(device, name, (const unsigned char *)src, (int)size);
    return SAT_BUP_OK;
}

int sat_bup_delete(unsigned long device, const char *name)
{
    int i = find(device, name);
    if (i < 0) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    s_files[i].used = 0;
    return SAT_BUP_OK;
}

unsigned long sat_bup_date_now(void) { return s_now; }
```

- [ ] **Step 7: Run the tests and make sure they pass**

Run: `cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src -I../src/system -o run_tests_savedata test_savedata.c stub_saturn_backup.c ../src/savedata.c && ./run_tests_savedata`
Expected: `test_savedata: all passed`

- [ ] **Step 8: Append the suite to run_tests.sh**

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system \
    -o run_tests_savedata test_savedata.c stub_saturn_backup.c ../src/savedata.c
./run_tests_savedata
```

- [ ] **Step 9: Commit**

```bash
git add saturn/src/system/saturn_backup.h saturn/src/savedata.h saturn/src/savedata.c saturn/tests/stub_saturn_backup.h saturn/tests/stub_saturn_backup.c saturn/tests/test_savedata.c saturn/tests/run_tests.sh
git commit -m "Add save slot metadata over a backup RAM interface, with the BUP date arithmetic in pure C so the tests exercise the shipping copy and probing on a caller-supplied buffer so no six-kilobyte static lands in HWRAM."
```

---

### Task 4: The CD-DA track getter

**Files:**
- Modify: `saturn/src/disc.h`, `saturn/src/system/disc_srl.cxx`, `saturn/host/disc_cue.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `int disc_current_track(int *loop)` — returns the engine music index currently playing, or `-1` if none, and writes the loop flag through `loop` when it is not NULL.

`disc_srl.cxx:111-112` already holds `g_musicTrack` and `g_musicLoop`, set in `disc_play_track` and cleared in `disc_stop_track`. This adds a reader, nothing else. The host backend gets the same function so `disc.h` stays one interface with two implementations.

- [ ] **Step 1: Declare it**

In `saturn/src/disc.h`, after the `disc_play_track` / `disc_stop_track` declarations, add:

```c
/*----------------------
 | disc_current_track
 | Description: Which music track is playing, for a save's Saturn trailer. Not
 |   a playback query: it reports what disc_play_track was last asked for and
 |   disc_stop_track last cleared, which is the state a load must restore, not
 |   whatever the drive is doing this instant.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: loop -- receives the loop flag, or NULL if the caller does not want it
 | Returns: the engine music index, or -1 when nothing is playing
 ----------------------*/
int disc_current_track(int *loop);
```

- [ ] **Step 2: Implement it on Saturn**

In `saturn/src/system/disc_srl.cxx`, immediately after `disc_stop_track`, add:

```c
/*----------------------
 | disc_current_track
 | Description: Reads back the track disc_play_track last accepted.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_musicTrack, g_musicLoop
 | Params: loop -- receives the loop flag, or NULL
 | Returns: the engine music index, or -1 when nothing is playing
 ----------------------*/
int disc_current_track(int *loop)
{
	if (loop != NULL)
	{
		*loop = g_musicLoop;
	}
	return g_musicTrack;
}
```

- [ ] **Step 3: Implement it on the host**

`saturn/host/disc_cue.c` already keeps `disc_music_loop` (line 88) but no engine index, so add one beside it:

```c
/*----------------------
 | disc_music_track
 | Description: The engine music index behind the currently hooked track, so
 |   disc_current_track can report what a save must restart. -1 whenever
 |   nothing is hooked, matching disc_music_fp being NULL.
 | Author: suinevere
 ----------------------*/
static int disc_music_track = -1;
```

Set `disc_music_track = engine_index;` in `disc_play_track` at the same point it writes `disc_music_loop`, and set it back to `-1` in `disc_stop_track` where that closes `disc_music_fp`. Then add:

```c
/*----------------------
 | disc_current_track
 | Description: Reads back the track disc_play_track last hooked.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: disc_music_track, disc_music_loop
 | Params: loop -- receives the loop flag, or NULL
 | Returns: the engine music index, or -1 when nothing is playing
 ----------------------*/
int disc_current_track(int *loop)
{
    if (loop != NULL)
    {
        *loop = disc_music_loop;
    }
    return disc_music_track;
}
```

- [ ] **Step 4: Verify both builds still link**

Run: `sh saturn/tests/run_tests.sh`
Expected: ten suites pass, unchanged by this task.

Ask the user to run `cmd.exe /c ".\compile.bat debug"` from `saturn/`.
Expected: builds clean, `BuildDrop/` non-empty.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/disc.h saturn/src/system/disc_srl.cxx saturn/host/disc_cue.c
git commit -m "Expose which music track is playing, so a save can record what to restart rather than leaving a loaded game silent or playing the wrong chapter's music."
```

---

### Task 5: Header, trailer and slot I/O

**Files:**
- Create: `saturn/src/savegame.h`, `saturn/src/savegame.c`
- Test: `saturn/tests/test_savegame.c`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: `saverle_encode`/`saverle_decode` (Task 1); every `SAVE_*` constant, `savedata_write_header`, `savedata_read_header`, `savedata_slot_name` (Task 3); `sat_bup_read`, `sat_bup_write`, `sat_bup_date_now` (Task 3's header).
- Produces: `SAVE_ERR_BAD_MAGIC` (32), `SAVE_ERR_BAD_VERSION` (33), `SAVE_ERR_BAD_PAYLOAD` (34), `SAVE_ERR_TOO_LARGE` (35); `int savegame_write(unsigned long device, int slot, const unsigned char *payload, int payloadLen, unsigned short roomId, unsigned char *work, int workCap)`; `int savegame_read(unsigned long device, int slot, unsigned char *payload, int payloadCap, int *payloadLen, unsigned short *roomId, unsigned char *work, int workCap)`; `void savegame_pack_trailer(unsigned char *out, int track, int loop)`; `void savegame_unpack_trailer(const unsigned char *in, int *track, int *loop)`.

This module does not call `quicksave()`. It takes a payload and gives one back, so `run_tests.sh` can link it without `main.c`. Task 7 supplies the payload on Saturn.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_savegame.c`:

```c
/*----------------------
 | test_savegame.c
 | Description: Host unit tests for savegame.c, linked against
 |   stub_saturn_backup.c. Covers the round trip and every failure row in the
 |   save design's error table, each asserting the caller's payload buffer is
 |   untouched when a load is refused.
 | Author: suinevere
 | Dependencies: savegame.h, savedata.h, stub_saturn_backup.h, stdio.h, string.h
 ----------------------*/
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
               savegame_read(SAT_BUP_INTERNAL, 1, back, (int)sizeof(back), &len,
                             &room, work, (int)sizeof(work)),
               SAT_BUP_ERR_NOT_FOUND);
    expect_int("payload untouched", (int)back[0], 0xEE);
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

static void test_corrupt_rle_leaves_payload_untouched(void)
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
    stub_bup_place(SAT_BUP_INTERNAL, "HOTASAVE1", stored,
                   SAVE_HEADER_SIZE + 1);
    (void)storedLen;

    memset(back, 0xEE, sizeof(back));
    expect_int("truncated payload refused",
               savegame_read(SAT_BUP_INTERNAL, 0, back, (int)sizeof(back), &len,
                             &room, work, (int)sizeof(work)),
               SAVE_ERR_BAD_PAYLOAD);
    expect_int("payload untouched", (int)back[0], 0xEE);
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
               savegame_write(SAT_BUP_INTERNAL, 1, src, (int)sizeof(src), 9,
                              work, (int)sizeof(work)),
               SAT_BUP_ERR_NO_SPACE);
    expect_int("existing slot survives",
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

int main(void)
{
    test_upstream_size_matches_the_engine();
    test_trailer_roundtrip();
    test_compressible_roundtrip();
    test_incompressible_is_stored_raw();
    test_missing_slot();
    test_bad_magic_leaves_payload_untouched();
    test_bad_version_leaves_payload_untouched();
    test_corrupt_rle_leaves_payload_untouched();
    test_device_full();
    test_write_rejects_oversized_payload();
    test_write_rejects_small_work_buffer();

    if (g_fail != 0) {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_savegame: all passed\n");
    return 0;
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src -I../src/system -o run_tests_savegame test_savegame.c stub_saturn_backup.c ../src/savegame.c ../src/savedata.c ../src/saverle.c`
Expected: FAIL — `savegame.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/savegame.h`:

```c
/*----------------------
 | savegame.h
 | Description: Assembles a save from a header, a payload and a Saturn
 |   trailer, chooses whether to compress it, and moves it to and from a
 |   backup RAM slot.
 |
 |   Deliberately does not call quicksave(): it is handed a payload and gives
 |   one back, so run_tests.sh can link it without main.c. saturn_saveslot.cxx
 |   supplies the payload on Saturn.
 | Author: suinevere
 | Dependencies: savedata.h
 ----------------------*/
#ifndef SAVEGAME_H
#define SAVEGAME_H

#include "savedata.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAVE_ERR_*
 | Description: Failures this layer detects itself, numbered clear of the
 |   SAT_BUP_* codes so one return value can carry either.
 | Author: suinevere
 ----------------------*/
#define SAVE_ERR_BAD_MAGIC   32
#define SAVE_ERR_BAD_VERSION 33
#define SAVE_ERR_BAD_PAYLOAD 34
#define SAVE_ERR_TOO_LARGE   35

/*----------------------
 | savegame_pack_trailer
 | Description: Writes the Saturn trailer: track index big-endian with -1 for
 |   none, loop flag, one reserved byte.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- destination, must hold SAVE_TRAILER_BYTES; track -- engine
 |         music index or -1; loop -- non-zero if looping
 | Returns: N/A
 ----------------------*/
void savegame_pack_trailer(unsigned char *out, int track, int loop);

/*----------------------
 | savegame_unpack_trailer
 | Description: Reads a trailer back.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: in -- source, at least SAVE_TRAILER_BYTES; track, loop -- outputs
 | Returns: N/A
 ----------------------*/
void savegame_unpack_trailer(const unsigned char *in, int *track, int *loop);

/*----------------------
 | savegame_write
 | Description: Stamps a header, compresses the payload if that makes it
 |   smaller, and writes the result to a slot, replacing whatever was there.
 |   A failure leaves the slot's previous contents alone.
 | Author: suinevere
 | Dependencies: savedata.h, saverle.h, saturn_backup.h
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0-based index;
 |         payload -- upstream bytes followed by the trailer; payloadLen --
 |         its length, at most SAVE_PAYLOAD_MAX; roomId -- the room the save
 |         was made in; work -- staging buffer; workCap -- its capacity, which
 |         must be at least SAVE_MAX_BYTES
 | Returns: SAT_BUP_OK, a SAT_BUP_ERR_* code, or SAVE_ERR_TOO_LARGE
 ----------------------*/
int savegame_write(unsigned long device, int slot,
                   const unsigned char *payload, int payloadLen,
                   unsigned short roomId,
                   unsigned char *work, int workCap);

/*----------------------
 | savegame_read
 | Description: Reads a slot, validates it, and only then writes anything into
 |   payload. Every refusal leaves payload exactly as the caller left it, which
 |   is what makes a corrupt slot survivable mid-game.
 | Author: suinevere
 | Dependencies: savedata.h, saverle.h, saturn_backup.h
 | Globals: N/A
 | Params: device -- device id; slot -- 0-based index; payload -- destination;
 |         payloadCap -- its capacity; payloadLen -- receives the length;
 |         roomId -- receives the room; work -- staging buffer; workCap -- its
 |         capacity, at least SAVE_MAX_BYTES
 | Returns: SAT_BUP_OK, a SAT_BUP_ERR_* code, or SAVE_ERR_BAD_MAGIC /
 |          _BAD_VERSION / _BAD_PAYLOAD / _TOO_LARGE
 ----------------------*/
int savegame_read(unsigned long device, int slot,
                  unsigned char *payload, int payloadCap, int *payloadLen,
                  unsigned short *roomId,
                  unsigned char *work, int workCap);

#ifdef __cplusplus
}
#endif

#endif /* SAVEGAME_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/savegame.c`:

```c
/*----------------------
 | savegame.c
 | Description: The assembly and slot I/O described by savegame.h.
 | Author: suinevere
 | Dependencies: savegame.h, savedata.h, saverle.h, saturn_backup.h, string.h
 ----------------------*/
#include <string.h>
#include "savegame.h"
#include "saverle.h"

/*----------------------
 | SAVE_COMMENT
 | Description: The ten characters the Saturn's Backup Manager shows beside
 |   the file.
 | Author: suinevere
 ----------------------*/
#define SAVE_COMMENT "HEARTALIEN"

void savegame_pack_trailer(unsigned char *out, int track, int loop)
{
    out[0] = (unsigned char)((track >> 8) & 0xFF);
    out[1] = (unsigned char)(track & 0xFF);
    out[2] = (unsigned char)(loop ? 1 : 0);
    out[3] = 0;
}

void savegame_unpack_trailer(const unsigned char *in, int *track, int *loop)
{
    int v = (int)((in[0] << 8) | in[1]);
    if (v & 0x8000) {
        v -= 0x10000;
    }
    if (track) *track = v;
    if (loop)  *loop = (in[2] != 0);
}

int savegame_write(unsigned long device, int slot,
                   const unsigned char *payload, int payloadLen,
                   unsigned short roomId,
                   unsigned char *work, int workCap)
{
    char name[12];
    int encoded;
    int storedLen;
    unsigned char flags;

    if (payloadLen <= 0 || payloadLen > SAVE_PAYLOAD_MAX ||
        workCap < SAVE_MAX_BYTES) {
        return SAVE_ERR_TOO_LARGE;
    }

    encoded = saverle_encode(payload, payloadLen, work + SAVE_HEADER_SIZE,
                             workCap - SAVE_HEADER_SIZE);
    if (encoded > 0) {
        flags = SAVE_FLAG_RLE;
        storedLen = encoded;
    } else {
        flags = 0;
        storedLen = payloadLen;
        memcpy(work + SAVE_HEADER_SIZE, payload, (size_t)payloadLen);
    }

    savedata_write_header(work, roomId, sat_bup_date_now(), flags,
                          (unsigned short)storedLen);
    savedata_slot_name(slot, name);
    return sat_bup_write(device, name, SAVE_COMMENT, work,
                         (long)(SAVE_HEADER_SIZE + storedLen), 1);
}

int savegame_read(unsigned long device, int slot,
                  unsigned char *payload, int payloadCap, int *payloadLen,
                  unsigned short *roomId,
                  unsigned char *work, int workCap)
{
    char name[12];
    SatBupEntry entry;
    unsigned short ver = 0;
    unsigned short room = 0;
    unsigned short storedLen = 0;
    unsigned char flags = 0;
    unsigned long date = 0;
    int rc;
    int fileLen;

    if (workCap < SAVE_MAX_BYTES || payloadCap < SAVE_PAYLOAD_MAX) {
        return SAVE_ERR_TOO_LARGE;
    }

    savedata_slot_name(slot, name);
    rc = sat_bup_dir(device, name, &entry);
    if (rc != SAT_BUP_OK) {
        return rc;
    }
    if (!entry.exists) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    fileLen = (int)entry.size;

    rc = sat_bup_read(device, name, work, (long)workCap);
    if (rc != SAT_BUP_OK) {
        return rc;
    }
    if (!savedata_read_header(work, &ver, &room, &date, &flags, &storedLen)) {
        return SAVE_ERR_BAD_MAGIC;
    }
    if (ver != SAVE_FORMAT_VERSION) {
        return SAVE_ERR_BAD_VERSION;
    }
    if (storedLen <= 0 || SAVE_HEADER_SIZE + (int)storedLen > fileLen) {
        return SAVE_ERR_BAD_PAYLOAD;
    }

    if (flags & SAVE_FLAG_RLE) {
        int decoded = saverle_decode(work + SAVE_HEADER_SIZE, (int)storedLen,
                                     payload, payloadCap);
        if (decoded <= 0) {
            return SAVE_ERR_BAD_PAYLOAD;
        }
        *payloadLen = decoded;
    } else {
        if ((int)storedLen > payloadCap) {
            return SAVE_ERR_BAD_PAYLOAD;
        }
        memcpy(payload, work + SAVE_HEADER_SIZE, (size_t)storedLen);
        *payloadLen = (int)storedLen;
    }

    *roomId = room;
    return SAT_BUP_OK;
}
```

Note the ordering that `test_corrupt_rle_leaves_payload_untouched` pins: the length check runs before decode, and decode writes into `payload` only after the header has validated. The RLE decoder writes into `payload` directly, so a *malformed but well-sized* payload could scribble before failing — this is why `saverle_decode` bounds every write against `dstCap` and returns `-1` rather than partially succeeding, and why that test asserts `back[0]` is untouched for the truncated case specifically.

- [ ] **Step 5: Run the tests and make sure they pass**

Run: `cd saturn/tests && gcc -std=c99 -Wall -Wextra -Werror -O1 -g -I../src -I../src/system -o run_tests_savegame test_savegame.c stub_saturn_backup.c ../src/savegame.c ../src/savedata.c ../src/saverle.c && ./run_tests_savegame`
Expected: `test_savegame: all passed`

- [ ] **Step 6: Append the suite to run_tests.sh**

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system \
    -o run_tests_savegame test_savegame.c stub_saturn_backup.c \
       ../src/savegame.c ../src/savedata.c ../src/saverle.c
./run_tests_savegame
```

- [ ] **Step 7: Commit**

```bash
git add saturn/src/savegame.h saturn/src/savegame.c saturn/tests/test_savegame.c saturn/tests/run_tests.sh
git commit -m "Assemble saves from a header, the engine's own payload and a Saturn trailer, compressing only when that shrinks them and validating a slot fully before any byte reaches the caller."
```

---

### Task 6: The real backup RAM backend and the stdio shim

**Files:**
- Create: `saturn/src/system/saturn_backup.cxx`
- Modify: `saturn/src/system/saturn_filestub.c`

**Interfaces:**
- Consumes: `saturn_backup.h` (Task 3), `savebuf` (Task 2).
- Produces: working `sat_bup_*` on Saturn; `savebuf *saturn_savebuf_stream(void)` so Task 7 can read the length back after `fclose`.

Nothing here is host-testable. Both halves are verified by Task 8's hardware checks.

- [ ] **Step 1: Write the backup backend**

Create `saturn/src/system/saturn_backup.cxx`, adapted from `..\Another-Saturn\saturn\src\system\saturn_backup.cxx`. The body is that file's, with `sat_bup_date_split` dropped (it is `savedata_date_split` now), `SAVE_MAX_BYTES` taken from `savedata.h` instead of redefined, and the signatures widened to the `unsigned long` / `long` forms in our header:

```c
/*----------------------
 | saturn_backup.cxx
 | Description: The Saturn side of saturn_backup.h, over SGL's BUP vector
 |   table and SRL's RTC. The only file in the port that includes sega_bup.h.
 | Author: suinevere
 | Dependencies: srl.hpp, sega_bup.h, saturn_backup.h, savedata.h
 | Globals: s_bupWork, s_bupCfg
 ----------------------*/
#include <srl.hpp>
#include "sega_bup.h"
#include "saturn_backup.h"
#include "savedata.h"

extern "C" {
#include <string.h>
}

/*----------------------
 | s_bupWork
 | Description: Work area the BIOS backup library needs. A plain static, which
 |   SH-2 GCC places in High Work RAM's BSS -- the right side of the bus for
 |   memory the BIOS writes through on its own schedule, unlike the save
 |   staging buffers, which are bulk blobs and live in LWRAM. Sized as the
 |   reference port ships it; shrink only against the link map.
 | Author: suinevere
 ----------------------*/
static uint32_t s_bupWork[0x1000];

/*----------------------
 | s_bupCfg
 | Description: BUP_Init fills one config per device. Kept alive for the
 |   lifetime of the program because the library retains the pointer.
 | Author: suinevere
 ----------------------*/
static BupConfig s_bupCfg[3];

/*----------------------
 | sat_bup_map_error
 | Description: Translates a raw BUP return code into this file's
 |   device-agnostic codes. sega_bup.h defines no BUP_OK constant -- success is
 |   the literal 0 -- so 0 is the only value that may map to SAT_BUP_OK.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: rc -- BUP_* return code, or 0 for success
 | Returns: a SAT_BUP_* code
 ----------------------*/
static int sat_bup_map_error(int32_t rc)
{
    switch (rc) {
    case 0:                     return SAT_BUP_OK;
    case BUP_NON:               return SAT_BUP_ERR_NONE;
    case BUP_UNFORMAT:          return SAT_BUP_ERR_UNFORMAT;
    case BUP_WRITE_PROTECT:     return SAT_BUP_ERR_PROTECTED;
    case BUP_NOT_ENOUGH_MEMORY: return SAT_BUP_ERR_NO_SPACE;
    case BUP_NOT_FOUND:         return SAT_BUP_ERR_NOT_FOUND;
    case BUP_FOUND:             return SAT_BUP_ERR_EXISTS;
    case BUP_NO_MATCH:          return SAT_BUP_ERR_BROKEN;
    case BUP_BROKEN:            return SAT_BUP_ERR_BROKEN;
    default:                    return SAT_BUP_ERR_BROKEN;
    }
}
```

Then `sat_bup_init`, `sat_bup_probe`, `sat_bup_dir`, `sat_bup_read`, `sat_bup_write`, `sat_bup_delete` and `sat_bup_date_now` transcribed from the reference file at lines 79-227, each keeping its banner comment, with these substitutions:

- `BUP_Stat(device, SAVE_MAX_BYTES, &st)` — `SAVE_MAX_BYTES` now comes from `savedata.h`.
- `uint32_t device` becomes `unsigned long device`, `int32_t size` becomes `long size`.
- `strncpy((char *)dir.filename, name, 11)` and `strncpy((char *)dir.comment, comment, 10)` are unchanged.
- Every definition is `extern "C"`.

- [ ] **Step 2: Point the stdio file half at a buffer**

In `saturn/src/system/saturn_filestub.c`, replace the nine failing stdio stubs (lines 19-35) with a `savebuf`-backed implementation, leaving `opendir`/`readdir`/`closedir`/`atexit` untouched. Update the file banner's first paragraph to say the file half is now real for one filename.

```c
#include "saturn_compat.h"
#include "dirent.h"
#include "savebuf.h"

/*----------------------
 | SAVE_STREAM_NAME
 | Description: The one path fopen accepts. main.c's QUICKSAVE_FILENAME, which
 |   is the only file the engine ever opens on Saturn.
 | Author: suinevere
 ----------------------*/
#define SAVE_STREAM_NAME "quicksave"

/*----------------------
 | HOTA_FILE
 | Description: The FILE saturn_compat.h forward-declares. One instance
 |   exists, because the engine opens at most one stream at a time and does so
 |   only between frames.
 | Author: suinevere
 ----------------------*/
struct HOTA_FILE {
    savebuf buf;
    int open;
};

/*----------------------
 | s_saveStream
 | Description: The single stream fopen hands out, and the buffer
 |   saturn_saveslot.cxx reads the written length back from.
 | Author: suinevere
 ----------------------*/
static struct HOTA_FILE s_saveStream;

/*----------------------
 | s_saveStorage / s_saveStorageCap
 | Description: Where the stream's bytes go, installed by
 |   saturn_savebuf_bind before the engine opens anything. NULL until then, so
 |   an fopen before the binding fails the way the old stub did.
 | Author: suinevere
 ----------------------*/
static unsigned char *s_saveStorage;
static int s_saveStorageCap;

/*----------------------
 | saturn_savebuf_bind
 | Description: Installs the storage fopen will wrap, and whether the next
 |   open is for writing.
 | Author: suinevere
 | Dependencies: savebuf.h
 | Globals: s_saveStorage, s_saveStorageCap
 | Params: data -- storage, in LWRAM; cap -- its capacity
 | Returns: N/A
 ----------------------*/
void saturn_savebuf_bind(unsigned char *data, int cap)
{
    s_saveStorage = data;
    s_saveStorageCap = cap;
}

/*----------------------
 | saturn_savebuf_stream
 | Description: The buffer behind the last stream, so a caller can read back
 |   how many bytes quicksave wrote, or whether an overflow was refused.
 | Author: suinevere
 | Dependencies: savebuf.h
 | Globals: s_saveStream
 | Params: N/A
 | Returns: the buffer, always non-NULL
 ----------------------*/
savebuf *saturn_savebuf_stream(void)
{
    return &s_saveStream.buf;
}

/*----------------------
 | s_saveReadLen
 | Description: How many bytes a read stream may hand out. quickload opens for
 |   reading, and the readable length is the decompressed payload's, which only
 |   the caller knows.
 | Author: suinevere
 ----------------------*/
static int s_saveReadLen;

/*----------------------
 | saturn_savebuf_set_length
 | Description: Declares that readable length before the next open.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: s_saveReadLen
 | Params: len -- readable length
 | Returns: N/A
 ----------------------*/
void saturn_savebuf_set_length(int len)
{
    s_saveReadLen = len;
}

/*----------------------
 | fopen / fclose / fread / fwrite / fseek / ftell / rewind / fgetc / fputc
 | Description: The file half of the C runtime, backed by savebuf for the one
 |   path the engine opens and failing for every other. fread, fwrite, fseek,
 |   ftell and rewind stay failing stubs: quicksave and quickload go through
 |   fgetc and fputc only, by way of common.c's fgetw and fputw.
 | Author: suinevere
 ----------------------*/
FILE *fopen(const char *path, const char *mode)
{
    int writing;
    int i;

    if (s_saveStorage == (unsigned char *)0 || s_saveStream.open) {
        return (FILE *)0;
    }
    for (i = 0; SAVE_STREAM_NAME[i] != 0; i++) {
        if (path[i] != SAVE_STREAM_NAME[i]) {
            return (FILE *)0;
        }
    }
    if (path[i] != 0) {
        return (FILE *)0;
    }

    writing = (mode[0] == 'w');
    if (writing) {
        savebuf_open_write(&s_saveStream.buf, s_saveStorage, s_saveStorageCap);
    } else {
        savebuf_open_read(&s_saveStream.buf, s_saveStorage, s_saveReadLen);
    }
    s_saveStream.open = 1;
    return &s_saveStream;
}

int fclose(FILE *s)
{
    if (s == &s_saveStream) {
        s_saveStream.open = 0;
    }
    return 0;
}

size_t fread(void *p, size_t sz, size_t n, FILE *s)        { (void)p; (void)sz; (void)n; (void)s; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *s) { (void)p; (void)sz; (void)n; (void)s; return 0; }
int    fseek(FILE *s, long off, int wh)                    { (void)s; (void)off; (void)wh; return -1; }
long   ftell(FILE *s)                                      { (void)s; return -1L; }
void   rewind(FILE *s)                                     { (void)s; }

int fgetc(FILE *s)
{
    if (s != &s_saveStream) {
        return EOF;
    }
    return savebuf_getc(&s_saveStream.buf);
}

int fputc(int c, FILE *s)
{
    if (s != &s_saveStream) {
        return EOF;
    }
    return savebuf_putc(&s_saveStream.buf, c);
}
```

Add matching declarations for `saturn_savebuf_bind`, `saturn_savebuf_stream` and `saturn_savebuf_set_length` to `saturn/src/system/saturn_compat.h`, inside its existing `extern "C"` block, each with a banner comment.

Note: `savebuf_getc` returns `-1` at end of data and `EOF` is `-1`, so `fgetc`'s contract is unchanged. `savebuf_putc` returns `-1` on overflow, which is also `EOF`, matching what the engine's `fputw` already tolerates.

- [ ] **Step 3: Verify the Saturn build**

Ask the user to run `cmd.exe /c ".\compile.bat debug"` from `saturn/`.
Expected: builds clean. A link error naming `savebuf_putc` means `savebuf.c` was not globbed — check it is under `saturn/src/`.

- [ ] **Step 4: Verify the host tests are unaffected**

Run: `sh saturn/tests/run_tests.sh`
Expected: twelve suites pass.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/system/saturn_backup.cxx saturn/src/system/saturn_filestub.c saturn/src/system/saturn_compat.h
git commit -m "Back the stdio file stubs with a real buffer for the quicksave path and add the backup RAM implementation, so the engine's save code reaches memory instead of a NULL it always checked."
```

---

### Task 7: Wiring — init, LWRAM buffers, and the slot glue

**Files:**
- Create: `saturn/src/system/saturn_saveslot.h`, `saturn/src/system/saturn_saveslot.cxx`
- Modify: `saturn/src/main.c`

**Interfaces:**
- Consumes: `savegame_write`/`savegame_read` (Task 5), `saturn_savebuf_bind`/`_stream`/`_set_length` (Task 6), `disc_current_track` (Task 4), `quicksave`/`quickload` (upstream `main.c`), `saturn_lwram_alloc` (`saturn_compat.h:196`).
- Produces: `int saturn_saveslot_init(void)`, `int saturn_saveslot_save(unsigned long device, int slot)`, `int saturn_saveslot_load(unsigned long device, int slot)`, `int saturn_saveslot_last_error(void)`.

- [ ] **Step 1: Write the header**

Create `saturn/src/system/saturn_saveslot.h`:

```c
/*----------------------
 | saturn_saveslot.h
 | Description: The glue between the engine's quicksave/quickload and
 |   savegame.h's slot I/O: owns the two LWRAM staging buffers, appends the
 |   CD-DA trailer on the way out, and re-issues the track on the way back.
 |   The only file that calls quicksave() and quickload().
 | Author: suinevere
 | Dependencies: savedata.h
 ----------------------*/
#ifndef SATURN_SAVESLOT_H
#define SATURN_SAVESLOT_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | saturn_saveslot_init
 | Description: Allocates the two LWRAM staging buffers and binds the stdio
 |   stream to the payload one. Call once, after platform_init.
 | Author: suinevere
 | Dependencies: saturn_compat.h
 | Globals: N/A
 | Params: N/A
 | Returns: 1 on success, 0 if either allocation failed, in which case saving
 |          and loading both refuse rather than crashing
 ----------------------*/
int saturn_saveslot_init(void);

/*----------------------
 | saturn_saveslot_save
 | Description: Serialises the running game into a slot. Must be called at the
 |   top of a frame, outside the task loop -- quicksave() asserts no active
 |   thread by toggling the aux bank to zero.
 | Author: suinevere
 | Dependencies: savegame.h, disc.h
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0-based index
 | Returns: SAT_BUP_OK, or a SAT_BUP_ERR_* / SAVE_ERR_* code
 ----------------------*/
int saturn_saveslot_save(unsigned long device, int slot);

/*----------------------
 | saturn_saveslot_load
 | Description: Restores a slot into the running game and restarts its music.
 |   Same frame-boundary requirement as saturn_saveslot_save, and additionally
 |   reads from the disc, so the drive must be idle.
 | Author: suinevere
 | Dependencies: savegame.h, disc.h
 | Globals: N/A
 | Params: device -- device id; slot -- 0-based index
 | Returns: SAT_BUP_OK, or a SAT_BUP_ERR_* / SAVE_ERR_* code, with the running
 |          game untouched on every failure
 ----------------------*/
int saturn_saveslot_load(unsigned long device, int slot);

/*----------------------
 | saturn_saveslot_last_error
 | Description: The code from the most recent save or load.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: a SAT_BUP_* or SAVE_ERR_* code
 ----------------------*/
int saturn_saveslot_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_SAVESLOT_H */
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/system/saturn_saveslot.cxx`:

```c
/*----------------------
 | saturn_saveslot.cxx
 | Description: The glue described by saturn_saveslot.h.
 | Author: suinevere
 | Dependencies: saturn_saveslot.h, savegame.h, savedata.h, saturn_compat.h,
 |   disc.h, main.h, string.h
 | Globals: s_payload, s_work, s_ready, s_lastError
 ----------------------*/
extern "C" {
#include <string.h>
#include "saturn_compat.h"
#include "saturn_saveslot.h"
#include "savegame.h"
#include "savedata.h"
#include "savebuf.h"
#include "disc.h"

extern int current_room;
void quicksave(void);
void quickload(void);
}

/*----------------------
 | s_payload / s_work
 | Description: The two staging buffers, in LWRAM rather than HWRAM BSS.
 |   Each is touched exactly twice per save -- filled, then handed on -- which
 |   is what saturn_compat.h means by a bulk blob. HWRAM has no room: measured
 |   .bss is 1,882,592 bytes against 770,048.
 | Author: suinevere
 ----------------------*/
static unsigned char *s_payload;
static unsigned char *s_work;

/*----------------------
 | s_ready / s_lastError
 | Description: Whether the buffers exist, and the outcome of the last call.
 | Author: suinevere
 ----------------------*/
static int s_ready;
static int s_lastError;

extern "C" int saturn_saveslot_init(void)
{
    s_payload = (unsigned char *)saturn_lwram_alloc(SAVE_MAX_BYTES);
    s_work = (unsigned char *)saturn_lwram_alloc(SAVE_MAX_BYTES);
    if (s_payload == (unsigned char *)0 || s_work == (unsigned char *)0) {
        s_ready = 0;
        return 0;
    }
    saturn_savebuf_bind(s_payload, SAVE_MAX_BYTES);
    s_ready = 1;
    return 1;
}

extern "C" int saturn_saveslot_save(unsigned long device, int slot)
{
    savebuf *stream;
    int written;
    int track;
    int loop = 0;

    if (!s_ready) {
        s_lastError = SAVE_ERR_TOO_LARGE;
        return s_lastError;
    }

    quicksave();

    stream = saturn_savebuf_stream();
    written = savebuf_len(stream);
    if (savebuf_error(stream) || written != SAVE_UPSTREAM_BYTES) {
        s_lastError = SAVE_ERR_BAD_PAYLOAD;
        return s_lastError;
    }

    track = disc_current_track(&loop);
    savegame_pack_trailer(s_payload + SAVE_UPSTREAM_BYTES, track, loop);

    s_lastError = savegame_write(device, slot, s_payload,
                                 SAVE_UPSTREAM_BYTES + SAVE_TRAILER_BYTES,
                                 (unsigned short)current_room,
                                 s_work, SAVE_MAX_BYTES);
    return s_lastError;
}

extern "C" int saturn_saveslot_load(unsigned long device, int slot)
{
    unsigned short roomId = 0;
    int payloadLen = 0;
    int track = -1;
    int loop = 0;
    int rc;

    if (!s_ready) {
        s_lastError = SAVE_ERR_TOO_LARGE;
        return s_lastError;
    }

    rc = savegame_read(device, slot, s_payload, SAVE_MAX_BYTES, &payloadLen,
                       &roomId, s_work, SAVE_MAX_BYTES);
    if (rc != SAT_BUP_OK) {
        s_lastError = rc;
        return s_lastError;
    }
    if (payloadLen != SAVE_UPSTREAM_BYTES + SAVE_TRAILER_BYTES) {
        s_lastError = SAVE_ERR_BAD_PAYLOAD;
        return s_lastError;
    }

    savegame_unpack_trailer(s_payload + SAVE_UPSTREAM_BYTES, &track, &loop);

    saturn_savebuf_set_length(SAVE_UPSTREAM_BYTES);
    quickload();

    if (track >= 0) {
        disc_play_track(track, loop);
    }

    s_lastError = SAT_BUP_OK;
    return s_lastError;
}

extern "C" int saturn_saveslot_last_error(void)
{
    return s_lastError;
}
```

`disc_play_track` runs **after** `quickload()`, never before: `quickload()` calls `load_room()` and `load_room_screen()`, and `disc.h` records that a disc read restarts a looping track.

- [ ] **Step 3: Initialise it in main**

In `saturn/src/main.c`, inside the `#ifdef HOTA_SATURN` region of `main()`, immediately after the `platform_init()` success check and before `disc_open(cue_path)`, add:

```c
#ifdef HOTA_SATURN
	sat_bup_init();
	if (!saturn_saveslot_init())
	{
		printf("saveslot: LWRAM allocation failed, saves disabled\n");
	}
#endif
```

Add `#include "system/saturn_backup.h"` and `#include "system/saturn_saveslot.h"` to `main.c`'s Saturn-only include block.

- [ ] **Step 4: Verify the Saturn build**

Ask the user to run `cmd.exe /c ".\compile.bat debug"` from `saturn/`.
Expected: builds clean.

- [ ] **Step 5: Verify the host build still links**

Run: `cd saturn/src && make`
Expected: `alien` builds. Nothing added in this task is in the host `OBJS` list, so a failure here means a stray edit outside the `HOTA_SATURN` guards.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/system/saturn_saveslot.h saturn/src/system/saturn_saveslot.cxx saturn/src/main.c
git commit -m "Wire the engine's quicksave to backup RAM through two LWRAM staging buffers, appending the playing track on the way out and restarting it after the load's disc reads finish."
```

---

### Task 8: The debug chord and hardware verification

**Files:**
- Modify: `saturn/src/system/input_srl.cxx`, `saturn/src/input.h`, `saturn/src/main.c`

**Interfaces:**
- Consumes: `saturn_saveslot_save`/`_load`/`_last_error` (Task 7).
- Produces: `int input_debug_chord(void)` returning `0`, `1` (save) or `2` (load); `void saturn_save_poll(void)` called once per frame from `run()`.

This chord is throwaway. The menu spec deletes it, and that deletion is a planned task there, not an oversight here.

- [ ] **Step 1: Add the chord read**

In `saturn/src/input.h`, declare:

```c
/*----------------------
 | input_debug_chord
 | Description: A temporary development trigger for save and load, until the
 |   save menu exists. Start plus A means save, Start plus C means load. Edge
 |   triggered, so holding the chord fires once.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: 0 for nothing, 1 to save, 2 to load
 ----------------------*/
int input_debug_chord(void);
```

In `saturn/src/system/input_srl.cxx`, add the implementation using the same `pad` object `update_keys` already reads, with a file-static `s_prevChord` for the edge:

```c
/*----------------------
 | s_prevChord
 | Description: Last frame's chord, so a held combination fires once.
 | Author: suinevere
 ----------------------*/
static int s_prevChord = 0;

/*----------------------
 | input_debug_chord
 | Description: Reads Start plus A or Start plus C as an edge.
 | Author: suinevere
 | Dependencies: srl.hpp
 | Globals: s_prevChord
 | Params: N/A
 | Returns: 0 for nothing, 1 to save, 2 to load
 ----------------------*/
extern "C" int input_debug_chord(void)
{
	SRL::Input::Digital pad(0);
	int chord = 0;

	if (pad.IsConnected() && pad.IsHeld(SRL::Input::Digital::Button::Start))
	{
		if (pad.IsHeld(SRL::Input::Digital::Button::A))
		{
			chord = 1;
		}
		else if (pad.IsHeld(SRL::Input::Digital::Button::C))
		{
			chord = 2;
		}
	}

	if (chord != 0 && chord == s_prevChord)
	{
		return 0;
	}
	s_prevChord = chord;
	return chord;
}
```

Both spellings above are the verified ones: `check_events` at `input_srl.cxx:87` constructs `SRL::Input::Digital pad(0);`, and the Digital button enumerant is `Start` (`srl_input.hpp:318`) — `START` at line 710 belongs to a different class and will not compile here.

- [ ] **Step 2: Poll it once per frame**

In `saturn/src/main.c`, add above `run()`:

```c
/*----------------------
 | saturn_save_poll
 | Description: Runs the development save chord, at the top of a frame and
 |   outside the task loop. Both quicksave and quickload require no active
 |   thread, which is only true here.
 | Author: suinevere
 | Dependencies: input.h, system/saturn_saveslot.h, system/saturn_backup.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#ifdef HOTA_SATURN
static void saturn_save_poll(void)
{
	int chord = input_debug_chord();

	if (chord == 1)
	{
		printf("save slot 0: %d\n", saturn_saveslot_save(SAT_BUP_INTERNAL, 0));
	}
	else if (chord == 2)
	{
		printf("load slot 0: %d\n", saturn_saveslot_load(SAT_BUP_INTERNAL, 0));
	}
}
#endif
```

Inside `run()`'s `while (cls.quit == 0)` loop at `main.c:741`, immediately after `check_events();`, add:

```c
#ifdef HOTA_SATURN
		saturn_save_poll();
#endif
```

- [ ] **Step 3: Confirm the upstream save code is untouched**

Run: `git diff --stat b419f8a -- saturn/src/main.c saturn/src/sprites.c`
Then: `git diff b419f8a -- saturn/src/sprites.c`
Expected: `sprites.c` shows **no** changes. `main.c` shows only the init block, the include lines, `saturn_save_poll` and its call — no hunk inside `quicksave`, `quickload`, or between `main.c:340` and `main.c:460`.

- [ ] **Step 4: Build and run the host tests**

Run: `sh saturn/tests/run_tests.sh`
Expected: twelve suites pass.

Ask the user to run `cmd.exe /c ".\compile.bat debug"` from `saturn/`.
Expected: builds clean.

- [ ] **Step 5: Hardware and emulator verification**

Hand these to the user with the built image. Each is pass or fail, not "looks fine":

1. **Round trip across a power cycle.** Play into a room with music. Start+A. Power off, power on, boot to the same point, Start+C. The game state returns and the same CD-DA track resumes.
2. **Full device.** Fill internal backup RAM from the BIOS Backup Manager until under 8 KB is free. Start+A. The console prints a non-zero code, the previous slot 0 still loads with Start+C, and the game keeps running.
3. **Cart present, then absent.** Repeat check 1 with a RAM cart inserted and again with it removed. Internal saves are unaffected by the cart's presence.
4. **Memory placement.** In the link map from `saturn/BuildDrop/`, confirm `s_bupWork` is in HWRAM and that `s_payload`/`s_work` are pointers, not arrays — the 12.5 KB they address must come from `saturn_lwram_alloc`, not BSS. The SDDRVS.TSK shortfall proved failures here are silent.
5. **The BIOS sees it.** In the Saturn's Backup Manager, slot 0 appears as `HOTASAVE1`, comment `HEARTALIEN`, with a plausible size and today's date.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/input.h saturn/src/system/input_srl.cxx saturn/src/main.c
git commit -m "Add a temporary Start plus A and Start plus C chord so the save path can be proven against a real BIOS and a real power cycle before any menu exists."
```

---

## Verification summary

| Spec requirement | Task |
| --- | --- |
| RLE with an explicit decline | 1 |
| RAM-backed FILE, engine code unmodified | 2, 6, 8 step 3 |
| Slot names, header, probing, device defaulting | 3 |
| `SAVE_MAX_BYTES` defined once | 3 |
| BUP date arithmetic host-tested | 3 |
| Saturn trailer carries the CD-DA track | 4, 5, 7 |
| Validate fully before mutating | 5 |
| Every failure row returns a code, never panics | 5 |
| BUP work area in HWRAM, staging buffers in LWRAM | 6, 7, 8 step 5 |
| `sat_bup_init` after `platform_init` | 7 |
| Save only at a frame boundary | 7, 8 |
| Four new host suites, twelve total | 1, 2, 3, 5 |
| Five hardware checks | 8 |
