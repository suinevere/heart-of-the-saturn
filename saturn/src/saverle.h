#ifndef SAVERLE_H
#define SAVERLE_H

#ifdef __cplusplus
extern "C" {
#endif

#define SAVERLE_MAX_LITERAL 128
#define SAVERLE_MIN_RUN       3
#define SAVERLE_MAX_RUN     130

int saverle_encode(const unsigned char *src, int srcLen,
                   unsigned char *dst, int dstCap);

int saverle_decode(const unsigned char *src, int srcLen,
                   unsigned char *dst, int dstCap);

#ifdef __cplusplus
}
#endif

#endif
