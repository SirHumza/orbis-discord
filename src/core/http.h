#ifndef ORBIS_HTTP_H
#define ORBIS_HTTP_H
#include <stddef.h>

typedef struct {
    int (*read)(void *ctx, char *buf, size_t cap);
    int (*write)(void *ctx, const char *buf, size_t len);
    void *ctx;
} http_io;

typedef struct {
    int status;
    char *body;
    size_t len;
} http_resp;

typedef struct http http;

http *http_new(http_io io);
void http_free(http *);
void http_resp_free(http_resp *);
int http_request(http *, const char *host, const char *method, const char *path,
                 const char *body, const char *token, http_resp *out);
#endif
