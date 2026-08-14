/*----------------------
 | saturn_saveslot.cxx
 | Description: The glue described by saturn_saveslot.h.
 | Author: suinevere
 | Dependencies: saturn_saveslot.h, savegame.h, savedata.h, saturn_compat.h,
 |   disc.h, main.h, string.h
 | Globals: s_payload, s_work, s_ready, s_lastError
 ----------------------*/
extern "C" {
#include <string.h>
#include "saturn_compat.h"
#include "saturn_saveslot.h"
#include "savegame.h"
#include "savedata.h"
#include "savebuf.h"
#include "disc.h"

extern int current_room;
void quicksave(void);
void quickload(void);
}

/*----------------------
 | s_payload / s_work
 | Description: The two staging buffers, in LWRAM rather than HWRAM BSS.
 |   Each is touched exactly twice per save -- filled, then handed on -- which
 |   is what saturn_compat.h means by a bulk blob. HWRAM has no room: measured
 |   .bss is 1,882,592 bytes against 770,048.
 | Author: suinevere
 ----------------------*/
static unsigned char *s_payload;
static unsigned char *s_work;

/*----------------------
 | s_ready / s_lastError
 | Description: Whether the buffers exist, and the outcome of the last call.
 | Author: suinevere
 ----------------------*/
static int s_ready;
static int s_lastError;

extern "C" int saturn_saveslot_init(void)
{
    s_payload = (unsigned char *)saturn_lwram_alloc(SAVE_MAX_BYTES);
    s_work = (unsigned char *)saturn_lwram_alloc(SAVE_MAX_BYTES);
    if (s_payload == (unsigned char *)0 || s_work == (unsigned char *)0) {
        if (s_payload != (unsigned char *)0) {
            saturn_lwram_free(s_payload);
            s_payload = (unsigned char *)0;
        }
        if (s_work != (unsigned char *)0) {
            saturn_lwram_free(s_work);
            s_work = (unsigned char *)0;
        }
        s_ready = 0;
        return 0;
    }
    saturn_savebuf_bind(s_payload, SAVE_MAX_BYTES);
    s_ready = 1;
    return 1;
}

extern "C" int saturn_saveslot_save(unsigned long device, int slot)
{
    savebuf *stream;
    int written;
    int track;
    int loop = 0;

    if (!s_ready) {
        s_lastError = SAVE_ERR_NO_BUFFERS;
        return s_lastError;
    }

    saturn_savebuf_reset();
    quicksave();

    stream = saturn_savebuf_stream();
    written = savebuf_len(stream);
    if (savebuf_error(stream) || written != SAVE_UPSTREAM_BYTES) {
        s_lastError = SAVE_ERR_BAD_PAYLOAD;
        return s_lastError;
    }

    track = disc_current_track(&loop);
    savegame_pack_trailer(s_payload + SAVE_UPSTREAM_BYTES, track, loop);

    s_lastError = savegame_write(device, slot, s_payload,
                                 SAVE_UPSTREAM_BYTES + SAVE_TRAILER_BYTES,
                                 (unsigned short)current_room,
                                 s_work, SAVE_MAX_BYTES);
    return s_lastError;
}

extern "C" int saturn_saveslot_load(unsigned long device, int slot)
{
    unsigned short roomId = 0;
    int payloadLen = 0;
    int track = -1;
    int loop = 0;
    int rc;

    if (!s_ready) {
        s_lastError = SAVE_ERR_NO_BUFFERS;
        return s_lastError;
    }

    rc = savegame_read(device, slot, s_payload, SAVE_MAX_BYTES, &payloadLen,
                       &roomId, s_work, SAVE_MAX_BYTES);
    if (rc != SAT_BUP_OK) {
        s_lastError = rc;
        return s_lastError;
    }
    if (payloadLen != SAVE_UPSTREAM_BYTES + SAVE_TRAILER_BYTES) {
        s_lastError = SAVE_ERR_BAD_PAYLOAD;
        return s_lastError;
    }

    savegame_unpack_trailer(s_payload + SAVE_UPSTREAM_BYTES, &track, &loop);

    saturn_savebuf_set_length(SAVE_UPSTREAM_BYTES);
    quickload();

    if (track >= 0) {
        disc_play_track(track, loop);
    } else {
        disc_stop_track();
    }

    s_lastError = SAT_BUP_OK;
    return s_lastError;
}

extern "C" int saturn_saveslot_last_error(void)
{
    return s_lastError;
}
