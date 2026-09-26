#ifndef BOOTMENU_H
#define BOOTMENU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BOOT_ENTRY_OUT_OF_THIS_WORLD  = 0,
    BOOT_ENTRY_HEART_OF_THE_ALIEN = 1
} boot_entry;

#define BOOT_KEY_UP      0x01u
#define BOOT_KEY_DOWN    0x02u
#define BOOT_KEY_LEFT    0x04u
#define BOOT_KEY_RIGHT   0x08u
#define BOOT_KEY_A       0x10u
#define BOOT_KEY_B       0x20u
#define BOOT_KEY_C       0x40u
#define BOOT_KEY_SELECT  0x80u
#define BOOT_KEY_MOVE    (BOOT_KEY_UP | BOOT_KEY_DOWN | BOOT_KEY_LEFT | BOOT_KEY_RIGHT)
#define BOOT_KEY_CONFIRM (BOOT_KEY_A | BOOT_KEY_B | BOOT_KEY_C)

#define BOOT_FADE_MS        1000u
#define BOOT_MUSIC_LOOP_MS 40000u

#define BOOT_VOLUME_MAX 7u

#define BOOT_MUSIC_INDEX 16

typedef struct
{
    uint32_t music_start_ms;
    int      highlight;
    int      music_started;
    int      part1_available;
    int      part2_available;
} bootmenu_state;

typedef struct
{
    boot_entry  highlight;
    uint8_t     music_volume;
    int         music_restart;
    int         start_game;
    int         start_part1;
} boot_frame;

void bootmenu_init(bootmenu_state *st, uint32_t now_ms,
                   int part1_available, int part2_available);

void bootmenu_step(bootmenu_state *st, uint32_t now_ms, uint32_t pressed,
                   boot_frame *out);

#ifdef __cplusplus
}
#endif

#endif
