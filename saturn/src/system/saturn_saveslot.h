/*----------------------
 | saturn_saveslot.h
 | Description: The glue between the engine's quicksave/quickload and
 |   savegame.h's slot I/O: owns the two LWRAM staging buffers, appends the
 |   CD-DA trailer on the way out, and re-issues the track on the way back.
 |   The only file that calls quicksave() and quickload().
 | Author: suinevere
 | Dependencies: savedata.h
 ----------------------*/
#ifndef SATURN_SAVESLOT_H
#define SATURN_SAVESLOT_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | saturn_saveslot_init
 | Description: Allocates the two LWRAM staging buffers and binds the stdio
 |   stream to the payload one. Call once, after platform_init.
 | Author: suinevere
 | Dependencies: saturn_compat.h
 | Globals: N/A
 | Params: N/A
 | Returns: 1 on success, 0 if either allocation failed, in which case saving
 |          and loading both refuse rather than crashing
 ----------------------*/
int saturn_saveslot_init(void);

/*----------------------
 | saturn_saveslot_save
 | Description: Serialises the running game into a slot. Must be called at the
 |   top of a frame, outside the task loop -- quicksave() asserts no active
 |   thread by toggling the aux bank to zero.
 | Author: suinevere
 | Dependencies: savegame.h, disc.h
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0-based index
 | Returns: SAT_BUP_OK, or a SAT_BUP_ERR_* / SAVE_ERR_* code
 ----------------------*/
int saturn_saveslot_save(unsigned long device, int slot);

/*----------------------
 | saturn_saveslot_load
 | Description: Restores a slot into the running game and restarts its music.
 |   Same frame-boundary requirement as saturn_saveslot_save, and additionally
 |   reads from the disc, so the drive must be idle.
 | Author: suinevere
 | Dependencies: savegame.h, disc.h
 | Globals: N/A
 | Params: device -- device id; slot -- 0-based index
 | Returns: SAT_BUP_OK, or a SAT_BUP_ERR_* / SAVE_ERR_* code, with the running
 |          game untouched on every failure
 ----------------------*/
int saturn_saveslot_load(unsigned long device, int slot);

/*----------------------
 | saturn_saveslot_last_error
 | Description: The code from the most recent save or load.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: a SAT_BUP_* or SAVE_ERR_* code
 ----------------------*/
int saturn_saveslot_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_SAVESLOT_H */
