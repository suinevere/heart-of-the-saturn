#ifndef SAVEGAME_H
#define SAVEGAME_H

#include "savedata.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_ERR_BAD_MAGIC   32
#define SAVE_ERR_BAD_VERSION 33
#define SAVE_ERR_BAD_PAYLOAD 34
#define SAVE_ERR_TOO_LARGE   35
#define SAVE_ERR_NO_BUFFERS  36
#define SAVE_ERR_BAD_SLOT    37

void savegame_pack_trailer(unsigned char *out, int track, int loop);

void savegame_unpack_trailer(const unsigned char *in, int *track, int *loop);

int savegame_write(unsigned long device, int slot,
                   const unsigned char *payload, int payloadLen,
                   unsigned short roomId,
                   unsigned char *work, int workCap);

int savegame_read(unsigned long device, int slot,
                  unsigned char *payload, int payloadCap, int *payloadLen,
                  unsigned short *roomId,
                  unsigned char *work, int workCap);

int savegame_write_checkpoint(unsigned long device, int slot,
                              unsigned short roomId, unsigned char entry,
                              unsigned char *work, int workCap);

int savegame_read_checkpoint(unsigned long device, int slot,
                             unsigned short *roomId, unsigned char *entry,
                             unsigned char *work, int workCap);

#ifdef __cplusplus
}
#endif

#endif
