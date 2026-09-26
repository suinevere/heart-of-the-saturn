#include "menu_state.h"
#include <string.h>

void menu_state_enter_title(MenuState *st)
{
    st->screen = MENU_TITLE;
    st->cursor = MENU_TITLE_ROW_START;
    st->konamiCount = 0;
}

void menu_state_enter_pause(MenuState *st)
{
    st->screen = MENU_PAUSE;
    st->cursor = MENU_PAUSE_ROW_RESUME;
}

void menu_state_enter_controls(MenuState *st, MenuScreen back)
{
    st->screen       = MENU_CONTROLS;
    st->cursor       = 0;
    st->capturing    = -1;
    st->mapDirty     = 0;
    st->map          = *keymap_active();
    st->returnScreen = back;
}

void menu_state_enter_death(MenuState *st)
{
    st->screen = MENU_DEATH;
    st->cursor = 0;
}

void menu_state_enter_options(MenuState *st, MenuScreen back)
{
    st->screen = MENU_OPTIONS;
    st->cursor = 0;
    st->optionsBack = back;
}

void menu_state_enter_levels(MenuState *st, MenuScreen back)
{
    st->screen = MENU_LEVEL_SELECT;
    st->levelCursor = 0;
    st->returnScreen = back;
}

int menu_state_levels(const MenuState *st, unsigned char *out, int cap)
{
    return checkpoint_visible(st->reached, st->unlocked, out, cap);
}

static int row_hidden(const MenuState *st, MenuScreen screen, int row)
{
    if (screen == MENU_TITLE) {
        return row == MENU_TITLE_ROW_LOAD && !st->hasSave;
    }
    if (screen == MENU_PAUSE) {
        if (row == MENU_PAUSE_ROW_SAVE) {
            return st->unlocked;
        }
        if (row == MENU_PAUSE_ROW_LOAD) {
            return !st->unlocked && !st->hasSave;
        }
        return 0;
    }
    if (screen == MENU_DEATH) {
        if (row == MENU_DEATH_ROW_SAVE || row == MENU_DEATH_ROW_SAVE_QUIT) {
            return st->unlocked;
        }
        if (row == MENU_DEATH_ROW_LOAD) {
            return !st->unlocked && !st->hasSave;
        }
        return 0;
    }
    return 0;
}

static int screen_rows_full(MenuScreen screen)
{
    if (screen == MENU_TITLE) {
        return MENU_TITLE_ROWS_FULL;
    }
    if (screen == MENU_PAUSE) {
        return MENU_PAUSE_ROWS_FULL;
    }
    if (screen == MENU_DEATH) {
        return MENU_DEATH_ROWS_FULL;
    }
    if (screen == MENU_OPTIONS) {
        return MENU_OPTIONS_ROWS_FULL;
    }
    return 0;
}

static int rows_visible(const MenuState *st, MenuScreen screen)
{
    int full = screen_rows_full(screen);
    int shown = 0;
    int row;

    for (row = 0; row < full; row++) {
        if (!row_hidden(st, screen, row)) {
            shown++;
        }
    }
    return shown;
}

static int death_row_order(const MenuState *st, int index)
{
    static const int LOCKED[MENU_DEATH_ROWS_FULL] = {
        MENU_DEATH_ROW_LOAD, MENU_DEATH_ROW_RESUME, MENU_DEATH_ROW_SAVE,
        MENU_DEATH_ROW_SAVE_QUIT, MENU_DEATH_ROW_TITLE
    };
    static const int UNLOCKED[MENU_DEATH_ROWS_FULL] = {
        MENU_DEATH_ROW_RESUME, MENU_DEATH_ROW_LOAD, MENU_DEATH_ROW_SAVE,
        MENU_DEATH_ROW_SAVE_QUIT, MENU_DEATH_ROW_TITLE
    };

    return st->unlocked ? UNLOCKED[index] : LOCKED[index];
}

static int row_at(const MenuState *st, MenuScreen screen, int cursor)
{
    int full = screen_rows_full(screen);
    int seen = 0;
    int i;

    for (i = 0; i < full; i++) {
        const int row = (screen == MENU_DEATH) ? death_row_order(st, i) : i;

        if (row_hidden(st, screen, row)) {
            continue;
        }
        if (seen == cursor) {
            return row;
        }
        seen++;
    }
    return full - 1;
}

int menu_state_title_rows(const MenuState *st)
{
    return rows_visible(st, MENU_TITLE);
}

int menu_state_title_row_at(const MenuState *st, int cursor)
{
    return row_at(st, MENU_TITLE, cursor);
}

