#ifndef SAVEDATA_H
#define SAVEDATA_H

#include "saturn_backup.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_NUM_SLOTS      1
#define SAVE_HEADER_SIZE    48
#define SAVE_FORMAT_VERSION 1

#define SAVE_UPSTREAM_BYTES 6150

#define SAVE_TRAILER_BYTES 4

#define SAVE_PAYLOAD_MAX (SAVE_UPSTREAM_BYTES + SAVE_TRAILER_BYTES)
#define SAVE_MAX_BYTES   (SAVE_HEADER_SIZE + SAVE_PAYLOAD_MAX)

#define SAVE_FLAG_RLE        0x01
#define SAVE_FLAG_CHECKPOINT 0x02

typedef enum {
    SLOT_EMPTY,
    SLOT_OK,
    SLOT_DAMAGED,
    SLOT_OLD_VERSION
} SlotState;

typedef struct {
    SlotState state;
    unsigned short roomId;
    unsigned long date;
    unsigned char flags;
    unsigned char entry;
} SlotInfo;

void savedata_slot_name(int slot, char *out);

void savedata_write_header(unsigned char *buf, unsigned short roomId,
                           unsigned char entry, unsigned long date,
                           unsigned char flags, unsigned short payloadLen);

int savedata_read_header(const unsigned char *buf, unsigned short *ver,
                         unsigned short *roomId, unsigned char *entry,
                         unsigned long *date, unsigned char *flags,
                         unsigned short *payloadLen);

SlotState savedata_probe(unsigned long device, int slot, SlotInfo *out,
                         unsigned char *scratch, int scratchCap);

unsigned long savedata_pick_default_device(const SatBupDev *internal,
                                           const SatBupDev *cart,
                                           int internalHasSaves,
                                           int cartHasSaves);

void savedata_date_split(unsigned long date, int *month, int *day, int *hour,
                         int *min);

#ifdef __cplusplus
}
#endif

#endif
