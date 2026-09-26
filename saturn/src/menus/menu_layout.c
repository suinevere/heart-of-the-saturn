#include "menu_layout.h"
#include "savegame.h"
#include "checkpoints.h"

#define MENU_GLYPH_W 8
#define MENU_GLYPH_H 10

#define MENU_SCREEN_W 320
#define MENU_SCREEN_H 224

#define MENU_BOX_PAD_X 24
#define MENU_BOX_PAD_Y 12

#define MENU_ROW_DY    16
#define MENU_STATUS_DY 20

#define MENU_TITLE_CENTRE_Y 174
#define MENU_TITLE_ROW_DY   22

#define MENU_CTRL_GAP         2
#define MENU_CTRL_VALUE_CELLS 6

#define MENU_LEVEL_NAME_CELLS 6
#define MENU_LEVEL_GAP        3
#define MENU_LEVEL_WORD_CELLS 4
#define MENU_LEVEL_WORD_GAP   2

typedef struct {
    int x;
    int y;
    int w;
} MenuBox;

static void put_rect(MenuItem *out, int cap, int *n, int id, int x, int y,
                     int w, int h)
{
    if (*n >= cap) {
        return;
    }
    out[*n].kind = MENU_ITEM_RECT;
    out[*n].id = (unsigned char)id;
    out[*n].x = (short)x;
    out[*n].y = (short)y;
    out[*n].w = (short)w;
    out[*n].h = (short)h;
    out[*n].ramp = MENU_RAMP_DIM;
    (*n)++;
}

static void put_text(MenuItem *out, int cap, int *n, int x, int y,
                     const char *s, int ramp)
{
    while (*s != 0) {
        if (*n >= cap) {
            return;
        }
        if (*s != ' ' && (unsigned char)*s >= 0x20u
            && (unsigned char)*s <= 0x5Fu) {
            out[*n].kind = MENU_ITEM_GLYPH;
            out[*n].id = (unsigned char)*s;
            out[*n].x = (short)x;
            out[*n].y = (short)y;
            out[*n].w = MENU_GLYPH_W;
            out[*n].h = MENU_GLYPH_H;
            out[*n].ramp = (unsigned char)ramp;
            (*n)++;
        }
        x += MENU_GLYPH_W;
        s++;
    }
}

static int cells_of(const char *s)
{
    int n = 0;

    if (s == 0) {
        return 0;
    }
    while (s[n] != 0) {
        n++;
    }
    return n;
}

static int widest_of(const char *const *lines, int count)
{
    int widest = 0;
    int i;

    for (i = 0; i < count; i++) {
        int w = cells_of(lines[i]);

        if (w > widest) {
            widest = w;
        }
    }
    return widest;
}

static MenuBox box_emit(MenuItem *out, int cap, int *n, int contentCells,
                        int contentH)
{
    MenuBox box;
    int w = contentCells * MENU_GLYPH_W + 2 * MENU_BOX_PAD_X;
    int h = contentH + 2 * MENU_BOX_PAD_Y;
    int x = (MENU_SCREEN_W - w) / 2;
    int y = (MENU_SCREEN_H - h) / 2;

    put_rect(out, cap, n, MENU_RECT_BORDER, x, y, w, h);
    put_rect(out, cap, n, MENU_RECT_FILL, x + MENU_BOX_BORDER,
             y + MENU_BOX_BORDER, w - 2 * MENU_BOX_BORDER,
             h - 2 * MENU_BOX_BORDER);

    box.x = x + MENU_BOX_PAD_X;
    box.y = y + MENU_BOX_PAD_Y;
    box.w = contentCells * MENU_GLYPH_W;
    return box;
}

static int rows_height(int rows, const char *status)
{
    int h = (rows - 1) * MENU_ROW_DY + MENU_GLYPH_H;

    if (status != 0) {
        h += MENU_STATUS_DY;
    }
    return h;
}

