#include "bup_devmap.h"

void bupDevmapResolve(const int *present, int count,
                      int *internalIdx, int *cartIdx)
{
    int first = BUP_DEVMAP_NONE;
    int second = BUP_DEVMAP_NONE;
    int i;

    for (i = 0; i < count; ++i) {
        if (!present[i]) {
            continue;
        }
        if (first == BUP_DEVMAP_NONE) {
            first = i;
        } else if (second == BUP_DEVMAP_NONE) {
            second = i;
        }
    }

    *internalIdx = (first != BUP_DEVMAP_NONE) ? first : 0;
    *cartIdx = second;
}
