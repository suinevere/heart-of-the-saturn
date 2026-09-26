#ifndef INPUT_H
#define INPUT_H

#include "keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int key_up, key_down, key_left, key_right;
extern int key_a, key_b, key_c, key_select;
extern int key_reset_record;

void input_swallow_held(void);

void check_events(void);

unsigned int input_raw_buttons(void);

void input_latch(void);
void input_latch_clear(void);

#ifdef __cplusplus
}
#endif

#endif