int menu_state_pause_rows(const MenuState *st)
{
    return rows_visible(st, MENU_PAUSE);
}

int menu_state_pause_row_at(const MenuState *st, int cursor)
{
    return row_at(st, MENU_PAUSE, cursor);
}

int menu_state_death_rows(const MenuState *st)
{
    return rows_visible(st, MENU_DEATH);
}

int menu_state_death_row_at(const MenuState *st, int cursor)
{
    return row_at(st, MENU_DEATH, cursor);
}

int menu_state_options_rows(const MenuState *st)
{
    return rows_visible(st, MENU_OPTIONS);
}

int menu_state_options_row_at(const MenuState *st, int cursor)
{
    return row_at(st, MENU_OPTIONS, cursor);
}

typedef enum {
    KON_NONE = 0,
    KON_UP,
    KON_DOWN,
    KON_LEFT,
    KON_RIGHT,
    KON_CANCEL,
    KON_CONFIRM,
    KON_PAUSE,
    KON_AMBIGUOUS
} KonamiToken;

static const unsigned char KONAMI[MENU_KONAMI_LEN] = {
    KON_UP, KON_UP, KON_DOWN, KON_DOWN,
    KON_LEFT, KON_RIGHT, KON_LEFT, KON_RIGHT,
    KON_CANCEL, KON_CONFIRM, KON_PAUSE
};

static KonamiToken konami_token(const MenuInput *in)
{
    KonamiToken tok = KON_NONE;
    int n = 0;

    if (in->up)      { tok = KON_UP;      n++; }
    if (in->down)    { tok = KON_DOWN;    n++; }
    if (in->left)    { tok = KON_LEFT;    n++; }
    if (in->right)   { tok = KON_RIGHT;   n++; }
    if (in->cancel)  { tok = KON_CANCEL;  n++; }
    if (in->confirm) { tok = KON_CONFIRM; n++; }
    if (in->pause)   { tok = KON_PAUSE;   n++; }

    if (n > 1) {
        return KON_AMBIGUOUS;
    }
    return tok;
}

static int konami_advance(MenuState *st, const MenuInput *in)
{
    KonamiToken tok = konami_token(in);
    int i;

    if (tok == KON_NONE) {
        return 0;
    }

    if (st->konamiCount < MENU_KONAMI_LEN) {
        st->konamiHistory[st->konamiCount] = (unsigned char)tok;
        st->konamiCount++;
    } else {
        for (i = 1; i < MENU_KONAMI_LEN; i++) {
            st->konamiHistory[i - 1] = st->konamiHistory[i];
        }
        st->konamiHistory[MENU_KONAMI_LEN - 1] = (unsigned char)tok;
    }

    if (st->konamiCount < MENU_KONAMI_LEN) {
        return 0;
    }

    for (i = 0; i < MENU_KONAMI_LEN; i++) {
        if (st->konamiHistory[i] != KONAMI[i]) {
            return 0;
        }
    }

    st->konamiCount = 0;
    return 1;
}

static int konami_ended_an_attempt(const MenuState *st)
{
    if (st->konamiCount < 2) {
        return 0;
    }
    return st->konamiHistory[st->konamiCount - 1] == KON_CONFIRM
        && st->konamiHistory[st->konamiCount - 2] == KON_CANCEL;
}

