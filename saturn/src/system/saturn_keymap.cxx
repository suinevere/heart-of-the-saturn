extern "C" {
#include "saturn_keymap.h"
#include "saturn_backup.h"
#include "keymap.h"
#include <string.h>
}

#define KEYMAP_BUP_NAME    "HOTA_CFG"
#define KEYMAP_BUP_COMMENT "CONTROLS"

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
