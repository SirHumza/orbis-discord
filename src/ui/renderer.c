#include <SDL2/SDL.h>
#include <stdint.h>

#ifdef __PS4__
#pragma comment(lib, "SceVideoOut")
#pragma comment(lib, "SceAudioOut")
#pragma comment(lib, "ScePad")
#endif
#include <stdlib.h>
#include <string.h>

#include "renderer.h"
#include "font.h"
#include "../../assets/roboto_ttf.h"
#include "../../assets/notoemoji_ttf.h"

#if defined(__has_include)
#  if __has_include("../image.h")
#    include "../image.h"
#    define UI_HAVE_IMAGE 1
#  endif
#endif

#define TEXT_CACHE_N 256
#define IMG_CACHE_N 64

static SDL_Window *g_win;
static SDL_Renderer *g_r;
static int g_ready;

typedef struct {
    char *text;
    uint32_t color;
    int px;
    SDL_Texture *tex;
    int w;
    uint64_t used;
} text_entry;

static text_entry g_text_cache[TEXT_CACHE_N];
static uint64_t g_clock;

#ifdef UI_HAVE_IMAGE
typedef struct {
    char *key;
    SDL_Texture *tex;
    int w, h;
    uint64_t frame_ts;
    uint64_t used;
} img_entry;

static img_entry g_img_cache[IMG_CACHE_N];
#endif

static char *dupstr(const char *s){
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if(p) memcpy(p, s, n);
    return p;
}

static void set_color(uint32_t argb){
    SDL_SetRenderDrawColor(g_r, (Uint8)(argb >> 16), (Uint8)(argb >> 8),
                           (Uint8)argb, (Uint8)(argb >> 24));
}

static void fill(int x, int y, int w, int h){
    SDL_Rect r;
    if(w <= 0 || h <= 0) return;
    r.x = x; r.y = y; r.w = w; r.h = h;
    SDL_RenderFillRect(g_r, &r);
}

int ui_init(void){
    Uint32 win_flags;
    int win_w, win_h;
#ifdef __PS4__
    win_flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
    win_w = 1920; win_h = 1080;
#else
    win_flags = 0;
    win_w = 1280; win_h = 720;
#endif
    if(SDL_Init(SDL_INIT_VIDEO) != 0) return 0;
    g_win = SDL_CreateWindow("orbis-discord", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, win_w, win_h, win_flags);
    if(!g_win) return 0;
    g_r = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!g_r) g_r = SDL_CreateRenderer(g_win, -1, 0);
    if(!g_r){
        SDL_DestroyWindow(g_win);
        g_win = NULL;
        return 0;
    }
    SDL_RenderSetLogicalSize(g_r, 1920, 1080);
    SDL_SetRenderDrawBlendMode(g_r, SDL_BLENDMODE_BLEND);
    if(!font_init(roboto_ttf, roboto_ttf_len, notoemoji_ttf, notoemoji_ttf_len)){
        SDL_DestroyRenderer(g_r);
        SDL_DestroyWindow(g_win);
        g_r = NULL; g_win = NULL;
        return 0;
    }
    g_ready = 1;
    return 1;
}

void ui_frame_begin(void){
    if(!g_ready) return;
    SDL_SetRenderDrawColor(g_r, 0, 0, 0, 255);
    SDL_RenderClear(g_r);
}

void ui_frame_end(void){
    if(!g_ready) return;
    SDL_RenderPresent(g_r);
}

void ui_rect(int x, int y, int w, int h, uint32_t argb){
    if(!g_ready) return;
    set_color(argb);
    fill(x, y, w, h);
}

static int isqrt_i(int v){
    int r = 0;
    while((r + 1) * (r + 1) <= v) r++;
    return r;
}

