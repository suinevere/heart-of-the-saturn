#ifndef MENU_STATE_H
#define MENU_STATE_H

#include "savedata.h"
#include "keymap.h"
#include "checkpoints.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MENU_NONE,
    MENU_TITLE,
    MENU_PAUSE,
    MENU_CONFIRM,
    MENU_CONTROLS,
    MENU_DEATH,
    MENU_OPTIONS,
    MENU_LEVEL_SELECT
} MenuScreen;

typedef enum {
    MENU_ACT_NONE,
    MENU_ACT_START_GAME,
    MENU_ACT_RESUME,
    MENU_ACT_SAVE_GAME,
    MENU_ACT_LOAD_GAME,
    MENU_ACT_RETURN_TO_TITLE,
    MENU_ACT_SAVE_KEYMAP,
    MENU_ACT_SAVE_AND_RESUME,
    MENU_ACT_SAVE_AND_QUIT,
    MENU_ACT_START_CHECKPOINT
} MenuAction;

typedef struct {
    int up, down, left, right, confirm, cancel, pause;
    PadButton captured;
} MenuInput;

#define MENU_KONAMI_LEN 11

typedef struct {
    MenuScreen screen;
    int        cursor;
    unsigned long device;
    int        cartPresent;
    int        confirmYes;
    MenuAction pending;
    MenuScreen confirmBack;
    SlotInfo   save;
    int        hasSave;
    MenuScreen returnScreen;
    MenuScreen optionsBack;
    KeyMap     map;
    int        capturing;
    int        mapDirty;
    unsigned long reached;
    int        unlocked;
    int        levelCursor;
    int        checkpoint;
    unsigned char konamiHistory[MENU_KONAMI_LEN];
    int        konamiCount;
} MenuState;

#define MENU_TITLE_ROW_START   0
#define MENU_TITLE_ROW_LOAD    1
#define MENU_TITLE_ROW_OPTIONS 2
#define MENU_TITLE_ROWS_FULL   3

int menu_state_title_rows(const MenuState *st);
int menu_state_title_row_at(const MenuState *st, int cursor);

void menu_state_enter_title(MenuState *st);

void menu_state_enter_pause(MenuState *st);

#define MENU_CONTROLS_ROWS       5
#define MENU_CONTROLS_ROW_RESET  3
#define MENU_CONTROLS_ROW_BACK   4

void menu_state_enter_controls(MenuState *st, MenuScreen back);

#define MENU_DEATH_ROW_LOAD      0
#define MENU_DEATH_ROW_RESUME    1
#define MENU_DEATH_ROW_SAVE      2
#define MENU_DEATH_ROW_SAVE_QUIT 3
#define MENU_DEATH_ROW_TITLE     4
#define MENU_DEATH_ROWS_FULL     5

void menu_state_enter_death(MenuState *st);

#define MENU_OPTIONS_ROW_LEVELS   0
#define MENU_OPTIONS_ROW_CONTROLS 1
#define MENU_OPTIONS_ROW_BACK     2
#define MENU_OPTIONS_ROWS_FULL    3

void menu_state_enter_options(MenuState *st, MenuScreen back);

void menu_state_enter_levels(MenuState *st, MenuScreen back);

int menu_state_levels(const MenuState *st, unsigned char *out, int cap);

#define MENU_PAUSE_ROW_RESUME   0
#define MENU_PAUSE_ROW_SAVE     1
#define MENU_PAUSE_ROW_LOAD     2
#define MENU_PAUSE_ROW_CONTROLS 3
#define MENU_PAUSE_ROW_TITLE    4
#define MENU_PAUSE_ROWS_FULL    5

int menu_state_pause_rows(const MenuState *st);
int menu_state_pause_row_at(const MenuState *st, int cursor);

int menu_state_death_rows(const MenuState *st);
int menu_state_death_row_at(const MenuState *st, int cursor);

int menu_state_options_rows(const MenuState *st);
int menu_state_options_row_at(const MenuState *st, int cursor);

MenuAction menu_state_step(MenuState *st, const MenuInput *in);

#ifdef __cplusplus
}
#endif

#endif
