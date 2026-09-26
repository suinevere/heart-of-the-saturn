#ifndef SATURN_KEYMAP_H
#define SATURN_KEYMAP_H

#include "keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

void saturn_keymap_load(void);

int saturn_keymap_save(const KeyMap *m);

#ifdef __cplusplus
}
#endif

#endif
