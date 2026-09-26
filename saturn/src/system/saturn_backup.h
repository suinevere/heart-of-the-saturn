#ifndef SATURN_BACKUP_H
#define SATURN_BACKUP_H

#ifdef __cplusplus
extern "C" {
#endif

#define SAT_BUP_INTERNAL 1
#define SAT_BUP_CART     2

#define SAT_BUP_OK              0
#define SAT_BUP_ERR_NONE        1
#define SAT_BUP_ERR_UNFORMAT    2
#define SAT_BUP_ERR_PROTECTED   3
#define SAT_BUP_ERR_NO_SPACE    4
#define SAT_BUP_ERR_NOT_FOUND   5
#define SAT_BUP_ERR_BROKEN      6
#define SAT_BUP_ERR_EXISTS      7

typedef struct {
    int present;
    int formatted;
    int writeProtected;
    unsigned long freeBytes;
} SatBupDev;

typedef struct {
    int exists;
    unsigned long size;
    unsigned long date;
} SatBupEntry;

void sat_bup_init(void);

int sat_bup_probe(unsigned long device, SatBupDev *out);

int sat_bup_dir(unsigned long device, const char *name, SatBupEntry *out);

int sat_bup_read(unsigned long device, const char *name, void *dst, long size);

int sat_bup_write(unsigned long device, const char *name, const char *comment,
                  const void *src, long size, int overwrite);

int sat_bup_delete(unsigned long device, const char *name);

unsigned long sat_bup_date_now(void);

#ifdef __cplusplus
}
#endif

#endif