void ui_rrect(int x, int y, int w, int h, int radius, uint32_t argb){
    int i;
    if(!g_ready || w <= 0 || h <= 0) return;
    if(radius > w / 2) radius = w / 2;
    if(radius > h / 2) radius = h / 2;
    if(radius <= 0){
        ui_rect(x, y, w, h, argb);
        return;
    }
    set_color(argb);
    fill(x, y + radius, w, h - 2 * radius);
    fill(x + radius, y, w - 2 * radius, radius);
    fill(x + radius, y + h - radius, w - 2 * radius, radius);
    for(i = 0; i < radius; i++){
        int dy = radius - 1 - i;
        int half = isqrt_i(radius * radius - dy * dy);
        fill(x + radius - half, y + i, half, 1);
        fill(x + w - radius, y + i, half, 1);
        fill(x + radius - half, y + h - 1 - i, half, 1);
        fill(x + w - radius, y + h - 1 - i, half, 1);
    }
}

static SDL_Texture *build_text(const char *s, uint32_t color, int px, int *out_w){
    const char *p;
    uint32_t *pix;
    int w = 0, pen = 0, ascent, lh;
    SDL_Texture *tex;
    p = s;
    while(*p){
        uint32_t cp = font_utf8_next(&p);
        if(!cp) break;
        w += font_glyph_advance(cp, px);
    }
    if(w <= 0) return NULL;
    ascent = font_ascent(px);
    lh = font_line_height(px);
    if(lh < ascent + 1) lh = ascent + 1;
    pix = calloc((size_t)w * (size_t)lh, sizeof *pix);
    if(!pix) return NULL;
    p = s;
    while(*p){
        font_glyph g;
        uint32_t cp = font_utf8_next(&p);
        if(!cp) break;
        if(font_glyph_get(&g, cp, px)){
            if(g.w > 0 && g.pixels){
                int gx, gy;
                for(gy = 0; gy < g.h; gy++){
                    int py = ascent + g.yoff + gy;
                    if(py < 0 || py >= lh) continue;
                    for(gx = 0; gx < g.w; gx++){
                        int pxx = pen + g.xoff + gx;
                        unsigned int a;
                        uint32_t *dst;
                        if(pxx < 0 || pxx >= w) continue;
                        a = g.pixels[gy * g.w + gx];
                        if(!a) continue;
                        a = a * ((color >> 24) & 0xFFu) / 255u;
                        dst = &pix[py * w + pxx];
                        if(a > (*dst >> 24)) *dst = (a << 24) | (color & 0xFFFFFFu);
                    }
                }
            }
            pen += g.advance;
            font_glyph_free(&g);
        }
    }
    tex = SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STATIC, w, lh);
    if(tex){
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(tex, NULL, pix, w * 4);
    }
    free(pix);
    *out_w = w;
    return tex;
}

static text_entry *text_find(const char *s, uint32_t color, int px){
    int i;
    for(i = 0; i < TEXT_CACHE_N; i++){
        text_entry *e = &g_text_cache[i];
        if(e->tex && e->color == color && e->px == px && strcmp(e->text, s) == 0){
            e->used = ++g_clock;
            return e;
        }
    }
    return NULL;
}

static text_entry *text_slot(void){
    int i, oldest = 0;
    for(i = 0; i < TEXT_CACHE_N; i++)
        if(!g_text_cache[i].tex) return &g_text_cache[i];
    for(i = 1; i < TEXT_CACHE_N; i++)
        if(g_text_cache[i].used < g_text_cache[oldest].used) oldest = i;
    return &g_text_cache[oldest];
}

static int text_add(const char *s, uint32_t color, int px){
    text_entry *e = text_slot();
    SDL_Texture *tex;
    int w = 0;
    if(e->tex){
        SDL_DestroyTexture(e->tex);
        free(e->text);
        e->tex = NULL;
        e->text = NULL;
    }
    tex = build_text(s, color, px, &w);
    if(!tex) return 0;
    e->text = dupstr(s);
    if(!e->text){
        SDL_DestroyTexture(tex);
        return 0;
    }
    e->color = color;
    e->px = px;
    e->tex = tex;
    e->w = w;
    e->used = ++g_clock;
    return 1;
}

