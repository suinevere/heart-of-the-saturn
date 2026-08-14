/*----------------------
 | savedata.h
 | Description: Save slot metadata between the engine and saturn_backup.h's
 |   raw BUP wrapper: slot naming, header packing, slot probing, device
 |   defaulting and BUP date arithmetic. Must not include srl.hpp or
 |   sega_bup.h, so it stays safe to include from any translation unit and
 |   buildable by run_tests.sh.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#ifndef SAVEDATA_H
#define SAVEDATA_H

#include "saturn_backup.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAVE_NUM_SLOTS / SAVE_HEADER_SIZE / SAVE_FORMAT_VERSION
 | Description: How many slots a device holds, the packed header size, and the
 |   format version a save must carry to be loadable.
 | Author: suinevere
 ----------------------*/
#define SAVE_NUM_SLOTS      3
#define SAVE_HEADER_SIZE    48
#define SAVE_FORMAT_VERSION 1

/*----------------------
 | SAVE_UPSTREAM_BYTES
 | Description: What quicksave() writes, and therefore what quickload() must
 |   be given back: 3 bytes of room, backdrop and palette, 512 of main
 |   variables, 4096 of aux task banks, 512 of task program counters and
 |   enables, and 1027 of sprite records. Fixed by the upstream format, not by
 |   this port -- if it ever disagrees with main.c, quickload will read
 |   garbage rather than fail, so test_savegame asserts it.
 | Author: suinevere
 ----------------------*/
#define SAVE_UPSTREAM_BYTES 6150

/*----------------------
 | SAVE_TRAILER_BYTES
 | Description: The Saturn-only tail: CD-DA track index as a big-endian
 |   signed 16-bit value with -1 for none, the loop flag, and one reserved
 |   byte.
 | Author: suinevere
 ----------------------*/
#define SAVE_TRAILER_BYTES 4

/*----------------------
 | SAVE_PAYLOAD_MAX / SAVE_MAX_BYTES
 | Description: The uncompressed payload, and a whole save at its worst case.
 |   SAVE_MAX_BYTES is the size of both staging buffers, the capacity checked
 |   before a read, and the BUP_Stat datasize -- defined here once and nowhere
 |   else.
 | Author: suinevere
 ----------------------*/
#define SAVE_PAYLOAD_MAX (SAVE_UPSTREAM_BYTES + SAVE_TRAILER_BYTES)
#define SAVE_MAX_BYTES   (SAVE_HEADER_SIZE + SAVE_PAYLOAD_MAX)

/*----------------------
 | SAVE_FLAG_RLE
 | Description: Set in the header when the payload is run-length encoded.
 | Author: suinevere
 ----------------------*/
#define SAVE_FLAG_RLE 0x01

/*----------------------
 | SlotState
 | Description: What savedata_probe found in one backup RAM slot.
 | Author: suinevere
 ----------------------*/
typedef enum {
    SLOT_EMPTY,
    SLOT_OK,
    SLOT_DAMAGED,
    SLOT_OLD_VERSION
} SlotState;

/*----------------------
 | SlotInfo
 | Description: What a slot list row needs to show for one slot.
 | Author: suinevere
 ----------------------*/
typedef struct {
    SlotState state;
    unsigned short roomId;
    unsigned long date;
} SlotInfo;

/*----------------------
 | savedata_slot_name
 | Description: Builds the BUP filename for a slot index.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: slot -- 0-based slot index; out -- destination, must hold 12 bytes
 | Returns: N/A
 ----------------------*/
void savedata_slot_name(int slot, char *out);

/*----------------------
 | savedata_write_header
 | Description: Packs a SAVE_HEADER_SIZE-byte header in place, big-endian.
 |   Layout: 0 magic 'HOTA', 4 version, 6 flags, 7 reserved, 8 payload length,
 |   10 room id, 12 date, 16 description[32].
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- destination, must hold SAVE_HEADER_SIZE bytes; roomId -- the
 |         room the save was made in; date -- packed BUP date; flags --
 |         SAVE_FLAG_* bits; payloadLen -- stored payload length in bytes
 | Returns: N/A
 ----------------------*/
void savedata_write_header(unsigned char *buf, unsigned short roomId,
                           unsigned long date, unsigned char flags,
                           unsigned short payloadLen);

/*----------------------
 | savedata_read_header
 | Description: Unpacks a header, checking the magic before writing any
 |   output.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- source, at least SAVE_HEADER_SIZE bytes; ver, roomId, date,
 |         flags, payloadLen -- outputs, all left untouched on failure
 | Returns: 0 on magic mismatch, 1 otherwise
 ----------------------*/
int savedata_read_header(const unsigned char *buf, unsigned short *ver,
                         unsigned short *roomId, unsigned long *date,
                         unsigned char *flags, unsigned short *payloadLen);

/*----------------------
 | savedata_probe
 | Description: Reads a whole slot into the caller's scratch buffer and
 |   classifies it. The buffer is the caller's because the only one this port
 |   can afford lives in LWRAM; owning a SAVE_MAX_BYTES static here would put
 |   6 KB in HWRAM BSS.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0-based index;
 |         out -- filled in; scratch -- working buffer; scratchCap -- its
 |         capacity, which must be at least SAVE_MAX_BYTES
 | Returns: the same state written to out->state
 ----------------------*/
SlotState savedata_probe(unsigned long device, int slot, SlotInfo *out,
                         unsigned char *scratch, int scratchCap);

/*----------------------
 | savedata_pick_default_device
 | Description: Chooses which backup device the save menus open on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: internal, cart -- probe results; internalHasSaves, cartHasSaves --
 |         non-zero if that device already holds a HOTASAVE* file
 | Returns: SAT_BUP_CART only when the cart is present, formatted and holds
 |          saves while internal does not; SAT_BUP_INTERNAL otherwise
 ----------------------*/
unsigned long savedata_pick_default_device(const SatBupDev *internal,
                                           const SatBupDev *cart,
                                           int internalHasSaves,
                                           int cartHasSaves);

/*----------------------
 | savedata_date_split
 | Description: Unpacks a BUP date word, which counts minutes from 1 January
 |   1980, into the fields a slot row shows. Pure arithmetic, which is why it
 |   lives here rather than in saturn_backup.cxx -- here the host tests reach
 |   the shipping copy instead of a duplicate.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: date -- packed word; month, day, hour, min -- outputs, any may be
 |         NULL
 | Returns: N/A
 ----------------------*/
void savedata_date_split(unsigned long date, int *month, int *day, int *hour,
                         int *min);

#ifdef __cplusplus
}
#endif

#endif /* SAVEDATA_H */
