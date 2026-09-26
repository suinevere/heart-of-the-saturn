#ifndef __SCREEN_INCLUDED__
#define __SCREEN_INCLUDED__

int screen_init();

int get_selected_screen();
char *get_selected_screen_ptr();

void select_screen(int which);
void update_screen(int which);
char *get_screen_ptr(int which);

void screen_arm_fade_restore(void);

void screen_arm_fade_in(void);

void screen_fade_cancel(void);

int screen_fade_in_active(void);

void copy_screen(int dest, int src);

void fill_screen(int dest, char color);

void fill_line(int count, int x, int y, int color);
void fill_line_reversed(int count, int x, int y, int color);

#endif
