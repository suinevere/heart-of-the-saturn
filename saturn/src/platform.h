#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

int          platform_init(void);

void         platform_quit(void);

unsigned int platform_ticks(void);

void         platform_delay(unsigned int ms);

void         platform_frame(void);

#ifdef __cplusplus
}
#endif

#endif
