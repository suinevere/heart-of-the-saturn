#ifndef VIDEO_H
#define VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

int  video_init(void);

int  video_create_surface(void);

void video_render(char *src);

void video_set_palette(int which);

void video_set_palette_rgb12(unsigned char *rgb12);

void video_set_fade(int level);

int  video_get_fade(void);

int  video_get_current_palette(void);

void video_set_scroll(int scroll);

int  video_get_scroll_register(void);

void video_toggle_fullscreen(void);

#ifdef __cplusplus
}
#endif

#endif
