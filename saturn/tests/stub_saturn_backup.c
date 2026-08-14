/*----------------------
 | stub_saturn_backup.c
 | Description: A host stand-in for saturn_backup.cxx, holding up to four
 |   files in memory and able to fail any call on demand, so the failure rows
 |   in the save design's error table are reachable from run_tests.sh.
 | Author: suinevere
 | Dependencies: stub_saturn_backup.h, savedata.h, string.h
 ----------------------*/
#include <string.h>
#include "stub_saturn_backup.h"
#include "savedata.h"

#define STUB_FILES 8

static struct {
    int used;
    unsigned long device;
    char name[12];
    unsigned char data[SAVE_MAX_BYTES];
    int len;
} s_files[STUB_FILES];

static int s_readFail;
static int s_writeFail;
static SatBupDev s_dev[3];
static unsigned long s_now;

void stub_bup_reset(void)
{
    memset(s_files, 0, sizeof(s_files));
    s_readFail = SAT_BUP_OK;
    s_writeFail = SAT_BUP_OK;
    memset(s_dev, 0, sizeof(s_dev));
    s_dev[SAT_BUP_INTERNAL].present = 1;
    s_dev[SAT_BUP_INTERNAL].formatted = 1;
    s_dev[SAT_BUP_INTERNAL].freeBytes = 32768;
    s_now = 0;
}

/*----------------------
 | find
 | Description: Locates a placed file by device and name.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: s_files
 | Params: device -- device id; name -- BUP filename
 | Returns: the index into s_files, or -1 when no such file is placed
 ----------------------*/
static int find(unsigned long device, const char *name)
{
    int i;
    for (i = 0; i < STUB_FILES; i++) {
        if (s_files[i].used && s_files[i].device == device &&
            strcmp(s_files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void stub_bup_place(unsigned long device, const char *name,
                    const unsigned char *data, int len)
{
    int i = find(device, name);

    if (i < 0) {
        for (i = 0; i < STUB_FILES; i++) {
            if (!s_files[i].used) {
                break;
            }
        }
        if (i == STUB_FILES) {
            return;
        }
    }
    s_files[i].used = 1;
    s_files[i].device = device;
    strcpy(s_files[i].name, name);
    memcpy(s_files[i].data, data, (size_t)len);
    s_files[i].len = len;
}

int stub_bup_fetch(unsigned long device, const char *name,
                   unsigned char *out, int cap)
{
    int i = find(device, name);
    if (i < 0 || s_files[i].len > cap) {
        return -1;
    }
    memcpy(out, s_files[i].data, (size_t)s_files[i].len);
    return s_files[i].len;
}

void stub_bup_fail_read(int code)  { s_readFail = code; }
void stub_bup_fail_write(int code) { s_writeFail = code; }

void stub_bup_set_device(unsigned long device, int present, int formatted,
                         int writeProtected, unsigned long freeBytes)
{
    s_dev[device].present = present;
    s_dev[device].formatted = formatted;
    s_dev[device].writeProtected = writeProtected;
    s_dev[device].freeBytes = freeBytes;
}

void sat_bup_init(void) { stub_bup_reset(); }

int sat_bup_probe(unsigned long device, SatBupDev *out)
{
    *out = s_dev[device];
    if (!out->present)        return SAT_BUP_ERR_NONE;
    if (!out->formatted)      return SAT_BUP_ERR_UNFORMAT;
    if (out->writeProtected)  return SAT_BUP_ERR_PROTECTED;
    return SAT_BUP_OK;
}

int sat_bup_dir(unsigned long device, const char *name, SatBupEntry *out)
{
    int i = find(device, name);
    memset(out, 0, sizeof(*out));
    if (i >= 0) {
        out->exists = 1;
        out->size = (unsigned long)s_files[i].len;
        out->date = 0;
    }
    return SAT_BUP_OK;
}

int sat_bup_read(unsigned long device, const char *name, void *dst, long size)
{
    int i;
    if (s_readFail != SAT_BUP_OK) {
        return s_readFail;
    }
    i = find(device, name);
    if (i < 0) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    if ((long)s_files[i].len > size) {
        return SAT_BUP_ERR_BROKEN;
    }
    memcpy(dst, s_files[i].data, (size_t)s_files[i].len);
    return SAT_BUP_OK;
}

int sat_bup_write(unsigned long device, const char *name, const char *comment,
                  const void *src, long size, int overwrite)
{
    (void)comment;
    (void)overwrite;
    if (s_writeFail != SAT_BUP_OK) {
        return s_writeFail;
    }
    if ((unsigned long)size > s_dev[device].freeBytes) {
        return SAT_BUP_ERR_NO_SPACE;
    }
    stub_bup_place(device, name, (const unsigned char *)src, (int)size);
    return SAT_BUP_OK;
}

int sat_bup_delete(unsigned long device, const char *name)
{
    int i = find(device, name);
    if (i < 0) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    s_files[i].used = 0;
    return SAT_BUP_OK;
}

unsigned long sat_bup_date_now(void) { return s_now; }
