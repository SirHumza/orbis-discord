#ifndef ORBIS_IMAGE_H
#define ORBIS_IMAGE_H
#include <stddef.h>
#include <stdint.h>

typedef struct image image;

/* fetch: 0 = success, *out is malloc'd and owned by the cache; nonzero = permanent miss. */
void image_set_fetch(int (*fetch)(const char *url, unsigned char **out, size_t *len, void *ud), void *ud);
void image_set_save_dir(const char *dir);
image *image_get(const char *url);
void image_tick(uint64_t now_ms);
int image_ready(const image *);
int image_w(const image *);
int image_h(const image *);
const uint32_t *image_pixels_rgba(const image *);
uint64_t image_frame_ts(const image *);
const char *image_path_on_disk(const image *);
int image_save(const char *url, const char *dir);
void image_cache_clear(void);

#endif
