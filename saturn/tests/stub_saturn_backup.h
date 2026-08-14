/*----------------------
 | stub_saturn_backup.h
 | Description: Test-only controls for the host stand-in of the backup RAM
 |   layer. Not built into the Saturn binary.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#ifndef STUB_SATURN_BACKUP_H
#define STUB_SATURN_BACKUP_H

#include "saturn_backup.h"

void stub_bup_reset(void);
void stub_bup_place(unsigned long device, const char *name,
                    const unsigned char *data, int len);
int  stub_bup_fetch(unsigned long device, const char *name,
                    unsigned char *out, int cap);
void stub_bup_fail_read(int code);
void stub_bup_fail_write(int code);
void stub_bup_set_device(unsigned long device, int present, int formatted,
                         int writeProtected, unsigned long freeBytes);

#endif /* STUB_SATURN_BACKUP_H */
