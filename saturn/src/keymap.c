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
