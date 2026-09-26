#ifndef BUP_DEVMAP_H
#define BUP_DEVMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#define BUP_DEVMAP_NONE (-1)

void bupDevmapResolve(const int *present, int count,
                      int *internalIdx, int *cartIdx);

#ifdef __cplusplus
}
#endif

#endif
