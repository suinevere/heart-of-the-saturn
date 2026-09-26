#ifndef DISCSEC_H
#define DISCSEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISC_MAX_SECTOR_BYTES 2048

int32_t discsec_whole_sectors(int32_t bytes, int32_t sector_size);

int32_t discsec_tail_bytes(int32_t bytes, int32_t sector_size);

#define DISC_MAX_REQUEST_SECTORS 128

int32_t discsec_request_sectors(int32_t remaining, int32_t max_chunk);

#ifdef __cplusplus
}
#endif

#endif
