#ifndef SATURN_SAVESLOT_H
#define SATURN_SAVESLOT_H

#ifdef __cplusplus
extern "C" {
#endif

int saturn_saveslot_init(void);

int saturn_saveslot_save(unsigned long device, int slot);

int saturn_saveslot_load(unsigned long device, int slot);

int saturn_saveslot_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
