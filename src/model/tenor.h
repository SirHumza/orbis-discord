/* tenor.h - Tenor API v2 response parsing (host-testable, no network). */
#ifndef ORBIS_TENOR_H
#define ORBIS_TENOR_H
#include <stddef.h>

#define GIF_MAX_URL 320

typedef struct {
    char url[GIF_MAX_URL];
    char preview[GIF_MAX_URL];
    int w, h;
} gif_item;

/* Parses a /v2/search or /v2/featured body; fills up to max items.
 * Returns the number of items (0 on parse failure). */
int gif_from_tenor(const char *body, gif_item *out, int max);

#endif
