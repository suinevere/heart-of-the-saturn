#ifndef SATURN_BOOTART_H
#define SATURN_BOOTART_H

#ifdef __cplusplus
extern "C" {
#endif

int boot_art_load(void);

void boot_art_draw(int highlight);

void boot_art_present(void);

void boot_art_release(void);

void boot_art_fade(int level);

int boot_art_title_texture(void);

int boot_art_title_row_texture(int row, int lit);

#ifdef __cplusplus
}
#endif

#endif
