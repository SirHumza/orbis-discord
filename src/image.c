#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../third_party/stb_image.h"
#include "../third_party/gifdec.h"
#include "../third_party/gifdec.c"

#include "image.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define IMG_CACHE_MAX 96
#define IMG_PATH_MAX 1024
#define IMG_DEFAULT_SAVE_DIR "/data/orbis-discord/downloads"

enum {
    IMG_PENDING = 0,
    IMG_READY,
    IMG_FAILED
};

struct image {
    char *url;
    unsigned char *data;
    size_t len;
    uint32_t *pixels;
    int w;
    int h;
    int state;
    int anim_done;
    gd_GIF *gif;
    uint64_t frame_ts;
    uint64_t frame_start;
    uint64_t frame_delay;
    uint64_t lru;
    char path[IMG_PATH_MAX];
};

static image *g_entries[IMG_CACHE_MAX];
static size_t g_count;
static uint64_t g_clock;
static int (*g_fetch)(const char *url, unsigned char **out, size_t *len, void *ud);
static void *g_fetch_ud;
static char g_save_dir[IMG_PATH_MAX] = IMG_DEFAULT_SAVE_DIR;

static uint16_t fnv1a16(const char *s){
    uint16_t h = 0x811c;
    while(*s){
        h ^= (uint8_t)*s++;
        h = (uint16_t)(h * 0x0193u);
    }
    return h;
}

static int has_png_magic(const unsigned char *d, size_t n){
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return n >= sizeof sig && memcmp(d, sig, sizeof sig) == 0;
}

static int has_jpeg_magic(const unsigned char *d, size_t n){
    return n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff;
}

static int has_gif_magic(const unsigned char *d, size_t n){
    return n >= 6 && memcmp(d, "GIF", 3) == 0 &&
           (memcmp(d + 3, "87a", 3) == 0 || memcmp(d + 3, "89a", 3) == 0);
}

static const char *ext_for(const unsigned char *d, size_t n){
    if(has_png_magic(d, n)) return "png";
    if(has_jpeg_magic(d, n)) return "jpg";
    if(n >= 6 && memcmp(d, "GIF", 3) == 0) return "gif";
    return NULL;
}

static void image_free(image *im){
    if(!im) return;
    if(im->gif) gd_close_gif(im->gif);
    free(im->pixels);
    free(im->data);
    free(im->url);
    free(im);
}

static void image_fail(image *im){
    im->state = IMG_FAILED;
    free(im->pixels);
    im->pixels = NULL;
    if(im->gif){
        gd_close_gif(im->gif);
        im->gif = NULL;
    }
    free(im->data);
    im->data = NULL;
    im->len = 0;
    im->w = 0;
    im->h = 0;
}

static int gif_render(image *im){
    size_t count = (size_t)im->w * (size_t)im->h;
    size_t i;
    unsigned char *rgb;
    if(count > SIZE_MAX / 3) return 0;
    rgb = malloc(count * 3);
    if(!rgb) return 0;
    gd_render_frame(im->gif, rgb);
    for(i = 0; i < count; i++)
        im->pixels[i] = (uint32_t)rgb[i * 3] |
                        ((uint32_t)rgb[i * 3 + 1] << 8) |
                        ((uint32_t)rgb[i * 3 + 2] << 16) |
                        0xff000000u;
    free(rgb);
    return 1;
}

static void image_decode_png_jpeg(image *im, uint64_t now_ms){
    int w = 0, h = 0, comp = 0;
    size_t count, i;
    unsigned char *rgba;

    if(im->len > (size_t)INT_MAX) { image_fail(im); return; }
    rgba = stbi_load_from_memory(im->data, (int)im->len, &w, &h, &comp, 4);
    if(!rgba || w <= 0 || h <= 0){
        stbi_image_free(rgba);
        image_fail(im);
        return;
    }
    count = (size_t)w * (size_t)h;
    if(count > SIZE_MAX / sizeof(uint32_t)){
        stbi_image_free(rgba);
        image_fail(im);
        return;
    }
    im->pixels = malloc(count * sizeof(uint32_t));
    if(!im->pixels){
        stbi_image_free(rgba);
        image_fail(im);
        return;
    }
    for(i = 0; i < count; i++)
        im->pixels[i] = (uint32_t)rgba[i * 4] |
                        ((uint32_t)rgba[i * 4 + 1] << 8) |
                        ((uint32_t)rgba[i * 4 + 2] << 16) |
                        ((uint32_t)rgba[i * 4 + 3] << 24);
    stbi_image_free(rgba);
    im->w = w;
    im->h = h;
    im->frame_ts = now_ms;
    im->state = IMG_READY;
}

static void image_decode_gif(image *im, uint64_t now_ms){
    gd_GIF *gif;
    size_t count;

    gif = gd_open_gif_data_len(im->data, im->len);
    if(!gif || gif->width == 0 || gif->height == 0){
        if(gif) gd_close_gif(gif);
        image_fail(im);
        return;
    }
    im->w = gif->width;
    im->h = gif->height;
    count = (size_t)im->w * (size_t)im->h;
    if(count > SIZE_MAX / sizeof(uint32_t)){
        gd_close_gif(gif);
        image_fail(im);
        return;
    }
    im->pixels = malloc(count * sizeof(uint32_t));
    if(!im->pixels){
        gd_close_gif(gif);
        image_fail(im);
        return;
    }
    im->gif = gif;
    if(gd_get_frame(gif) != 1 || !gif_render(im)){
        image_fail(im);
        return;
    }
    im->frame_delay = (uint64_t)gif->gce.delay * 10;
    if(im->frame_delay == 0) im->frame_delay = 100;
    im->frame_start = now_ms;
    im->frame_ts = now_ms;
    im->state = IMG_READY;
}

