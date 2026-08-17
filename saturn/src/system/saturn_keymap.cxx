/*----------------------
 | saturn_keymap.cxx
 | Description: The persistence described by saturn_keymap.h. Stores one
 |   KEYMAP_ENTRY_BYTES entry named HOTA_CFG, preferring SAT_BUP_INTERNAL and
 |   falling back to SAT_BUP_CART only when internal memory itself refuses.
 | Author: suinevere
 | Dependencies: saturn_keymap.h, saturn_backup.h, keymap.h
 | Globals: N/A
 ----------------------*/
extern "C" {
#include "saturn_keymap.h"
#include "saturn_backup.h"
#include "keymap.h"
}

#include <string.h>

/*----------------------
 | KEYMAP_BUP_NAME / KEYMAP_BUP_COMMENT
 | Description: The backup RAM filename and the comment shown by the
 |   Saturn's own Backup Manager. Eight characters each, well inside the
 |   eleven and ten the format allows.
 | Author: suinevere
 ----------------------*/
#define KEYMAP_BUP_NAME    "HOTA_CFG"
#define KEYMAP_BUP_COMMENT "CONTROLS"

/*----------------------
 | saturn_keymap_load
 | Description: See saturn_keymap.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_keymap_load(void)
{
    unsigned char buf[KEYMAP_ENTRY_BYTES];
    KeyMap m;
    int rc;

    memset(buf, 0, sizeof buf);
    rc = sat_bup_read(SAT_BUP_INTERNAL, KEYMAP_BUP_NAME, buf, KEYMAP_ENTRY_BYTES);
    if (rc == SAT_BUP_ERR_NOT_FOUND || rc == SAT_BUP_ERR_UNFORMAT) {
        rc = sat_bup_read(SAT_BUP_CART, KEYMAP_BUP_NAME, buf, KEYMAP_ENTRY_BYTES);
    }
    if (rc != SAT_BUP_OK) {
        return;
    }
    if (keymap_parse(&m, buf, KEYMAP_ENTRY_BYTES)) {
        keymap_set_active(&m);
    }
}

/*----------------------
 | saturn_keymap_save
 | Description: See saturn_keymap.h.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: m -- the mapping to store
 | Returns: SAT_BUP_OK, or the last SAT_BUP_ERR_* code
 ----------------------*/
extern "C" int saturn_keymap_save(const KeyMap *m)
{
    unsigned char buf[KEYMAP_ENTRY_BYTES];
    int rc;

    keymap_serialise(m, buf);

    rc = sat_bup_write(SAT_BUP_INTERNAL, KEYMAP_BUP_NAME, KEYMAP_BUP_COMMENT,
                       buf, KEYMAP_ENTRY_BYTES, 1);
    if (rc == SAT_BUP_ERR_NO_SPACE || rc == SAT_BUP_ERR_UNFORMAT ||
        rc == SAT_BUP_ERR_PROTECTED) {
        rc = sat_bup_write(SAT_BUP_CART, KEYMAP_BUP_NAME, KEYMAP_BUP_COMMENT,
                           buf, KEYMAP_ENTRY_BYTES, 1);
    }
    return rc;
}
