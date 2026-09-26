#ifndef __MAIN_INCLUDED__
#define __MAIN_INCLUDED__

void update_keys();

void play_intro(void);

int vm_enter_code(int code, int *playedAnimations);

void fade_out_begin(void);
void fade_out_finish(void);

void fade_out_begin_hold(unsigned int hold_ms);

#define ACCESS_CODE_SKIP_ARMED 2
#define ACCESS_CODE_SKIP_LIVE  1

extern unsigned int access_code_skip_at;
extern unsigned int access_code_load_seq;

#endif
