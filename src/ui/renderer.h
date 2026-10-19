#ifndef UI_RENDERER_H
#define UI_RENDERER_H
#include <stdint.h>

int  ui_init(void);
void ui_frame_begin(void);
void ui_frame_end(void);
void ui_rect(int x, int y, int w, int h, uint32_t argb);
void ui_rrect(int x, int y, int w, int h, int radius, uint32_t argb);
int  ui_text(int x, int y, const char *utf8, uint32_t argb, int px);
int  ui_text_width(const char *utf8, int px);
void ui_set_clip(int x, int y, int w, int h);
void ui_clear_clip(void);
int  ui_img(const char *key, int x, int y, int w, int h);
void ui_pump(void);

#endif
