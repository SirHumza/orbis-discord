/* tenor_net.c - one cached TLS client to tenor.googleapis.com.
 * Blocking by design: the caller runs on the UI thread, same tradeoff as the
 * CDN fetcher; move both to the worker queue together if stalls matter. */
#include "tenor_net.h"
#include "../core/http_tls.h"
#include "../core/log.h"
#include <stdlib.h>

#define TENOR_HOST "tenor.googleapis.com"

static http *g_tenor;

int tenor_net_get(const char *path, char **out, size_t *len){
    http_resp r;
    if(!path || !out || !len) return -1;
    if(!g_tenor){
        g_tenor = http_tls_new(TENOR_HOST, 443);
        if(!g_tenor) return -1;
    }
    if(http_request(g_tenor, TENOR_HOST, "GET", path, NULL, NULL, &r) != 0)
        return -1;
    if(r.status != 200 || !r.body || r.len == 0){
        log_msg("tenor: fetch failed (%d)", r.status);
        http_resp_free(&r);
        return -1;
    }
    *out = r.body;
    *len = r.len;
    r.body = NULL;
    http_resp_free(&r);
    return 0;
}