static void image_decode(image *im, uint64_t now_ms){
    if(!im->data || im->len == 0){
        image_fail(im);
        return;
    }
    if(has_png_magic(im->data, im->len) || has_jpeg_magic(im->data, im->len))
        image_decode_png_jpeg(im, now_ms);
    else if(has_gif_magic(im->data, im->len))
        image_decode_gif(im, now_ms);
    else
        image_fail(im);
}

static void image_advance(image *im, uint64_t now_ms){
    if(im->anim_done || !im->gif) return;
    if(now_ms < im->frame_start) return;
    while(now_ms - im->frame_start >= im->frame_delay){
        if(gd_get_frame(im->gif) != 1 || !gif_render(im)){
            im->anim_done = 1;
            return;
        }
        im->frame_start += im->frame_delay;
        im->frame_delay = (uint64_t)im->gif->gce.delay * 10;
        if(im->frame_delay == 0) im->frame_delay = 100;
        im->frame_ts = im->frame_start;
    }
}

void image_set_fetch(int (*fetch)(const char *url, unsigned char **out, size_t *len, void *ud), void *ud){
    g_fetch = fetch;
    g_fetch_ud = ud;
}

void image_set_save_dir(const char *dir){
    if(!dir || !*dir) dir = IMG_DEFAULT_SAVE_DIR;
    snprintf(g_save_dir, sizeof g_save_dir, "%s", dir);
}

static image *image_find(const char *url){
    size_t i;
    for(i = 0; i < g_count; i++)
        if(strcmp(g_entries[i]->url, url) == 0) return g_entries[i];
    return NULL;
}

static void image_evict_one(void){
    size_t i, victim = 0;
    for(i = 1; i < g_count; i++)
        if(g_entries[i]->lru < g_entries[victim]->lru) victim = i;
    image_free(g_entries[victim]);
    g_entries[victim] = g_entries[g_count - 1];
    g_count--;
}

image *image_get(const char *url){
    image *im;
    unsigned char *out = NULL;
    size_t len = 0;

    if(!url) return NULL;
    im = image_find(url);
    if(im){
        im->lru = ++g_clock;
        return im;
    }
    if(g_count >= IMG_CACHE_MAX) image_evict_one();
    im = calloc(1, sizeof *im);
    if(!im) return NULL;
    im->url = malloc(strlen(url) + 1);
    if(!im->url){
        free(im);
        return NULL;
    }
    strcpy(im->url, url);
    im->lru = ++g_clock;
    im->state = IMG_PENDING;
    g_entries[g_count++] = im;
    if(!g_fetch){
        im->state = IMG_FAILED;
        return im;
    }
    if(g_fetch(url, &out, &len, g_fetch_ud) == 0 && out && len > 0){
        im->data = out;
        im->len = len;
    } else {
        free(out);
        im->state = IMG_FAILED;
    }
    return im;
}

void image_tick(uint64_t now_ms){
    size_t i;
    for(i = 0; i < g_count; i++){
        image *im = g_entries[i];
        if(im->state == IMG_PENDING)
            image_decode(im, now_ms);
        else if(im->state == IMG_READY && im->gif)
            image_advance(im, now_ms);
    }
}

int image_ready(const image *im){
    return im && im->state == IMG_READY;
}

int image_w(const image *im){
    return image_ready(im) ? im->w : 0;
}

int image_h(const image *im){
    return image_ready(im) ? im->h : 0;
}

const uint32_t *image_pixels_rgba(const image *im){
    return image_ready(im) ? im->pixels : NULL;
}

uint64_t image_frame_ts(const image *im){
    return image_ready(im) ? im->frame_ts : 0;
}

const char *image_path_on_disk(const image *im){
    if(!im || !im->path[0]) return NULL;
    return im->path;
}

int image_save(const char *url, const char *dir){
    image *im;
    const char *ext;
    const char *target_dir;
    FILE *f;
    size_t written;

    if(!url) return 0;
    im = image_get(url);
    if(!im || im->state == IMG_FAILED || !im->data || im->len == 0) return 0;
    if(im->path[0]) return 1;
    ext = ext_for(im->data, im->len);
    if(!ext) return 0;
    target_dir = (dir && *dir) ? dir : g_save_dir;
    mkdir(target_dir, 0777);
    snprintf(im->path, sizeof im->path, "%s/%04x.%s",
             target_dir, (unsigned)fnv1a16(url), ext);
    f = fopen(im->path, "wb");
    if(!f){
        im->path[0] = 0;
        return 0;
    }
    written = fwrite(im->data, 1, im->len, f);
    if(fclose(f) != 0 || written != im->len){
        im->path[0] = 0;
        return 0;
    }
    return 1;
}

void image_cache_clear(void){
    size_t i;
    for(i = 0; i < g_count; i++)
        image_free(g_entries[i]);
    g_count = 0;
}
