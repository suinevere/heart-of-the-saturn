/*----------------------
 | keymap.c
 | Description: The pure half of the controller menu: defaults, the raw-mask
 |   to key-global translation, and the one live mapping. The swap rule and
 |   the stored entry are in this file too, added by later tasks.
 |
 |   Design: docs/superpowers/specs/2026-08-16-hota-saturn-controller-menu-design.md
 | Author: suinevere
 | Dependencies: keymap.h
 ----------------------*/
#include "keymap.h"
#include <string.h>

/*----------------------
 | g_active / g_activeInit
 | Description: The mapping check_events reads, and whether it has been
 |   initialised. Lazily defaulted rather than statically, because a static
 |   initialiser would have to repeat the default in a second place.
 | Author: suinevere
 ----------------------*/
static KeyMap g_active;
static int    g_activeInit;

unsigned int keymap_button_bit(PadButton b)
{
    if (b == PAD_NONE) {
        return 0u;
    }
    return 1u << ((unsigned int)b - 1u);
}

void keymap_defaults(KeyMap *m)
{
    m->row[KEYMAP_ROW_RUN]     = PAD_A;
    m->row[KEYMAP_ROW_WHIP]    = PAD_B;
    m->row[KEYMAP_ROW_JUMP]    = PAD_C;
    m->row[KEYMAP_ROW_FORWARD] = PAD_NONE;
}

void keymap_apply(const KeyMap *m, unsigned int raw, int *a, int *b, int *c)
{
    unsigned int forward = keymap_button_bit(m->row[KEYMAP_ROW_FORWARD]);

    *a = (raw & keymap_button_bit(m->row[KEYMAP_ROW_RUN]))  ? 1 : 0;
    *b = (raw & keymap_button_bit(m->row[KEYMAP_ROW_WHIP])) ? 1 : 0;
    *c = (raw & keymap_button_bit(m->row[KEYMAP_ROW_JUMP])) ? 1 : 0;

    if (forward != 0u && (raw & forward) != 0u) {
        *a = 1;
        *c = 1;
    }
}

const KeyMap *keymap_active(void)
{
    if (!g_activeInit) {
        keymap_defaults(&g_active);
        g_activeInit = 1;
    }
    return &g_active;
}

void keymap_set_active(const KeyMap *m)
{
    g_active = *m;
    g_activeInit = 1;
}

/*----------------------
 | keymap_assign
 | Description: Binds a button to a row, swapping with whichever row already
 |   held it so that nothing is ever left unbound by accident and every
 |   capture is one reversible step.
 |
 |   Refuses exactly two things. A core row may not be set to PAD_NONE. And a
 |   swap may not hand a core row PAD_NONE, which is reachable only when the
 |   shortcut row is empty and the button picked for it belongs to a core row
 |   -- there the displaced row would have taken the shortcut's nothing and
 |   Run, Whip or Jump would have gone dead. The caller shows IN USE and the
 |   player moves the core row first, or picks a free button.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the mapping; row -- which action; b -- the button, or PAD_NONE
 |         to clear the shortcut row
 | Returns: 1 if the mapping changed, 0 if the assignment was refused and the
 |          mapping is untouched
 ----------------------*/
int keymap_assign(KeyMap *m, KeymapRow row, PadButton b)
{
    PadButton previous;
    int q;

    if (b == PAD_NONE) {
        if (row != KEYMAP_ROW_FORWARD) {
            return 0;
        }
        m->row[row] = PAD_NONE;
        return 1;
    }

    if (m->row[row] == b) {
        return 0;
    }

    previous = m->row[row];

    for (q = 0; q < KEYMAP_ROW_COUNT; q++) {
        if (q == (int)row || m->row[q] != b) {
            continue;
        }
        if (previous == PAD_NONE) {
            return 0;
        }
        m->row[q] = previous;
        break;
    }

    m->row[row] = b;
    return 1;
}

void keymap_serialise(const KeyMap *m, unsigned char *buf)
{
    int i;

    memset(buf, 0, KEYMAP_ENTRY_BYTES);
    buf[0] = 'H';
    buf[1] = 'O';
    buf[2] = 'T';
    buf[3] = 'C';
    buf[4] = KEYMAP_FORMAT_VERSION;

    for (i = 0; i < KEYMAP_ROW_COUNT; i++) {
        buf[6 + i] = (unsigned char)m->row[i];
    }
}

int keymap_parse(KeyMap *m, const unsigned char *buf, int len)
{
    KeyMap candidate;
    int i;
    int j;

    if (len < KEYMAP_ENTRY_BYTES) {
        return 0;
    }
    if (buf[0] != 'H' || buf[1] != 'O' || buf[2] != 'T' || buf[3] != 'C') {
        return 0;
    }
    if (buf[4] != KEYMAP_FORMAT_VERSION) {
        return 0;
    }

    for (i = 0; i < KEYMAP_ROW_COUNT; i++) {
        if (buf[6 + i] > (unsigned char)PAD_R) {
            return 0;
        }
        candidate.row[i] = (PadButton)buf[6 + i];
    }

    for (i = 0; i < KEYMAP_ROW_COUNT; i++) {
        if (i != KEYMAP_ROW_FORWARD && candidate.row[i] == PAD_NONE) {
            return 0;
        }
        for (j = i + 1; j < KEYMAP_ROW_COUNT; j++) {
            if (candidate.row[i] != PAD_NONE
                && candidate.row[i] == candidate.row[j]) {
                return 0;
            }
        }
    }

    *m = candidate;
    return 1;
}
