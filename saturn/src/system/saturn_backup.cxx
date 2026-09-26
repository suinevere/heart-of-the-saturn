#include <srl.hpp>
#include "sega_bup.h"
#include "saturn_backup.h"
#include "bup_devmap.h"
#include "savedata.h"

extern "C" {
#include <string.h>
}

static uint32_t s_bupWork[0x1000];

static BupConfig s_bupCfg[3];

static int s_internalIdx = 0;
static int s_cartIdx = 1;

static int sat_bup_hw(unsigned long device)
{
    if (device == SAT_BUP_INTERNAL) {
        return s_internalIdx;
    }
    if (device == SAT_BUP_CART) {
        return s_cartIdx;
    }
    return BUP_DEVMAP_NONE;
}

static int sat_bup_map_error(int32_t rc)
{
    switch (rc) {
    case 0:                     return SAT_BUP_OK;
    case BUP_NON:               return SAT_BUP_ERR_NONE;
    case BUP_UNFORMAT:          return SAT_BUP_ERR_UNFORMAT;
    case BUP_WRITE_PROTECT:     return SAT_BUP_ERR_PROTECTED;
    case BUP_NOT_ENOUGH_MEMORY: return SAT_BUP_ERR_NO_SPACE;
    case BUP_NOT_FOUND:         return SAT_BUP_ERR_NOT_FOUND;
    case BUP_FOUND:             return SAT_BUP_ERR_EXISTS;
    case BUP_NO_MATCH:          return SAT_BUP_ERR_BROKEN;
    case BUP_BROKEN:            return SAT_BUP_ERR_BROKEN;
    default:                    return SAT_BUP_ERR_BROKEN;
    }
}

extern "C" void sat_bup_init(void)
{
    int present[3];
    int i;

    BUP_Init((uint32_t *)BUP_LIB_ADDRESS, s_bupWork, s_bupCfg);

    for (i = 0; i < 3; ++i) {
        BupStat st;
        int32_t rc = BUP_Stat((uint32_t)i, SAVE_MAX_BYTES, &st);
        present[i] = (rc != BUP_NON) ? 1 : 0;
    }
    bupDevmapResolve(present, 3, &s_internalIdx, &s_cartIdx);
}

extern "C" int sat_bup_probe(unsigned long device, SatBupDev *out)
{
    BupStat st;
    memset(out, 0, sizeof(*out));

    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }

    int32_t rc = BUP_Stat((uint32_t)hw, SAVE_MAX_BYTES, &st);
    if (rc == BUP_NON) {
        return SAT_BUP_ERR_NONE;
    }
    out->present = 1;
    if (rc == BUP_UNFORMAT) {
        return SAT_BUP_ERR_UNFORMAT;
    }
    out->formatted = 1;
    if (rc == BUP_WRITE_PROTECT) {
        out->writeProtected = 1;
        return SAT_BUP_ERR_PROTECTED;
    }
    out->freeBytes = st.freesize;
    return SAT_BUP_OK;
}

extern "C" int sat_bup_dir(unsigned long device, const char *name, SatBupEntry *out)
{
    BupDir dir;
    memset(out, 0, sizeof(*out));
    memset(&dir, 0, sizeof(dir));

    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }

    int32_t rc = BUP_Dir((uint32_t)hw, (uint8_t *)name, 1, &dir);
    if (rc < 0) {
        return sat_bup_map_error(rc);
    }
    if (rc == 0) {
        return SAT_BUP_OK;
    }
    out->exists = 1;
    out->size = dir.datasize;
    out->date = dir.date;
    return SAT_BUP_OK;
}

extern "C" int sat_bup_read(unsigned long device, const char *name, void *dst,
                            long size)
{
    BupDir dir;
    memset(&dir, 0, sizeof(dir));

    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }

    int32_t dirRc = BUP_Dir((uint32_t)hw, (uint8_t *)name, 1, &dir);
    if (dirRc < 0) {
        return sat_bup_map_error(dirRc);
    }
    if (dirRc == 0) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    if (dir.datasize > (uint32_t)size) {
        return SAT_BUP_ERR_BROKEN;
    }

    int32_t rc = BUP_Read((uint32_t)hw, (uint8_t *)name, (uint8_t *)dst);
    return sat_bup_map_error(rc);
}

extern "C" int sat_bup_write(unsigned long device, const char *name,
                             const char *comment, const void *src,
                             long size, int overwrite)
{
    BupDir dir;
    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }

    memset(&dir, 0, sizeof(dir));
    strncpy((char *)dir.filename, name, 11);
    strncpy((char *)dir.comment, comment, 10);
    dir.language = BUP_ENGLISH;
    dir.date = sat_bup_date_now();
    dir.datasize = (uint32_t)size;
    dir.blocksize = 0;

    int32_t rc = BUP_Write((uint32_t)hw, &dir, (uint8_t *)src,
                           overwrite ? 1 : 0);

    if (overwrite && rc == BUP_FOUND) {
        BUP_Delete((uint32_t)hw, (uint8_t *)name);
        rc = BUP_Write((uint32_t)hw, &dir, (uint8_t *)src, 1);
    }

    return sat_bup_map_error(rc);
}

extern "C" int sat_bup_delete(unsigned long device, const char *name)
{
    const int hw = sat_bup_hw(device);
    if (hw == BUP_DEVMAP_NONE) {
        return SAT_BUP_ERR_NONE;
    }
    return sat_bup_map_error(BUP_Delete((uint32_t)hw, (uint8_t *)name));
}

extern "C" unsigned long sat_bup_date_now(void)
{
    SRL::Types::DateTime now = SRL::Types::DateTime::Now();
    BupDate d = now.ToBackupUnitDate();
    return BUP_SetDate(&d);
}
