#ifndef SATURN_BOOTMARK_H
#define SATURN_BOOTMARK_H

#ifndef BOOTMARK_DIAG
#define BOOTMARK_DIAG 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BOOTMARK_PLATFORM   1
#define BOOTMARK_DISC       2
#define BOOTMARK_INIT       3
#define BOOTMARK_BOOTART    4
#define BOOTMARK_FIRSTFRAME 5

#if BOOTMARK_DIAG && defined(HOTA_SATURN)
void boot_mark(int step);
#else
#define boot_mark(step) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
