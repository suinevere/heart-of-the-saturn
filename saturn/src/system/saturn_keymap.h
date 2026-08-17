/*----------------------
 | saturn_keymap.h
 | Description: Persists the player's control mapping to Saturn backup RAM,
 |   kept apart from saturn_saveslot.h on purpose. A save slot is per-
 |   playthrough game state; a control mapping is a per-console preference.
 |   Folding the two together would mean bumping the slot format every time a
 |   preference changes, and would pull control state into menu_state.h
 |   through the savedata.h include saveslot already carries.
 | Author: suinevere
 | Dependencies: keymap.h
 ----------------------*/
#ifndef SATURN_KEYMAP_H
#define SATURN_KEYMAP_H

#include "keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | saturn_keymap_load
 | Description: Loads the stored control mapping and installs it as active.
 |   Returns nothing because there is no failure a caller could act on: every
 |   failure -- missing entry, wrong format, unformatted or absent device --
 |   just leaves the active mapping at keymap_active's default, which is
 |   already a playable pad.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void saturn_keymap_load(void);

/*----------------------
 | saturn_keymap_save
 | Description: Serialises m and writes it to backup RAM, preferring internal
 |   memory and retrying on the cartridge only when internal memory itself is
 |   the problem -- full, unformatted, or write-protected.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the mapping to store
 | Returns: SAT_BUP_OK, or the last SAT_BUP_ERR_* code from saturn_backup.h
 ----------------------*/
int saturn_keymap_save(const KeyMap *m);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_KEYMAP_H */
