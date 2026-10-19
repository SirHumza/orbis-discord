#ifndef UI_FONT_H
#define UI_FONT_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    int xoff, yoff;
    int advance;
    unsigned char *pixels;
} font_glyph;

int font_init(const unsigned char *ttf_text, size_t text_len,
              const unsigned char *ttf_emoji, size_t emoji_len);
uint32_t font_utf8_next(const char **p);
int font_glyph_get(font_glyph *out, uint32_t cp, int px);
int font_glyph_advance(uint32_t cp, int px);
void font_glyph_free(font_glyph *g);
int font_ascent(int px);
int font_line_height(int px);

#endif
