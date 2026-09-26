#ifndef SATURN_RESET_H
#define SATURN_RESET_H

#include "keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

int saturn_system_reset(void);

#define SATURN_RESET_CHORD     (PAD_BIT_A | PAD_BIT_B | PAD_BIT_C | PAD_BIT_START)

void saturn_reset_poll(void);

int saturn_reset_taken(void);

#ifdef __cplusplus
}
#endif
#endif
