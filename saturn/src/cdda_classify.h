#ifndef CDDA_CLASSIFY_H
#define CDDA_CLASSIFY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CDDA_FORGET  = 0,
    CDDA_RESUME  = 1,
    CDDA_RESTART = 2
} cdda_action;

cdda_action cdda_classify(int was_playing, int loop, int observed,
                          uint32_t fad, uint32_t start, uint32_t end);

#ifdef __cplusplus
}
#endif

#endif
