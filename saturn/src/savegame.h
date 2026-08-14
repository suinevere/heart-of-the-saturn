/*----------------------
 | savegame.h
 | Description: Assembles a save from a header, a payload and a Saturn
 |   trailer, chooses whether to compress it, and moves it to and from a
 |   backup RAM slot.
 |
 |   Deliberately does not call quicksave(): it is handed a payload and gives
 |   one back, so run_tests.sh can link it without main.c. saturn_saveslot.cxx
 |   supplies the payload on Saturn.
 | Author: suinevere
 | Dependencies: savedata.h
 ----------------------*/
#ifndef SAVEGAME_H
#define SAVEGAME_H

#include "savedata.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAVE_ERR_*
 | Description: Failures this layer detects itself, numbered clear of the
 |   SAT_BUP_* codes so one return value can carry either.
 | Author: suinevere
 ----------------------*/
#define SAVE_ERR_BAD_MAGIC   32
#define SAVE_ERR_BAD_VERSION 33
#define SAVE_ERR_BAD_PAYLOAD 34
#define SAVE_ERR_TOO_LARGE   35

/*----------------------
 | savegame_pack_trailer
 | Description: Writes the Saturn trailer: track index big-endian with -1 for
 |   none, loop flag, one reserved byte.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: out -- destination, must hold SAVE_TRAILER_BYTES; track -- engine
 |         music index or -1; loop -- non-zero if looping
 | Returns: N/A
 ----------------------*/
void savegame_pack_trailer(unsigned char *out, int track, int loop);

/*----------------------
 | savegame_unpack_trailer
 | Description: Reads a trailer back.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: in -- source, at least SAVE_TRAILER_BYTES; track, loop -- outputs
 | Returns: N/A
 ----------------------*/
void savegame_unpack_trailer(const unsigned char *in, int *track, int *loop);

/*----------------------
 | savegame_write
 | Description: Stamps a header, compresses the payload if that makes it
 |   smaller, and writes the result to a slot, replacing whatever was there.
 |   A failure leaves the slot's previous contents alone.
 | Author: suinevere
 | Dependencies: savedata.h, saverle.h, saturn_backup.h
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0-based index;
 |         payload -- upstream bytes followed by the trailer; payloadLen --
 |         its length, at most SAVE_PAYLOAD_MAX; roomId -- the room the save
 |         was made in; work -- staging buffer; workCap -- its capacity, which
 |         must be at least SAVE_MAX_BYTES
 | Returns: SAT_BUP_OK, a SAT_BUP_ERR_* code, or SAVE_ERR_TOO_LARGE
 ----------------------*/
int savegame_write(unsigned long device, int slot,
                   const unsigned char *payload, int payloadLen,
                   unsigned short roomId,
                   unsigned char *work, int workCap);

/*----------------------
 | savegame_read
 | Description: Reads a slot, validates it, and only then writes anything into
 |   payload. Every refusal leaves payload exactly as the caller left it, which
 |   is what makes a corrupt slot survivable mid-game.
 | Author: suinevere
 | Dependencies: savedata.h, saverle.h, saturn_backup.h
 | Globals: N/A
 | Params: device -- device id; slot -- 0-based index; payload -- destination;
 |         payloadCap -- its capacity; payloadLen -- receives the length;
 |         roomId -- receives the room; work -- staging buffer; workCap -- its
 |         capacity, at least SAVE_MAX_BYTES
 | Returns: SAT_BUP_OK, a SAT_BUP_ERR_* code, or SAVE_ERR_BAD_MAGIC /
 |          _BAD_VERSION / _BAD_PAYLOAD / _TOO_LARGE
 ----------------------*/
int savegame_read(unsigned long device, int slot,
                  unsigned char *payload, int payloadCap, int *payloadLen,
                  unsigned short *roomId,
                  unsigned char *work, int workCap);

#ifdef __cplusplus
}
#endif

#endif /* SAVEGAME_H */
