#include "cdn.h"
#include "../image.h"
#include "../core/http_tls.h"
#include "../core/log.h"
#include <stdlib.h>
#include <string.h>

#define CDN_HOST "cdn.discordapp.com"
#define MEDIA_HOST "media.discordapp.net"
#define TENOR_MEDIA_HOST "media.tenor.com"

static http *g_cdn;
static http *g_media;
static http *g_tenor_media;

static int cdn_fetch(const char *url, unsigned char **out, size_t *len,
                     void *ud){
    const char *rest, *slash, *host;
    size_t hlen;
    http **client;
    http_resp r;
    (void)ud;
    if(!url || strncmp(url, "https://", 8) != 0) return -1;
    rest = url + 8;
    slash = strchr(rest, '/');
    if(!slash || slash == rest) return -1;
    hlen = (size_t)(slash - rest);
    if(hlen == strlen(CDN_HOST) && !strncmp(rest, CDN_HOST, hlen)){
        host = CDN_HOST;
        client = &g_cdn;
    } else if(hlen == strlen(MEDIA_HOST) && !strncmp(rest, MEDIA_HOST, hlen)){
        host = MEDIA_HOST;
        client = &g_media;
    } else if(hlen == strlen(TENOR_MEDIA_HOST) &&
              !strncmp(rest, TENOR_MEDIA_HOST, hlen)){
        host = TENOR_MEDIA_HOST;
        client = &g_tenor_media;
    } else {
        return -1;
    }
    if(!*client){
        *client = http_tls_new(host, 443);
        if(!*client) return -1;
    }
    if(http_request(*client, host, "GET", slash, NULL, NULL, &r) != 0)
        return -1;
    if(r.status != 200 || !r.body || r.len == 0){
        log_msg("cdn: fetch failed (%d)", r.status);
        http_resp_free(&r);
        return -1;
    }
    *out = (unsigned char *)r.body;
    *len = r.len;
    r.body = NULL;
    http_resp_free(&r);
    return 0;
}

void cdn_init(void){
    image_set_fetch(cdn_fetch, NULL);
}
