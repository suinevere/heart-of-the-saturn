#ifndef SATURN_MENUART_H
#define SATURN_MENUART_H

#include "menu_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

int  menu_art_load(void);

void menu_art_begin(int exclusive);

void menu_art_draw(const MenuItem *items, int count);

void menu_art_fade(int level);

void menu_art_present(void);

void menu_art_end(void);

#ifdef __cplusplus
}
#endif

#endif
