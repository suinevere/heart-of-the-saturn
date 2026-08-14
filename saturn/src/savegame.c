/*----------------------
 | savegame.c
 | Description: The assembly and slot I/O described by savegame.h.
 | Author: suinevere
 | Dependencies: savegame.h, savedata.h, saverle.h, saturn_backup.h, string.h
 ----------------------*/
#include <string.h>
#include "savegame.h"
#include "saverle.h"

/*----------------------
 | SAVE_COMMENT
 | Description: The ten characters the Saturn's Backup Manager shows beside
 |   the file.
 | Author: suinevere
 ----------------------*/
#define SAVE_COMMENT "HEARTALIEN"

void savegame_pack_trailer(unsigned char *out, int track, int loop)
{
    out[0] = (unsigned char)((track >> 8) & 0xFF);
    out[1] = (unsigned char)(track & 0xFF);
    out[2] = (unsigned char)(loop ? 1 : 0);
    out[3] = 0;
}

void savegame_unpack_trailer(const unsigned char *in, int *track, int *loop)
{
    int v = (int)((in[0] << 8) | in[1]);
    if (v & 0x8000) {
        v -= 0x10000;
    }
    if (track) *track = v;
    if (loop)  *loop = (in[2] != 0);
}

int savegame_write(unsigned long device, int slot,
                   const unsigned char *payload, int payloadLen,
                   unsigned short roomId,
                   unsigned char *work, int workCap)
{
    char name[12];
    int encoded;
    int storedLen;
    unsigned char flags;

    if (slot < 0 || slot >= SAVE_NUM_SLOTS) {
        return SAVE_ERR_BAD_SLOT;
    }
    if (payloadLen <= 0 || payloadLen > SAVE_PAYLOAD_MAX ||
        workCap < SAVE_MAX_BYTES) {
        return SAVE_ERR_TOO_LARGE;
    }

    encoded = saverle_encode(payload, payloadLen, work + SAVE_HEADER_SIZE,
                             workCap - SAVE_HEADER_SIZE);
    if (encoded > 0) {
        flags = SAVE_FLAG_RLE;
        storedLen = encoded;
    } else {
        flags = 0;
        storedLen = payloadLen;
        memcpy(work + SAVE_HEADER_SIZE, payload, (size_t)payloadLen);
    }

    savedata_write_header(work, roomId, sat_bup_date_now(), flags,
                          (unsigned short)storedLen);
    savedata_slot_name(slot, name);
    return sat_bup_write(device, name, SAVE_COMMENT, work,
                         (long)(SAVE_HEADER_SIZE + storedLen), 1);
}

int savegame_read(unsigned long device, int slot,
                  unsigned char *payload, int payloadCap, int *payloadLen,
                  unsigned short *roomId,
                  unsigned char *work, int workCap)
{
    char name[12];
    SatBupEntry entry;
    unsigned short ver = 0;
    unsigned short room = 0;
    unsigned short storedLen = 0;
    unsigned char flags = 0;
    unsigned long date = 0;
    int rc;
    int fileLen;

    if (slot < 0 || slot >= SAVE_NUM_SLOTS) {
        return SAVE_ERR_BAD_SLOT;
    }
    if (workCap < SAVE_MAX_BYTES || payloadCap < SAVE_PAYLOAD_MAX) {
        return SAVE_ERR_TOO_LARGE;
    }

    savedata_slot_name(slot, name);
    rc = sat_bup_dir(device, name, &entry);
    if (rc != SAT_BUP_OK) {
        return rc;
    }
    if (!entry.exists) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    fileLen = (int)entry.size;

    rc = sat_bup_read(device, name, work, (long)workCap);
    if (rc != SAT_BUP_OK) {
        return rc;
    }
    if (!savedata_read_header(work, &ver, &room, &date, &flags, &storedLen)) {
        return SAVE_ERR_BAD_MAGIC;
    }
    if (ver != SAVE_FORMAT_VERSION) {
        return SAVE_ERR_BAD_VERSION;
    }
    if (storedLen <= 0 || SAVE_HEADER_SIZE + (int)storedLen > fileLen) {
        return SAVE_ERR_BAD_PAYLOAD;
    }

    if (flags & SAVE_FLAG_RLE) {
        int decoded = saverle_decode(work + SAVE_HEADER_SIZE, (int)storedLen,
                                     payload, payloadCap);
        if (decoded <= 0) {
            return SAVE_ERR_BAD_PAYLOAD;
        }
        *payloadLen = decoded;
    } else {
        if ((int)storedLen > payloadCap) {
            return SAVE_ERR_BAD_PAYLOAD;
        }
        memcpy(payload, work + SAVE_HEADER_SIZE, (size_t)storedLen);
        *payloadLen = (int)storedLen;
    }

    *roomId = room;
    return SAT_BUP_OK;
}
