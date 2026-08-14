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