int ui_text(int x, int y, const char *utf8, uint32_t argb, int px){
    text_entry *e;
    SDL_Rect dst;
    if(!g_ready || !utf8 || px < 1) return 0;
    e = text_find(utf8, argb, px);
    if(!e){
        if(!text_add(utf8, argb, px)) return 0;
        e = text_find(utf8, argb, px);
        if(!e) return 0;
    }
    dst.x = x;
    dst.y = y;
    dst.w = e->w;
    dst.h = font_line_height(px);
    SDL_RenderCopy(g_r, e->tex, NULL, &dst);
    return e->w;
}

int ui_text_width(const char *utf8, int px){
    const char *p;
    int w = 0;
    if(!utf8 || px < 1) return 0;
    p = utf8;
    while(*p){
        uint32_t cp = font_utf8_next(&p);
        if(!cp) break;
        w += font_glyph_advance(cp, px);
    }
    return w;
}

void ui_set_clip(int x, int y, int w, int h){
    SDL_Rect r;
    if(!g_ready) return;
    r.x = x; r.y = y;
    r.w = w > 0 ? w : 0;
    r.h = h > 0 ? h : 0;
    SDL_RenderSetClipRect(g_r, &r);
}

void ui_clear_clip(void){
    if(!g_ready) return;
    SDL_RenderSetClipRect(g_r, NULL);
}

#ifdef UI_HAVE_IMAGE

static img_entry *img_slot(void){
    int i, oldest = 0;
    for(i = 0; i < IMG_CACHE_N; i++)
        if(!g_img_cache[i].tex) return &g_img_cache[i];
    for(i = 1; i < IMG_CACHE_N; i++)
        if(g_img_cache[i].used < g_img_cache[oldest].used) oldest = i;
    return &g_img_cache[oldest];
}

static img_entry *img_find(const char *key){
    int i;
    for(i = 0; i < IMG_CACHE_N; i++){
        img_entry *e = &g_img_cache[i];
        if(e->tex && strcmp(e->key, key) == 0){
            e->used = ++g_clock;
            return e;
        }
    }
    return NULL;
}

int ui_img(const char *key, int x, int y, int w, int h){
    image *im;
    img_entry *e;
    const uint32_t *pix;
    uint64_t ts;
    int iw, ih;
    SDL_Rect dst;
    if(!g_ready || !key) return 0;
    im = image_get(key);
    if(!im || !image_ready(im)) return 0;
    pix = image_pixels_rgba(im);
    iw = image_w(im);
    ih = image_h(im);
    if(!pix || iw <= 0 || ih <= 0) return 0;
    ts = image_frame_ts(im);
    e = img_find(key);
    if(!e){
        e = img_slot();
        if(e->tex){
            SDL_DestroyTexture(e->tex);
            free(e->key);
            e->tex = NULL;
            e->key = NULL;
        }
        e->key = dupstr(key);
        if(!e->key) return 0;
        e->w = 0;
        e->h = 0;
        e->frame_ts = 0;
        e->used = ++g_clock;
    }
    if(!e->tex || e->w != iw || e->h != ih){
        if(e->tex) SDL_DestroyTexture(e->tex);
        e->tex = SDL_CreateTexture(g_r, SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STATIC, iw, ih);
        if(!e->tex){ e->w = 0; e->h = 0; return 0; }
        SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_BLEND);
        e->w = iw;
        e->h = ih;
        e->frame_ts = 0;
    }
    if(e->frame_ts != ts){
        SDL_UpdateTexture(e->tex, NULL, pix, iw * 4);
        e->frame_ts = ts;
    }
    dst.x = x;
    dst.y = y;
    dst.w = w > 0 ? w : iw;
    dst.h = h > 0 ? h : ih;
    SDL_RenderCopy(g_r, e->tex, NULL, &dst);
    return 1;
}

#else

int ui_img(const char *key, int x, int y, int w, int h){
    (void)key; (void)x; (void)y; (void)w; (void)h;
    return 0;
}

#endif

void ui_pump(void){
    SDL_Event ev;
    while(SDL_PollEvent(&ev)){}
}