static void put_centred(MenuItem *out, int cap, int *n, const MenuBox *box,
                        int y, const char *s, int ramp)
{
    put_text(out, cap, n,
             box->x + (box->w - cells_of(s) * MENU_GLYPH_W) / 2, y, s, ramp);
}

#define MENU_ROWS_MAX (MENU_DEATH_ROWS_FULL + 1)

static void build_rows(const char *const *lines, int count, int cursor,
                       const char *status, MenuItem *out, int cap, int *n)
{
    const char *all[MENU_ROWS_MAX];
    MenuBox box;
    int total = 0;
    int i;

    if (count > MENU_ROWS_MAX - 1) {
        count = MENU_ROWS_MAX - 1;
    }
    for (i = 0; i < count; i++) {
        all[total] = lines[i];
        total++;
    }
    if (status != 0) {
        all[total] = status;
        total++;
    }

    box = box_emit(out, cap, n, widest_of(all, total),
                   rows_height(count, status));

    for (i = 0; i < count; i++) {
        put_centred(out, cap, n, &box, box.y + i * MENU_ROW_DY, lines[i],
                    i == cursor ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    }
    if (status != 0) {
        put_centred(out, cap, n, &box,
                    box.y + (count - 1) * MENU_ROW_DY + MENU_STATUS_DY,
                    status, MENU_RAMP_DIM);
    }
}

static void append_char(char *dst, int cap, int *pos, char c)
{
    if (*pos + 1 >= cap) {
        return;
    }
    dst[*pos] = c;
    (*pos)++;
    dst[*pos] = 0;
}

static void append_str(char *dst, int cap, int *pos, const char *s)
{
    while (*s != 0) {
        append_char(dst, cap, pos, *s);
        s++;
    }
}

static void append_pad2(char *dst, int cap, int *pos, int v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > 99) {
        v = 99;
    }
    append_char(dst, cap, pos, (char)('0' + (v / 10)));
    append_char(dst, cap, pos, (char)('0' + (v % 10)));
}

static void append_room(char *dst, int cap, int *pos, unsigned short room)
{
    if (room > 999u) {
        room = 999u;
    }
    if (room >= 100u) {
        append_char(dst, cap, pos, (char)('0' + (room / 100u)));
    }
    if (room >= 10u) {
        append_char(dst, cap, pos, (char)('0' + ((room / 10u) % 10u)));
    }
    append_char(dst, cap, pos, (char)('0' + (room % 10u)));
}

void menu_layout_save_row(char *out, int cap, const SlotInfo *info)
{
    int pos = 0;
    int month = 0, day = 0, hour = 0, minute = 0;

    out[0] = 0;

    if (info->state == SLOT_EMPTY) {
        append_str(out, cap, &pos, "- EMPTY -");
        return;
    }
    if (info->state == SLOT_DAMAGED) {
        append_str(out, cap, &pos, "- DAMAGED -");
        return;
    }
    if (info->state == SLOT_OLD_VERSION) {
        append_str(out, cap, &pos, "- OLD SAVE -");
        return;
    }

    if (info->flags & SAVE_FLAG_CHECKPOINT) {
        int i = checkpoint_find((int)info->roomId, (int)info->entry);

        if (i >= 0) {
            append_str(out, cap, &pos, checkpoint_word(i));
            append_str(out, cap, &pos, "     ");
        } else {
            append_str(out, cap, &pos, "ROOM ");
            append_room(out, cap, &pos, info->roomId);
            append_str(out, cap, &pos, "  ");
        }
    } else {
        append_str(out, cap, &pos, "ROOM ");
        append_room(out, cap, &pos, info->roomId);
        append_str(out, cap, &pos, "  ");
    }

    savedata_date_split(info->date, &month, &day, &hour, &minute);
    append_pad2(out, cap, &pos, month);
    append_char(out, cap, &pos, '/');
    append_pad2(out, cap, &pos, day);
    append_char(out, cap, &pos, ' ');
    append_pad2(out, cap, &pos, hour);
    append_char(out, cap, &pos, ':');
    append_pad2(out, cap, &pos, minute);
}