static MenuAction step_title(MenuState *st, const MenuInput *in)
{
    int rows;

    if (konami_advance(st, in)) {
        st->unlocked = 1;
        menu_state_enter_levels(st, MENU_TITLE);
        return MENU_ACT_NONE;
    }
    if (in->confirm && konami_ended_an_attempt(st)) {
        return MENU_ACT_NONE;
    }

    rows = menu_state_title_rows(st);

    if (st->cursor >= rows) {
        st->cursor = rows - 1;
    }
    if (in->up) {
        st->cursor = (st->cursor + rows - 1) % rows;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % rows;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        int row = menu_state_title_row_at(st, st->cursor);

        if (row == MENU_TITLE_ROW_START) {
            return MENU_ACT_START_GAME;
        }
        if (row == MENU_TITLE_ROW_LOAD) {
            return MENU_ACT_LOAD_GAME;
        }
        menu_state_enter_options(st, MENU_TITLE);
        return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

static MenuAction step_options(MenuState *st, const MenuInput *in)
{
    int rows = menu_state_options_rows(st);
    int row;

    if (st->cursor >= rows) {
        st->cursor = rows - 1;
    }
    if (in->cancel) {
        st->screen = st->optionsBack;
        st->cursor = 0;
        return MENU_ACT_NONE;
    }
    if (in->up) {
        st->cursor = (st->cursor + rows - 1) % rows;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % rows;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        row = menu_state_options_row_at(st, st->cursor);

        if (row == MENU_OPTIONS_ROW_LEVELS) {
            menu_state_enter_levels(st, MENU_OPTIONS);
            return MENU_ACT_NONE;
        }
        if (row == MENU_OPTIONS_ROW_CONTROLS) {
            menu_state_enter_controls(st, MENU_OPTIONS);
            return MENU_ACT_NONE;
        }
        st->screen = st->optionsBack;
        st->cursor = 0;
        return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

static int level_row_start(const unsigned char *list, int i)
{
    int room = checkpoint_room(list[i]);

    while (i > 0 && checkpoint_room(list[i - 1]) == room) {
        i--;
    }
    return i;
}

static int level_row_end(const unsigned char *list, int n, int i)
{
    int room = checkpoint_room(list[i]);

    while (i + 1 < n && checkpoint_room(list[i + 1]) == room) {
        i++;
    }
    return i + 1;
}

static MenuAction step_levels(MenuState *st, const MenuInput *in)
{
    unsigned char list[CHECKPOINT_COUNT];
    int n = menu_state_levels(st, list, CHECKPOINT_COUNT);
    int start;
    int end;
    int col;

    if (n <= 0) {
        st->screen = st->returnScreen;
        return MENU_ACT_NONE;
    }
    if (st->levelCursor >= n) {
        st->levelCursor = n - 1;
    }
    if (st->levelCursor < 0) {
        st->levelCursor = 0;
    }

    if (in->cancel) {
        st->screen = st->returnScreen;
        st->cursor = 0;
        return MENU_ACT_NONE;
    }
    if (in->left) {
        st->levelCursor = (st->levelCursor + n - 1) % n;
        return MENU_ACT_NONE;
    }
    if (in->right) {
        st->levelCursor = (st->levelCursor + 1) % n;
        return MENU_ACT_NONE;
    }

    start = level_row_start(list, st->levelCursor);
    end = level_row_end(list, n, st->levelCursor);
    col = st->levelCursor - start;

    if (in->down) {
        int next = (end < n) ? end : 0;
        int nextEnd = level_row_end(list, n, next);

        st->levelCursor = (next + col < nextEnd) ? (next + col) : (nextEnd - 1);
        return MENU_ACT_NONE;
    }
    if (in->up) {
        int previous = (start > 0) ? level_row_start(list, start - 1)
                                   : level_row_start(list, n - 1);
        int previousEnd = level_row_end(list, n, previous);

        st->levelCursor = (previous + col < previousEnd) ? (previous + col)
                                                         : (previousEnd - 1);
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        st->checkpoint = (int)list[st->levelCursor];
        return MENU_ACT_START_CHECKPOINT;
    }
    return MENU_ACT_NONE;
}

static void ask_confirm(MenuState *st, MenuAction pending, MenuScreen back)
{
    st->screen = MENU_CONFIRM;
    st->pending = pending;
    st->confirmBack = back;
    st->confirmYes = pending == MENU_ACT_LOAD_GAME
                  || pending == MENU_ACT_SAVE_GAME;
}

static MenuAction step_pause(MenuState *st, const MenuInput *in)
{
    int rows = menu_state_pause_rows(st);
    int row;

    if (st->cursor >= rows) {
        st->cursor = rows - 1;
    }
    if (in->cancel || in->pause) {
        return MENU_ACT_RESUME;
    }
    if (in->up) {
        st->cursor = (st->cursor + rows - 1) % rows;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % rows;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        row = menu_state_pause_row_at(st, st->cursor);

        if (row == MENU_PAUSE_ROW_RESUME) {
            return MENU_ACT_RESUME;
        }
        if (row == MENU_PAUSE_ROW_SAVE) {
            if (!st->hasSave) {
                return MENU_ACT_SAVE_GAME;
            }
            ask_confirm(st, MENU_ACT_SAVE_GAME, MENU_PAUSE);
            return MENU_ACT_NONE;
        }
        if (row == MENU_PAUSE_ROW_LOAD) {
            if (st->unlocked) {
                menu_state_enter_levels(st, MENU_PAUSE);
            } else {
                ask_confirm(st, MENU_ACT_LOAD_GAME, MENU_PAUSE);
            }
            return MENU_ACT_NONE;
        }
        if (row == MENU_PAUSE_ROW_CONTROLS) {
            menu_state_enter_controls(st, MENU_PAUSE);
            return MENU_ACT_NONE;
        }
        ask_confirm(st, MENU_ACT_RETURN_TO_TITLE, MENU_PAUSE);
        return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

static MenuAction step_confirm(MenuState *st, const MenuInput *in)
{
    MenuAction action;

    if (in->left || in->right) {
        st->confirmYes = !st->confirmYes;
        return MENU_ACT_NONE;
    }
    if (in->cancel) {
        st->screen = st->confirmBack;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        if (!st->confirmYes) {
            st->screen = st->confirmBack;
            return MENU_ACT_NONE;
        }
        action = st->pending;

        if (action == MENU_ACT_RETURN_TO_TITLE) {
            menu_state_enter_title(st);
        } else {
            st->screen = st->confirmBack;
        }
        return action;
    }
    return MENU_ACT_NONE;
}

static MenuAction leave_controls(MenuState *st)
{
    st->screen = st->returnScreen;
    st->cursor = 0;

    if (st->mapDirty) {
        st->mapDirty = 0;
        return MENU_ACT_SAVE_KEYMAP;
    }
    return MENU_ACT_NONE;
}

static MenuAction step_controls(MenuState *st, const MenuInput *in)
{
    if (st->capturing >= 0) {
        if (in->pause) {
            st->capturing = -1;
            return MENU_ACT_NONE;
        }
        if (in->captured != PAD_NONE) {
            if (keymap_assign(&st->map, (KeymapRow)st->capturing, in->captured)) {
                st->mapDirty = 1;
            }
            st->capturing = -1;
        }
        return MENU_ACT_NONE;
    }

    if (in->up) {
        st->cursor = (st->cursor + MENU_CONTROLS_ROWS - 1) % MENU_CONTROLS_ROWS;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % MENU_CONTROLS_ROWS;
        return MENU_ACT_NONE;
    }
    if (in->cancel) {
        return leave_controls(st);
    }
    if (in->confirm) {
        if (st->cursor < KEYMAP_ROW_COUNT) {
            st->capturing = st->cursor;
            return MENU_ACT_NONE;
        }
        if (st->cursor == MENU_CONTROLS_ROW_RESET) {
            KeyMap before = st->map;
            keymap_defaults(&st->map);
            if (memcmp(&st->map, &before, sizeof(before)) != 0) {
                st->mapDirty = 1;
            }
            return MENU_ACT_NONE;
        }
        return leave_controls(st);
    }
    return MENU_ACT_NONE;
}

static MenuAction step_death(MenuState *st, const MenuInput *in)
{
    int rows = menu_state_death_rows(st);
    int row;

    if (st->cursor >= rows) {
        st->cursor = rows - 1;
    }
    if (in->cancel || in->pause) {
        return MENU_ACT_RESUME;
    }
    if (in->up) {
        st->cursor = (st->cursor + rows - 1) % rows;
        return MENU_ACT_NONE;
    }
    if (in->down) {
        st->cursor = (st->cursor + 1) % rows;
        return MENU_ACT_NONE;
    }
    if (in->confirm) {
        row = menu_state_death_row_at(st, st->cursor);

        if (row == MENU_DEATH_ROW_RESUME) {
            return MENU_ACT_RESUME;
        }
        if (row == MENU_DEATH_ROW_SAVE) {
            return MENU_ACT_SAVE_AND_RESUME;
        }
        if (row == MENU_DEATH_ROW_LOAD) {
            if (st->unlocked) {
                menu_state_enter_levels(st, MENU_DEATH);
            } else {
                ask_confirm(st, MENU_ACT_LOAD_GAME, MENU_DEATH);
            }
            return MENU_ACT_NONE;
        }
        if (row == MENU_DEATH_ROW_SAVE_QUIT) {
            return MENU_ACT_SAVE_AND_QUIT;
        }
        menu_state_enter_title(st);
        return MENU_ACT_NONE;
    }
    return MENU_ACT_NONE;
}

MenuAction menu_state_step(MenuState *st, const MenuInput *in)
{
    switch (st->screen) {
    case MENU_TITLE:    return step_title(st, in);
    case MENU_PAUSE:    return step_pause(st, in);
    case MENU_CONFIRM:  return step_confirm(st, in);
    case MENU_CONTROLS: return step_controls(st, in);
    case MENU_DEATH:    return step_death(st, in);
    case MENU_OPTIONS:  return step_options(st, in);
    case MENU_LEVEL_SELECT: return step_levels(st, in);
    default:            return MENU_ACT_NONE;
    }
}
