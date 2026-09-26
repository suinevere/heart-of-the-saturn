#ifndef CDTOC_H
#define CDTOC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CDTOC_WORDS         102
#define CDTOC_FIRST_WORD    99
#define CDTOC_LAST_WORD     100
#define CDTOC_LEADOUT_WORD  101

#define CDTOC_MAX_TRACK     99

int cdtoc_is_audio(const uint32_t *toc, int track);

uint32_t cdtoc_track_start(const uint32_t *toc, int track);

uint32_t cdtoc_track_end(const uint32_t *toc, int track);

int cdtoc_max_audio_track(const uint32_t *toc);

#ifdef __cplusplus
}
#endif

#endif
