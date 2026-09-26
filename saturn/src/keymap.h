#ifndef KEYMAP_H
#define KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAD_NONE = 0,
    PAD_A, PAD_B, PAD_C, PAD_X, PAD_Y, PAD_Z, PAD_L, PAD_R
} PadButton;

#define PAD_BIT_A     0x0001u
#define PAD_BIT_B     0x0002u
#define PAD_BIT_C     0x0004u
#define PAD_BIT_X     0x0008u
#define PAD_BIT_Y     0x0010u
#define PAD_BIT_Z     0x0020u
#define PAD_BIT_L     0x0040u
#define PAD_BIT_R     0x0080u
#define PAD_BIT_UP    0x0100u
#define PAD_BIT_DOWN  0x0200u
#define PAD_BIT_LEFT  0x0400u
#define PAD_BIT_RIGHT 0x0800u
#define PAD_BIT_START 0x1000u

typedef enum {
    KEYMAP_ROW_RUN,
    KEYMAP_ROW_WHIP,
    KEYMAP_ROW_JUMP,
    KEYMAP_ROW_COUNT
} KeymapRow;

typedef struct {
    PadButton row[KEYMAP_ROW_COUNT];
} KeyMap;

#define KEYMAP_ENTRY_BYTES    16
#define KEYMAP_FORMAT_VERSION 2

unsigned int keymap_button_bit(PadButton b);

void keymap_defaults(KeyMap *m);

void keymap_apply(const KeyMap *m, unsigned int raw, int *a, int *b, int *c);

const KeyMap *keymap_active(void);

void keymap_set_active(const KeyMap *m);

int keymap_assign(KeyMap *m, KeymapRow row, PadButton b);

void keymap_serialise(const KeyMap *m, unsigned char *buf);

int keymap_parse(KeyMap *m, const unsigned char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif
