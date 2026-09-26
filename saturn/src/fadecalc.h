#ifndef FADECALC_H
#define FADECALC_H

#ifdef __cplusplus
extern "C" {
#endif

#define FADECALC_LEVEL_NORMAL 255

#define FADECALC_SEGA_CD_STEPS 8

int fadecalc_scale(int value, int level);

int fadecalc_step_level(int step, int steps);

#ifdef __cplusplus
}
#endif

#endif
