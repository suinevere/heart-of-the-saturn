/*----------------------
 | saturn_backup.h
 | Description: A small C interface for Saturn backup RAM, backed by SGL's BUP
 |   vector table. It exists so savedata.c and savegame.c can read and write
 |   saves without pulling <srl.hpp> into an engine translation unit -- the
 |   engine's headers wrap SGL's C headers in extern "C" and mixing the two
 |   include orders is fragile. Same seam saturn_bootart.h draws for artwork.
 |
 |   Every entry point takes the device explicitly. There is deliberately no
 |   implicit "current device" here: the menu owns that choice.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef SATURN_BACKUP_H
#define SATURN_BACKUP_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAT_BUP_INTERNAL / SAT_BUP_CART
 | Description: Device ids, matching SGL's BUP_MAIN_UNIT and BUP_CURTRIDGE.
 | Author: suinevere
 ----------------------*/
#define SAT_BUP_INTERNAL 1
#define SAT_BUP_CART     2

/*----------------------
 | SAT_BUP_*
 | Description: Return codes, distinct from SGL's so callers need not include
 |   sega_bup.h.
 | Author: suinevere
 ----------------------*/
#define SAT_BUP_OK              0
#define SAT_BUP_ERR_NONE        1
#define SAT_BUP_ERR_UNFORMAT    2
#define SAT_BUP_ERR_PROTECTED   3
#define SAT_BUP_ERR_NO_SPACE    4
#define SAT_BUP_ERR_NOT_FOUND   5
#define SAT_BUP_ERR_BROKEN      6
#define SAT_BUP_ERR_EXISTS      7

/*----------------------
 | SatBupDev
 | Description: What sat_bup_probe found on one device.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int present;
    int formatted;
    int writeProtected;
    unsigned long freeBytes;
} SatBupDev;

/*----------------------
 | SatBupEntry
 | Description: What sat_bup_dir found for one filename.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int exists;
    unsigned long size;
    unsigned long date;
} SatBupEntry;

/*----------------------
 | sat_bup_init
 | Description: Brings up the BIOS backup library. Call once, after
 |   platform_init and before any other sat_bup_* call.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_bup_init(void);

/*----------------------
 | sat_bup_probe
 | Description: Reports whether a device is present, formatted, writable, and
 |   how much room it has left.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; out -- filled in
 | Returns: SAT_BUP_OK, or an error code with out zeroed
 ----------------------*/
int sat_bup_probe(unsigned long device, SatBupDev *out);

/*----------------------
 | sat_bup_dir
 | Description: Looks a save up by name without reading its contents.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- device id; name -- BUP filename; out -- filled in
 | Returns: SAT_BUP_OK whether or not the file exists; check out->exists. An
 |          error code means the lookup itself failed.
 ----------------------*/
int sat_bup_dir(unsigned long device, const char *name, SatBupEntry *out);

/*----------------------
 | sat_bup_read
 | Description: Reads a whole save into dst, refusing before the read if the
 |   stored file is larger than dst.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- device id; name -- BUP filename; dst -- destination;
 |         size -- capacity of dst
 | Returns: SAT_BUP_OK, SAT_BUP_ERR_NOT_FOUND, or SAT_BUP_ERR_BROKEN
 ----------------------*/
int sat_bup_read(unsigned long device, const char *name, void *dst, long size);

/*----------------------
 | sat_bup_write
 | Description: Writes a save, stamping it with the current RTC time.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- device id; name -- BUP filename; comment -- up to 10
 |         characters shown by the Saturn's Backup Manager; src -- the bytes;
 |         size -- how many; overwrite -- non-zero to replace
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_EXISTS / _NO_SPACE / _PROTECTED /
 |          _UNFORMAT
 ----------------------*/
int sat_bup_write(unsigned long device, const char *name, const char *comment,
                  const void *src, long size, int overwrite);

/*----------------------
 | sat_bup_delete
 | Description: Removes a save.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: device -- device id; name -- BUP filename
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_NOT_FOUND
 ----------------------*/
int sat_bup_delete(unsigned long device, const char *name);

/*----------------------
 | sat_bup_date_now
 | Description: The current RTC time as a BUP date word.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: the packed word
 ----------------------*/
unsigned long sat_bup_date_now(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_BACKUP_H */
