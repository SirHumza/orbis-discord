#include "tests.h"
#include "fixtures.h"
#include "../src/image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PX(r,g,b,a) ((uint32_t)(r) | ((uint32_t)(g)<<8) | ((uint32_t)(b)<<16) | ((uint32_t)(a)<<24))

typedef struct {
    int calls;
    int fail;
    const unsigned char *data;
    size_t len;
} fetch_stub;

static int stub_fetch(const char *url, unsigned char **out, size_t *len, void *ud){
    fetch_stub *s = ud;
    unsigned char *p;
    (void)url;
    s->calls++;
    if(s->fail || !s->data || !s->len){
        *out = NULL;
        *len = 0;
        return -1;
    }
    p = malloc(s->len);
    if(!p) return -1;
    memcpy(p, s->data, s->len);
    *out = p;
    *len = s->len;
    return 0;
}

static void tmp_dir(char *dir, size_t cap){
    static int seq;
    snprintf(dir, cap, "/tmp/orbis_image_test_%ld_%d", (long)getpid(), seq++);
    mkdir(dir, 0700);
}

static void begin(fetch_stub *s, const unsigned char *data, size_t len){
    image_cache_clear();
    memset(s, 0, sizeof *s);
    s->data = data;
    s->len = len;
    image_set_fetch(stub_fetch, s);
}

TEST(test_image_handle_not_ready_before_tick){
    fetch_stub s;
    image *im;
    begin(&s, png_2x2, png_2x2_len);
    im = image_get("https://cdn.example/a.png");
    ASSERT_TRUE(im != NULL);
    ASSERT_EQ_INT(image_ready(im), 0);
    ASSERT_TRUE(image_pixels_rgba(im) == NULL);
    ASSERT_EQ_INT(s.calls, 1);
}

TEST(test_image_png_ready_dims){
    fetch_stub s;
    image *im;
    begin(&s, png_2x2, png_2x2_len);
    im = image_get("https://cdn.example/a.png");
    image_tick(1000);
    ASSERT_EQ_INT(image_ready(im), 1);
    ASSERT_EQ_INT(image_w(im), 2);
    ASSERT_EQ_INT(image_h(im), 2);
    ASSERT_TRUE(image_pixels_rgba(im) != NULL);
}

TEST(test_image_png_pixels_exact){
    fetch_stub s;
    image *im;
    const uint32_t *p;
    begin(&s, png_2x2, png_2x2_len);
    im = image_get("https://cdn.example/a.png");
    image_tick(1000);
    p = image_pixels_rgba(im);
    ASSERT_TRUE(p != NULL);
    ASSERT_EQ_INT(p[0], PX(255, 0, 0, 255));
    ASSERT_EQ_INT(p[1], PX(0, 255, 0, 255));
    ASSERT_EQ_INT(p[2], PX(0, 0, 255, 255));
    ASSERT_EQ_INT(p[3], PX(255, 255, 255, 255));
}

TEST(test_image_same_url_one_fetch){
    fetch_stub s;
    image *a;
    image *b;
    begin(&s, png_2x2, png_2x2_len);
    a = image_get("https://cdn.example/a.png");
    b = image_get("https://cdn.example/a.png");
    ASSERT_TRUE(a != NULL);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ_INT(s.calls, 1);
}

TEST(test_image_gif_frame_advance){
    fetch_stub s;
    image *im;
    const uint32_t *p;
    uint64_t t0;
    uint64_t t1;
    begin(&s, gif_anim_2f, gif_anim_2f_len);
    im = image_get("https://cdn.example/anim.gif");
    image_tick(1000);
    ASSERT_EQ_INT(image_ready(im), 1);
    ASSERT_EQ_INT(image_w(im), 2);
    ASSERT_EQ_INT(image_h(im), 2);
    t0 = image_frame_ts(im);
    p = image_pixels_rgba(im);
    ASSERT_TRUE(p != NULL);
    ASSERT_EQ_INT(p[0], PX(255, 0, 0, 255));
    image_tick(1100);
    t1 = image_frame_ts(im);
    p = image_pixels_rgba(im);
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(t1 != t0);
    ASSERT_EQ_INT(p[0], PX(0, 0, 255, 255));
}

TEST(test_image_gif_holds_last_frame){
    fetch_stub s;
    image *im;
    const uint32_t *p;
    uint64_t t1;
    begin(&s, gif_anim_2f, gif_anim_2f_len);
    im = image_get("https://cdn.example/anim.gif");
    image_tick(1000);
    image_tick(1100);
    t1 = image_frame_ts(im);
    image_tick(1500);
    p = image_pixels_rgba(im);
    ASSERT_TRUE(p != NULL);
    ASSERT_EQ_INT(image_ready(im), 1);
    ASSERT_EQ_INT(p[0], PX(0, 0, 255, 255));
    ASSERT_EQ_INT(image_frame_ts(im), t1);
}

TEST(test_image_static_gif_stable){
    fetch_stub s;
    image *im;
    const uint32_t *p;
    uint64_t t0;
    begin(&s, gif_static, gif_static_len);
    im = image_get("https://cdn.example/static.gif");
    image_tick(2000);
    ASSERT_EQ_INT(image_ready(im), 1);
    t0 = image_frame_ts(im);
    p = image_pixels_rgba(im);
    ASSERT_TRUE(p != NULL);
    ASSERT_EQ_INT(p[0], PX(0, 255, 0, 255));
    image_tick(2500);
    ASSERT_EQ_INT(image_frame_ts(im), t0);
    ASSERT_EQ_INT(image_pixels_rgba(im)[0], PX(0, 255, 0, 255));
}

