#ifndef MENU_H
#define MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_PASSWORD_ROOM 7

#define MENU_START_ROOM    1

int  menu_front(void);

int  menu_gate(void);

void menu_reset_for_boot(void);

void menu_pause_poll(void);

void menu_soft_reset(void);

void menu_note_checkpoint(void);

#ifdef __cplusplus
}
#endif

#endif
