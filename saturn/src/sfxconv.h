#ifndef SFXCONV_H
#define SFXCONV_H

#ifdef __cplusplus
extern "C" {
#endif

#define SFXCONV_MIN_PLAYABLE 0x900

signed char sfxconv_decode_byte(unsigned char u);

int sfxconv_locate(int index, int *out_offset, int *out_length);

int sfxconv_padded_size(int length);

void sfxconv_decode_into(int offset, int length, signed char *dst, int dst_size);

#ifdef __cplusplus
}
#endif

#endif