TEST(test_image_png_frame_ts_stable){
    fetch_stub s;
    image *im;
    uint64_t t0;
    begin(&s, png_2x2, png_2x2_len);
    im = image_get("https://cdn.example/a.png");
    image_tick(3000);
    t0 = image_frame_ts(im);
    image_tick(9000);
    ASSERT_EQ_INT(image_frame_ts(im), t0);
}

TEST(test_image_bad_bytes_permanent_miss){
    fetch_stub s;
    image *im;
    begin(&s, bad_bytes, bad_bytes_len);
    im = image_get("https://cdn.example/bad.bin");
    image_tick(4000);
    image_tick(5000);
    ASSERT_TRUE(im != NULL);
    ASSERT_EQ_INT(image_ready(im), 0);
    ASSERT_TRUE(image_pixels_rgba(im) == NULL);
    ASSERT_EQ_INT(image_w(im), 0);
    ASSERT_EQ_INT(image_h(im), 0);
    ASSERT_EQ_INT(s.calls, 1);
}

TEST(test_image_fetch_failure_permanent_miss){
    fetch_stub s;
    image *im;
    begin(&s, png_2x2, png_2x2_len);
    s.fail = 1;
    im = image_get("https://cdn.example/missing.png");
    image_tick(4000);
    image_tick(5000);
    ASSERT_TRUE(im != NULL);
    ASSERT_EQ_INT(image_ready(im), 0);
    ASSERT_TRUE(image_pixels_rgba(im) == NULL);
    ASSERT_EQ_INT(s.calls, 1);
}

TEST(test_image_without_fetch_never_ready){
    image *im;
    image_cache_clear();
    image_set_fetch(NULL, NULL);
    im = image_get("https://cdn.example/nofetch.png");
    image_tick(1000);
    image_tick(2000);
    ASSERT_TRUE(im != NULL);
    ASSERT_EQ_INT(image_ready(im), 0);
    ASSERT_TRUE(image_pixels_rgba(im) == NULL);
}

TEST(test_image_save_writes_png){
    fetch_stub s;
    image *im;
    const char *path;
    unsigned char buf[128];
    size_t n;
    FILE *f;
    char dir[128];
    tmp_dir(dir, sizeof dir);
    begin(&s, png_2x2, png_2x2_len);
    ASSERT_EQ_INT(image_save("https://cdn.example/a.png", dir), 1);
    im = image_get("https://cdn.example/a.png");
    path = image_path_on_disk(im);
    ASSERT_TRUE(path != NULL);
    f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    ASSERT_EQ_INT(n, png_2x2_len);
    ASSERT_EQ_MEM(buf, png_2x2, png_2x2_len);
    ASSERT_EQ_STR(path + strlen(path) - 4, ".png");
    unlink(path);
    rmdir(dir);
}

TEST(test_image_save_reuses_cache){
    fetch_stub s;
    int calls;
    char dir[128];
    tmp_dir(dir, sizeof dir);
    begin(&s, png_2x2, png_2x2_len);
    ASSERT_EQ_INT(image_save("https://cdn.example/a.png", dir), 1);
    calls = s.calls;
    ASSERT_EQ_INT(image_save("https://cdn.example/a.png", dir), 1);
    ASSERT_EQ_INT(s.calls, calls);
    {
        image *im = image_get("https://cdn.example/a.png");
        const char *path = image_path_on_disk(im);
        ASSERT_TRUE(path != NULL);
        unlink(path);
    }
    rmdir(dir);
}

TEST(test_image_eviction_refetches){
    fetch_stub s;
    char url[64];
    int i;
    begin(&s, png_2x2, png_2x2_len);
    for(i = 0; i < 97; i++){
        snprintf(url, sizeof url, "https://cdn.example/%d.png", i);
        ASSERT_TRUE(image_get(url) != NULL);
    }
    ASSERT_EQ_INT(s.calls, 97);
    ASSERT_TRUE(image_get("https://cdn.example/0.png") != NULL);
    ASSERT_EQ_INT(s.calls, 98);
}

TEST(test_image_cache_clear_refetches){
    fetch_stub s;
    image *im;
    begin(&s, png_2x2, png_2x2_len);
    im = image_get("https://cdn.example/a.png");
    image_tick(1000);
    ASSERT_EQ_INT(image_ready(im), 1);
    ASSERT_EQ_INT(s.calls, 1);
    image_cache_clear();
    ASSERT_TRUE(image_get("https://cdn.example/a.png") != NULL);
    ASSERT_EQ_INT(s.calls, 2);
}

void run_image_tests(void){
    g_tests_run++; test_image_handle_not_ready_before_tick();
    g_tests_run++; test_image_png_ready_dims();
    g_tests_run++; test_image_png_pixels_exact();
    g_tests_run++; test_image_same_url_one_fetch();
    g_tests_run++; test_image_gif_frame_advance();
    g_tests_run++; test_image_gif_holds_last_frame();
    g_tests_run++; test_image_static_gif_stable();
    g_tests_run++; test_image_png_frame_ts_stable();
    g_tests_run++; test_image_bad_bytes_permanent_miss();
    g_tests_run++; test_image_fetch_failure_permanent_miss();
    g_tests_run++; test_image_without_fetch_never_ready();
    g_tests_run++; test_image_save_writes_png();
    g_tests_run++; test_image_save_reuses_cache();
    g_tests_run++; test_image_eviction_refetches();
    g_tests_run++; test_image_cache_clear_refetches();
}
