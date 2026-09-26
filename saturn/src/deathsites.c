#include "deathsites.h"

typedef struct {
    signed char room;
    long        pc;
} DeathSite;

static const DeathSite DEATH_SITES[DEATH_SITE_COUNT] = {
    { 1, 0x25abL },
    { 1, 0x25adL },
    { 2, 0x1abeL },
    { 2, 0x780bL },
    { 3, 0x1c98L },
    { 3, 0x1c9aL },
    { 3, 0x2708L },
    { 3, 0x30eeL },
    { 3, 0x3b9aL },
    { 4, 0x282cL },
    { 4, 0x3262L },
    { 4, 0x4012L },
    { 5, 0x09deL },
    { 5, 0x2a8aL },
    { 5, 0x680cL },
    { 5, 0xfe27L },
    { 6, 0x10f6L },
    { 8, 0x005cL },
    { 8, 0x1faaL }
};

int death_site_is_real(int room, int pc)
{
    int i;

    for (i = 0; i < DEATH_SITE_COUNT; i++) {
        if (DEATH_SITES[i].room == room && DEATH_SITES[i].pc == (long)pc) {
            return 1;
        }
    }
    return 0;
}

int death_site_room(int i)
{
    if (i < 0 || i >= DEATH_SITE_COUNT) {
        return 0;
    }
    return DEATH_SITES[i].room;
}

int death_site_pc(int i)
{
    if (i < 0 || i >= DEATH_SITE_COUNT) {
        return -1;
    }
    return (int)DEATH_SITES[i].pc;
}
