/*----------------------
 | keymap.h
 | Description: The gameplay button mapping, as pure data plus the four
 |   operations on it. No srl.hpp, no engine headers, no backup RAM -- the
 |   same discipline savedata.h and menu_state.h keep, and the reason the
 |   swap rule and the chord can be tested with host gcc.
 |
 |   PadButton exists so menu_state.c can handle a button identity without
 |   knowing it corresponds to an SRL::Input::Digital::Button. Only
 |   input_srl.cxx makes that connection.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef KEYMAP_H
#define KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | PadButton
 | Description: A bindable button, or PAD_NONE for an empty binding. The
 |   values are contiguous from 1 so keymap_button_bit is one shift, and
 |   PAD_NONE is 0 so a memset-zeroed MenuInput reports no capture.
 | Author: suinevere
 ----------------------*/
typedef enum {
    PAD_NONE = 0,
    PAD_A, PAD_B, PAD_C, PAD_X, PAD_Y, PAD_Z, PAD_L, PAD_R
} PadButton;

/*----------------------
 | PAD_BIT_*
 | Description: One bit per physical control on port 0, so a frame of raw pad
 |   state is a single unsigned int. The eight bindable buttons occupy bits
 |   0-7 in PadButton order, which is what lets keymap_button_bit shift
 |   rather than switch. The directions and Start are here because menus read
 |   this same mask, and are deliberately not bindable.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | KeymapRow
 | Description: The three bindable actions, in screen order. Every row always
 |   holds a real button; PAD_NONE is never a legal binding.
 |
 |   There is deliberately no jump-forward row. Jump forward is run and jump
 |   held together, and keymap_apply reproduces that from the two rows below
 |   whatever they are bound to, so a fourth row naming one button for it
 |   would have been a second route to a move that already has one.
 | Author: suinevere
 ----------------------*/
typedef enum {
    KEYMAP_ROW_RUN,
    KEYMAP_ROW_WHIP,
    KEYMAP_ROW_JUMP,
    KEYMAP_ROW_COUNT
} KeymapRow;

/*----------------------
 | KeyMap
 | Description: One binding per row. No two rows may hold the same button, and
 |   no row may hold PAD_NONE; keymap_assign and keymap_parse are the only
 |   things that write one, and both enforce that.
 | Author: suinevere
 ----------------------*/
typedef struct {
    PadButton row[KEYMAP_ROW_COUNT];
} KeyMap;

/*----------------------
 | KEYMAP_ENTRY_BYTES / KEYMAP_FORMAT_VERSION
 | Description: The size of the stored entry and the format version it must
 |   carry, following savedata.h's convention. Sixteen bytes for nine used and
 |   seven of room, because the backup RAM cost of the slack is nothing next to
 |   a format bump.
 |
 |   Version 2 dropped the fourth binding when the jump-forward row went away.
 |   A stored version 1 entry is refused rather than read short, so a console
 |   carrying one falls back to the defaults instead of loading a row that no
 |   longer exists into one that does.
 | Author: suinevere
 ----------------------*/
#define KEYMAP_ENTRY_BYTES    16
#define KEYMAP_FORMAT_VERSION 2

/*----------------------
 | keymap_button_bit
 | Description: The PAD_BIT_* for a button.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- the button
 | Returns: its mask bit, or 0 for PAD_NONE -- which makes an unbound row
 |          test as never held without a special case at the call site
 ----------------------*/
unsigned int keymap_button_bit(PadButton b);

/*----------------------
 | keymap_defaults
 | Description: A, B and C -- exactly what input_srl.cxx hardwired before this
 |   module existed, so a console with no stored config plays identically to
 |   the previous build.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- map to fill in
 | Returns: N/A
 ----------------------*/
void keymap_defaults(KeyMap *m);

/*----------------------
 | keymap_apply
 | Description: Turns a frame of raw pad state into the three face-button key
 |   globals the engine reads.
 |
 |   There is no chord to implement. The engine reads level state from
 |   independent globals, so run and jump held together is jump forward
 |   whatever the two buttons are -- this function reproduces it by writing
 |   each global from its own binding and nothing else. That is also why there
 |   is no fourth row naming one button for the move: it would be a second
 |   route to something the first two already produce.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the mapping; raw -- PAD_BIT_* mask of everything held; a, b, c
 |         -- filled in with 0 or 1
 | Returns: N/A
 ----------------------*/
void keymap_apply(const KeyMap *m, unsigned int raw, int *a, int *b, int *c);

/*----------------------
 | keymap_active
 | Description: The mapping check_events reads. Defaults until something calls
 |   keymap_set_active, so a caller that never loads a config still gets a
 |   playable pad.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_active, g_activeInit
 | Params: N/A
 | Returns: the live mapping, borrowed -- callers copy it, never keep it
 ----------------------*/
const KeyMap *keymap_active(void);

/*----------------------
 | keymap_set_active
 | Description: Replaces the live mapping. The controls screen edits its own
 |   copy and calls this once on the way out, so cancelling cannot leave the
 |   player with controls they did not confirm.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_active, g_activeInit
 | Params: m -- the mapping to install, copied
 | Returns: N/A
 ----------------------*/
void keymap_set_active(const KeyMap *m);

/*----------------------
 | keymap_assign
 | Description: Binds a button to a row, swapping with whichever row already
 |   held it so that nothing is ever left unbound and every capture is one
 |   reversible step.
 |
 |   With the shortcut row gone this can no longer refuse a real button. Every
 |   row holds one, so the displaced row always receives one, and the swap
 |   always succeeds -- which is why the caller has no rejection state to show.
 |   The only zero returns are a button already in place and a PAD_NONE the
 |   caller should never send.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the mapping; row -- which action; b -- the button to bind
 | Returns: 1 if the mapping changed, 0 if it did not -- either b was already
 |          row's binding, or b was PAD_NONE, which is not a legal binding for
 |          any row. The mapping is untouched on both.
 ----------------------*/
int keymap_assign(KeyMap *m, KeymapRow row, PadButton b);

/*----------------------
 | keymap_serialise
 | Description: Packs a mapping into KEYMAP_ENTRY_BYTES. Layout: 0 magic
 |   'HOTC', 4 version, 5 reserved, 6..9 the four bindings, 10..15 zero.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the mapping; buf -- destination, must hold KEYMAP_ENTRY_BYTES
 | Returns: N/A
 ----------------------*/
void keymap_serialise(const KeyMap *m, unsigned char *buf);

/*----------------------
 | keymap_parse
 | Description: Reads a stored entry, validating everything before it writes
 |   anything: the magic, the version, the length, every button's range, that
 |   no two rows share a button, and that no core row is unbound. A corrupt
 |   config must never be worse than a fresh one, so the caller's response to
 |   a 0 is keymap_defaults.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- filled in only on success; buf -- the stored bytes; len --
 |         how many there are
 | Returns: 1 if the entry was valid and m was written, 0 otherwise with m
 |          untouched
 ----------------------*/
int keymap_parse(KeyMap *m, const unsigned char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* KEYMAP_H */
