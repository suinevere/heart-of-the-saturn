#ifndef DEATHSITES_H
#define DEATHSITES_H

#ifdef __cplusplus
extern "C" {
#endif

#define DEATH_SITE_COUNT 19

int death_site_is_real(int room, int pc);

int death_site_room(int i);

int death_site_pc(int i);

#ifdef __cplusplus
}
#endif

#endif