const char *menu_layout_status_text(int err, unsigned long device)
{
    if (err == SAT_BUP_OK) {
        return 0;
    }

    switch (err) {
    case SAVE_ERR_TOO_LARGE:    return "SAVE STATE TOO LARGE";
    case SAT_BUP_ERR_NONE:      return "NO BACKUP DEVICE";
    case SAT_BUP_ERR_UNFORMAT:  return (device == SAT_BUP_CART)
                                       ? "CARTRIDGE UNFORMATTED"
                                       : "BACKUP RAM UNFORMATTED";
    case SAT_BUP_ERR_PROTECTED: return "CARTRIDGE WRITE PROTECTED";
    case SAT_BUP_ERR_NO_SPACE:  return "NOT ENOUGH SPACE";
    case SAT_BUP_ERR_NOT_FOUND: return "SAVE NOT FOUND";
    case SAT_BUP_ERR_EXISTS:    return "SLOT ALREADY IN USE";
    case SAT_BUP_ERR_BROKEN:    return "SAVE DATA DAMAGED";
    default:                    return "SAVE FAILED";
    }
}

static int title_row_y(int count, int index)
{
    return MENU_TITLE_CENTRE_Y - MENU_TITLE_ROW_H / 2
           - (count - 1) * MENU_TITLE_ROW_DY / 2 + index * MENU_TITLE_ROW_DY;
}

static void build_title(const MenuState *st, MenuItem *out, int cap, int *n)
{
    int count = menu_state_title_rows(st);
    int i;

    for (i = 0; i < count && *n < cap; i++) {
        MenuItem *it = &out[(*n)++];

        it->kind = MENU_ITEM_TITLE_ROW;
        it->id = (unsigned char)menu_state_title_row_at(st, i);
        it->x = (short)((MENU_SCREEN_W - MENU_TITLE_ROW_W) / 2);
        it->y = (short)title_row_y(count, i);
        it->w = MENU_TITLE_ROW_W;
        it->h = MENU_TITLE_ROW_H;
        it->ramp = (unsigned char)(st->cursor == i ? MENU_RAMP_TITLE_SEL
                                                   : MENU_RAMP_TITLE_DIM);
    }
}

static void build_pause(const MenuState *st, const char *status, MenuItem *out,
                        int cap, int *n)
{
    static const char *const LABELS[MENU_PAUSE_ROWS_FULL] = {
        "RESUME", "SAVE GAME", "LOAD GAME", "CONTROLS", "RETURN TO TITLE"
    };
    const char *rows[MENU_PAUSE_ROWS_FULL];
    int count = menu_state_pause_rows(st);
    int i;

    for (i = 0; i < count; i++) {
        const int row = menu_state_pause_row_at(st, i);

        rows[i] = (row == MENU_PAUSE_ROW_LOAD && st->unlocked)
                      ? "LEVEL SELECT"
                      : LABELS[row];
    }
    build_rows(rows, count, st->cursor, status, out, cap, n);
}

static void build_options(const MenuState *st, const char *status,
                          MenuItem *out, int cap, int *n)
{
    static const char *const LABELS[MENU_OPTIONS_ROWS_FULL] = {
        "LEVEL SELECT", "CONTROLS", "BACK"
    };
    const char *rows[MENU_OPTIONS_ROWS_FULL];
    int count = menu_state_options_rows(st);
    int i;

    for (i = 0; i < count; i++) {
        rows[i] = LABELS[menu_state_options_row_at(st, i)];
    }
    build_rows(rows, count, st->cursor, status, out, cap, n);
}

