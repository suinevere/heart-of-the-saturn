extern "C" {
#include "saturn_progress.h"
#include "saturn_backup.h"
#include "checkpoints.h"
#include <string.h>
}

#define PROGRESS_BUP_NAME    "HOTA_PRG"
#define PROGRESS_BUP_COMMENT "PROGRESS"

extern "C" unsigned long saturn_progress_load(void)
{
    unsigned char buf[CHECKPOINT_PROGRESS_BYTES];
    unsigned long mask = 0UL;
    int rc;

    memset(buf, 0, sizeof buf);
    rc = sat_bup_read(SAT_BUP_INTERNAL, PROGRESS_BUP_NAME, buf,
                      CHECKPOINT_PROGRESS_BYTES);
    if (rc == SAT_BUP_ERR_NOT_FOUND || rc == SAT_BUP_ERR_UNFORMAT) {
        rc = sat_bup_read(SAT_BUP_CART, PROGRESS_BUP_NAME, buf,
                          CHECKPOINT_PROGRESS_BYTES);
    }
    if (rc != SAT_BUP_OK) {
        return 0UL;
    }
    if (!checkpoint_progress_parse(buf, CHECKPOINT_PROGRESS_BYTES, &mask)) {
        return 0UL;
    }
    return mask;
}

extern "C" int saturn_progress_save(unsigned long mask)
{
    unsigned char buf[CHECKPOINT_PROGRESS_BYTES];
    int rc;

    checkpoint_progress_serialise(mask, buf);

    rc = sat_bup_write(SAT_BUP_INTERNAL, PROGRESS_BUP_NAME,
                       PROGRESS_BUP_COMMENT, buf,
                       CHECKPOINT_PROGRESS_BYTES, 1);
    if (rc == SAT_BUP_ERR_NO_SPACE || rc == SAT_BUP_ERR_UNFORMAT ||
        rc == SAT_BUP_ERR_PROTECTED) {
        rc = sat_bup_write(SAT_BUP_CART, PROGRESS_BUP_NAME,
                           PROGRESS_BUP_COMMENT, buf,
                           CHECKPOINT_PROGRESS_BYTES, 1);
    }
    return rc;
}
