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