static void build_death(const MenuState *st, const char *status, MenuItem *out,
                        int cap, int *n)
{
    static const char *const LABELS[MENU_DEATH_ROWS_FULL] = {
        "LOAD GAME", "RESUME", "SAVE & RESUME", "SAVE & QUIT", "QUIT"
    };
    const char *rows[MENU_DEATH_ROWS_FULL];
    int count = menu_state_death_rows(st);
    int i;

    for (i = 0; i < count; i++) {
        const int row = menu_state_death_row_at(st, i);

        rows[i] = (row == MENU_DEATH_ROW_LOAD && st->unlocked)
                      ? "LEVEL SELECT"
                      : LABELS[row];
    }
    build_rows(rows, count, st->cursor, status, out, cap, n);
}

#define MENU_CONFIRM_ANSWER_GAP 7

static void build_confirm(const MenuState *st, MenuItem *out, int cap, int *n)
{
    static const char *const ANSWERS = "YES    NO";
    char save[MENU_ROW_CHARS];
    const char *lines[4];
    MenuBox box;
    int count;
    int answerY;
    int answerX;
    int i;

    if (st->pending == MENU_ACT_RETURN_TO_TITLE) {
        lines[0] = "RETURN TO TITLE ?";
        lines[1] = "PROGRESS WILL BE LOST";
        count = 2;
    } else if (st->pending == MENU_ACT_LOAD_GAME) {
        menu_layout_save_row(save, MENU_ROW_CHARS, &st->save);
        lines[0] = "LOAD GAME ?";
        lines[1] = save;
        lines[2] = "PROGRESS WILL BE LOST";
        count = 3;
    } else {
        menu_layout_save_row(save, MENU_ROW_CHARS, &st->save);
        lines[0] = "OVERWRITE SAVE ?";
        lines[1] = save;
        count = 2;
    }
    lines[count] = ANSWERS;

    box = box_emit(out, cap, n, widest_of(lines, count + 1),
                   rows_height(count + 1, 0));

    for (i = 0; i < count; i++) {
        put_centred(out, cap, n, &box, box.y + i * MENU_ROW_DY, lines[i],
                    MENU_RAMP_DIM);
    }

    answerY = box.y + count * MENU_ROW_DY;
    answerX = box.x + (box.w - cells_of(ANSWERS) * MENU_GLYPH_W) / 2;

    put_text(out, cap, n, answerX, answerY, "YES",
             st->confirmYes ? MENU_RAMP_SEL : MENU_RAMP_DIM);
    put_text(out, cap, n, answerX + MENU_CONFIRM_ANSWER_GAP * MENU_GLYPH_W,
             answerY, "NO",
             st->confirmYes ? MENU_RAMP_DIM : MENU_RAMP_SEL);
}

static const char *button_name(PadButton b)
{
    switch (b) {
    case PAD_A: return "A";
    case PAD_B: return "B";
    case PAD_C: return "C";
    case PAD_X: return "X";
    case PAD_Y: return "Y";
    case PAD_Z: return "Z";
    case PAD_L: return "L";
    case PAD_R: return "R";
    default:    return "NONE";
    }
}

static void build_controls(const MenuState *st, const char *status,
                           MenuItem *out, int cap, int *n)
{
    static const char *const LABELS[MENU_CONTROLS_ROWS] = {
        "RUN/SHOOT/SHIELD", "WHIP", "JUMP", "RESET DEFAULTS", "BACK"
    };
    const char *foot = status;
    MenuBox box;
    int cells;
    int i;

    if (foot == 0 && st->capturing >= 0) {
        foot = "START CANCELS";
    }

    cells = widest_of(LABELS, MENU_CONTROLS_ROWS) + MENU_CTRL_GAP
          + MENU_CTRL_VALUE_CELLS;

    if (cells_of(foot) > cells) {
        cells = cells_of(foot);
    }

    box = box_emit(out, cap, n, cells, rows_height(MENU_CONTROLS_ROWS, foot));

    for (i = 0; i < MENU_CONTROLS_ROWS; i++) {
        int y = box.y + i * MENU_ROW_DY;
        int ramp = st->cursor == i ? MENU_RAMP_SEL : MENU_RAMP_DIM;

        put_text(out, cap, n, box.x, y, LABELS[i], ramp);

        if (i < KEYMAP_ROW_COUNT) {
            const char *value = (st->capturing == i)
                              ? "PRESS?"
                              : button_name(st->map.row[i]);

            put_text(out, cap, n,
                     box.x + box.w - cells_of(value) * MENU_GLYPH_W, y,
                     value, ramp);
        }
    }

    if (foot != 0) {
        put_centred(out, cap, n, &box,
                    box.y + (MENU_CONTROLS_ROWS - 1) * MENU_ROW_DY
                        + MENU_STATUS_DY,
                    foot, MENU_RAMP_DIM);
    }
}

