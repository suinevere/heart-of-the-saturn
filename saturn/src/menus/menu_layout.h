#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

#include "menu_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_ITEM_RECT      0
#define MENU_ITEM_GLYPH     1
#define MENU_ITEM_TITLE_ROW 2

#define MENU_TITLE_ROW_W 200
#define MENU_TITLE_ROW_H 18

#define MENU_RECT_BORDER 0
#define MENU_RECT_FILL   1

#define MENU_BOX_BORDER 2

#define MENU_RAMP_DIM       0
#define MENU_RAMP_SEL       1
#define MENU_RAMP_TITLE_DIM 2
#define MENU_RAMP_TITLE_SEL 3

#define MENU_LAYOUT_MAX_ITEMS 200

#define MENU_ROW_CHARS 32

typedef struct {
    unsigned char kind;
    unsigned char id;
    short         x;
    short         y;
    short         w;
    short         h;
    unsigned char ramp;
} MenuItem;

int menu_layout_build(const MenuState *st, const char *status,
                      MenuItem *out, int cap);

int menu_layout_message(const char *text, MenuItem *out, int cap);

void menu_layout_save_row(char *out, int cap, const SlotInfo *info);

const char *menu_layout_status_text(int err, unsigned long device);

#ifdef __cplusplus
}
#endif

#endif
