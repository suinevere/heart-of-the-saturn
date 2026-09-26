#ifndef SATURN_PROGRESS_H
#define SATURN_PROGRESS_H

#ifdef __cplusplus
extern "C" {
#endif

unsigned long saturn_progress_load(void);

int saturn_progress_save(unsigned long mask);

#ifdef __cplusplus
}
#endif

#endif
