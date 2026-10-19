#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb_truetype.h"

#include "font.h"
#include <stdlib.h>
#include <string.h>

static stbtt_fontinfo g_text;
static stbtt_fontinfo g_emoji;
static int g_has_text;
static int g_has_emoji;

static int emoji_cp(uint32_t cp){
    return (cp >= 0x2600 && cp <= 0x27BF) ||
           (cp >= 0x2B00 && cp <= 0x2BFF) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0x1F000 && cp <= 0x1FAFF);
}

int font_init(const unsigned char *ttf_text, size_t text_len,
              const unsigned char *ttf_emoji, size_t emoji_len){
    g_has_text = ttf_text && text_len &&
        stbtt_InitFont(&g_text, ttf_text, stbtt_GetFontOffsetForIndex(ttf_text, 0));
    g_has_emoji = ttf_emoji && emoji_len &&
        stbtt_InitFont(&g_emoji, ttf_emoji, stbtt_GetFontOffsetForIndex(ttf_emoji, 0));
    return g_has_text;
}

uint32_t font_utf8_next(const char **p){
    const unsigned char *s = (const unsigned char *)(p ? *p : NULL);
    uint32_t cp;
    int n, i;
    if(!s || !s[0]) return 0;
    if(s[0] < 0x80){
        *p += 1;
        return s[0];
    }
    if((s[0] & 0xE0) == 0xC0){ cp = s[0] & 0x1Fu; n = 1; }
    else if((s[0] & 0xF0) == 0xE0){ cp = s[0] & 0x0Fu; n = 2; }
    else if((s[0] & 0xF8) == 0xF0){ cp = s[0] & 0x07u; n = 3; }
    else { *p += 1; return 0xFFFD; }
    for(i = 0; i < n; i++)
        if((s[1 + i] & 0xC0) != 0x80){ *p += 1; return 0xFFFD; }
    for(i = 0; i < n; i++) cp = (cp << 6) | (s[1 + i] & 0x3Fu);
    if((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ||
       (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF){
        *p += 1;
        return 0xFFFD;
    }
    *p += n + 1;
    return cp;
}

static stbtt_fontinfo *pick_font(uint32_t cp){
    stbtt_fontinfo *first = g_has_text ? &g_text : (g_has_emoji ? &g_emoji : NULL);
    stbtt_fontinfo *second = g_has_text ? &g_text : (g_has_emoji ? &g_emoji : NULL);
    if(!first) return NULL;
    if(emoji_cp(cp) && g_has_emoji){ first = &g_emoji; second = &g_text; }
    if(stbtt_FindGlyphIndex(first, (int)cp)) return first;
    if(second && second != first && stbtt_FindGlyphIndex(second, (int)cp)) return second;
    return NULL;
}

static int box_glyph(font_glyph *out, int px){
    int bh = px - px / 4, bw = px / 2, border, x, y;
    if(bh < 1) bh = 1;
    if(bw < 1) bw = 1;
    out->pixels = calloc((size_t)bw * (size_t)bh, 1);
    if(!out->pixels) return 0;
    out->w = bw;
    out->h = bh;
    out->xoff = 1;
    out->yoff = -bh;
    out->advance = bw + 2;
    border = px >= 20 ? 2 : 1;
    for(y = 0; y < bh; y++)
        for(x = 0; x < bw; x++)
            if(x < border || y < border || x >= bw - border || y >= bh - border)
                out->pixels[y * bw + x] = 255;
    return 1;
}

int font_glyph_get(font_glyph *out, uint32_t cp, int px){
    stbtt_fontinfo *f;
    float sc;
    int adv = 0, lsb = 0, w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char *bm;
    if(!out || px < 1) return 0;
    memset(out, 0, sizeof *out);
    f = pick_font(cp);
    if(!f) return box_glyph(out, px);
    sc = stbtt_ScaleForPixelHeight(f, (float)px);
    stbtt_GetCodepointHMetrics(f, (int)cp, &adv, &lsb);
    bm = stbtt_GetCodepointBitmapSubpixel(f, sc, sc, 0, 0, (int)cp, &w, &h, &xoff, &yoff);
    out->advance = (int)((float)adv * sc + 0.5f);
    if(w > 0 && h > 0 && bm){
        out->pixels = bm;
        out->w = w;
        out->h = h;
        out->xoff = xoff;
        out->yoff = yoff;
    } else {
        stbtt_FreeBitmap(bm, NULL);
    }
    return 1;
}

int font_glyph_advance(uint32_t cp, int px){
    stbtt_fontinfo *f;
    int adv = 0, lsb = 0;
    if(px < 1) return 0;
    f = pick_font(cp);
    if(!f) return px / 2 + 2;
    stbtt_GetCodepointHMetrics(f, (int)cp, &adv, &lsb);
    return (int)((float)adv * stbtt_ScaleForPixelHeight(f, (float)px) + 0.5f);
}

void font_glyph_free(font_glyph *g){
    if(!g) return;
    free(g->pixels);
    g->pixels = NULL;
    g->w = g->h = 0;
}

int font_ascent(int px){
    int a = 0, d = 0, gap = 0;
    if(px < 1) return 0;
    if(!g_has_text) return px * 3 / 4;
    stbtt_GetFontVMetrics(&g_text, &a, &d, &gap);
    return (int)((float)a * stbtt_ScaleForPixelHeight(&g_text, (float)px) + 0.5f);
}

int font_line_height(int px){
    int a = 0, d = 0, gap = 0;
    if(px < 1) return 0;
    if(!g_has_text) return px;
    stbtt_GetFontVMetrics(&g_text, &a, &d, &gap);
    return (int)((float)(a - d + gap) * stbtt_ScaleForPixelHeight(&g_text, (float)px) + 0.5f);
}