static void build_levels(const MenuState *st, MenuItem *out, int cap, int *n)
{
    static const char *const HEADING = "LEVEL SELECT";
    unsigned char list[CHECKPOINT_COUNT];
    int count = menu_state_levels(st, list, CHECKPOINT_COUNT);
    MenuBox box;
    int rooms = 0;
    int widestRow = 0;
    int previousRoom = -1;
    int column = 0;
    int cells;
    int row;
    int i;

    for (i = 0; i < count; i++) {
        int room = checkpoint_room((int)list[i]);

        if (room != previousRoom) {
            previousRoom = room;
            rooms++;
            column = 0;
        }
        column++;

        if (column > widestRow) {
            widestRow = column;
        }
    }

    if (rooms == 0) {
        return;
    }

    cells = MENU_LEVEL_NAME_CELLS + MENU_LEVEL_GAP
          + widestRow * MENU_LEVEL_WORD_CELLS
          + (widestRow - 1) * MENU_LEVEL_WORD_GAP;

    if (cells_of(HEADING) > cells) {
        cells = cells_of(HEADING);
    }

    box = box_emit(out, cap, n, cells, rows_height(rooms + 1, 0));
    put_centred(out, cap, n, &box, box.y, HEADING, MENU_RAMP_DIM);

    previousRoom = -1;
    row = -1;
    column = 0;

    for (i = 0; i < count; i++) {
        int index = (int)list[i];
        int room = checkpoint_room(index);
        int y;

        if (room != previousRoom) {
            previousRoom = room;
            row++;
            column = 0;
            put_text(out, cap, n, box.x, box.y + (row + 1) * MENU_ROW_DY,
                     checkpoint_room_name(room), MENU_RAMP_DIM);
        }

        y = box.y + (row + 1) * MENU_ROW_DY;
        put_text(out, cap, n,
                 box.x + (MENU_LEVEL_NAME_CELLS + MENU_LEVEL_GAP
                          + column * (MENU_LEVEL_WORD_CELLS
                                      + MENU_LEVEL_WORD_GAP))
                     * MENU_GLYPH_W,
                 y, checkpoint_word(index),
                 st->levelCursor == i ? MENU_RAMP_SEL : MENU_RAMP_DIM);
        column++;
    }
}

int menu_layout_message(const char *text, MenuItem *out, int cap)
{
    MenuBox box;
    int n = 0;

    box = box_emit(out, cap, &n, cells_of(text), rows_height(1, 0));
    put_centred(out, cap, &n, &box, box.y, text, MENU_RAMP_SEL);
    return n;
}

int menu_layout_build(const MenuState *st, const char *status, MenuItem *out,
                      int cap)
{
    int n = 0;

    switch (st->screen) {
    case MENU_TITLE:    build_title(st, out, cap, &n); break;
    case MENU_PAUSE:    build_pause(st, status, out, cap, &n); break;
    case MENU_CONFIRM:  build_confirm(st, out, cap, &n); break;
    case MENU_CONTROLS: build_controls(st, status, out, cap, &n); break;
    case MENU_DEATH:    build_death(st, status, out, cap, &n); break;
    case MENU_OPTIONS:  build_options(st, status, out, cap, &n); break;
    case MENU_LEVEL_SELECT: build_levels(st, out, cap, &n); break;
    default: break;
    }
    return n;
}
