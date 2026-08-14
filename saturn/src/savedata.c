/*----------------------
 | savedata.c
 | Description: The slot metadata described by savedata.h.
 | Author: suinevere
 | Dependencies: savedata.h, string.h
 ----------------------*/
#include <string.h>
#include "savedata.h"

void savedata_slot_name(int slot, char *out)
{
    strcpy(out, "HOTASAVE");
    out[8] = (char)('1' + slot);
    out[9] = 0;
}

void savedata_write_header(unsigned char *buf, unsigned short roomId,
                           unsigned long date, unsigned char flags,
                           unsigned short payloadLen)
{
    buf[0] = 'H';
    buf[1] = 'O';
    buf[2] = 'T';
    buf[3] = 'A';
    buf[4] = (unsigned char)((SAVE_FORMAT_VERSION >> 8) & 0xFF);
    buf[5] = (unsigned char)(SAVE_FORMAT_VERSION & 0xFF);
    buf[6] = flags;
    buf[7] = 0;
    buf[8] = (unsigned char)((payloadLen >> 8) & 0xFF);
    buf[9] = (unsigned char)(payloadLen & 0xFF);
    buf[10] = (unsigned char)((roomId >> 8) & 0xFF);
    buf[11] = (unsigned char)(roomId & 0xFF);
    buf[12] = (unsigned char)((date >> 24) & 0xFF);
    buf[13] = (unsigned char)((date >> 16) & 0xFF);
    buf[14] = (unsigned char)((date >> 8) & 0xFF);
    buf[15] = (unsigned char)(date & 0xFF);
    memset(buf + 16, 0, SAVE_HEADER_SIZE - 16);
}

int savedata_read_header(const unsigned char *buf, unsigned short *ver,
                         unsigned short *roomId, unsigned long *date,
                         unsigned char *flags, unsigned short *payloadLen)
{
    if (buf[0] != 'H' || buf[1] != 'O' || buf[2] != 'T' || buf[3] != 'A') {
        return 0;
    }
    *ver = (unsigned short)((buf[4] << 8) | buf[5]);
    *flags = buf[6];
    *payloadLen = (unsigned short)((buf[8] << 8) | buf[9]);
    *roomId = (unsigned short)((buf[10] << 8) | buf[11]);
    *date = ((unsigned long)buf[12] << 24) | ((unsigned long)buf[13] << 16) |
            ((unsigned long)buf[14] << 8) | (unsigned long)buf[15];
    return 1;
}

SlotState savedata_probe(unsigned long device, int slot, SlotInfo *out,
                         unsigned char *scratch, int scratchCap)
{
    char name[12];
    SatBupEntry entry;
    unsigned short ver = 0;
    unsigned short roomId = 0;
    unsigned short payloadLen = 0;
    unsigned char flags = 0;
    unsigned long date = 0;

    savedata_slot_name(slot, name);
    out->state = SLOT_EMPTY;
    out->roomId = 0;
    out->date = 0;

    if (scratchCap < SAVE_MAX_BYTES) {
        out->state = SLOT_DAMAGED;
        return SLOT_DAMAGED;
    }
    if (sat_bup_dir(device, name, &entry) != SAT_BUP_OK || !entry.exists) {
        return SLOT_EMPTY;
    }
    if (sat_bup_read(device, name, scratch, (long)scratchCap) != SAT_BUP_OK) {
        out->state = SLOT_DAMAGED;
        return SLOT_DAMAGED;
    }
    if (!savedata_read_header(scratch, &ver, &roomId, &date, &flags,
                              &payloadLen)) {
        out->state = SLOT_DAMAGED;
        return SLOT_DAMAGED;
    }

    out->roomId = roomId;
    out->date = date;
    out->state = (ver < SAVE_FORMAT_VERSION) ? SLOT_OLD_VERSION : SLOT_OK;
    return out->state;
}

unsigned long savedata_pick_default_device(const SatBupDev *internal,
                                           const SatBupDev *cart,
                                           int internalHasSaves,
                                           int cartHasSaves)
{
    (void)internal;
    if (cart->present && cart->formatted && cartHasSaves && !internalHasSaves) {
        return SAT_BUP_CART;
    }
    return SAT_BUP_INTERNAL;
}

void savedata_date_split(unsigned long date, int *month, int *day, int *hour,
                         int *min)
{
    static const int len[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned long days = date / 1440UL;
    unsigned long rem = date % 1440UL;
    int year = 1980;
    int mo = 0;

    for (;;) {
        int inYear = ((year % 4) == 0) ? 366 : 365;
        if (days < (unsigned long)inYear) {
            break;
        }
        days -= (unsigned long)inYear;
        year++;
    }
    for (;;) {
        int inMonth = len[mo];
        if (mo == 1 && (year % 4) == 0) {
            inMonth = 29;
        }
        if (days < (unsigned long)inMonth) {
            break;
        }
        days -= (unsigned long)inMonth;
        mo++;
    }

    if (month) *month = mo + 1;
    if (day)   *day = (int)days + 1;
    if (hour)  *hour = (int)(rem / 60UL);
    if (min)   *min = (int)(rem % 60UL);
}
