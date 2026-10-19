#include "http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define HTTP_RESP_MAX (4u * 1024u * 1024u)

struct http { http_io io; };

http *http_new(http_io io){
    http *h = calloc(1, sizeof *h);
    if(h) h->io = io;
    return h;
}

void http_free(http *h){ free(h); }

void http_resp_free(http_resp *r){
    if(!r) return;
    free(r->body);
    r->body = NULL;
    r->len = 0;
    r->status = 0;
}

static char *find_hdr_end(char *s, size_t n){
    for(size_t i = 0; i + 3 < n; i++)
        if(s[i] == '\r' && s[i+1] == '\n' && s[i+2] == '\r' && s[i+3] == '\n')
            return s + i;
    return NULL;
}

static size_t header_value_num(const char *hdrs, const char *name){
    const char *p = hdrs;
    size_t nl = strlen(name);
    while((p = strstr(p, name)) != NULL){
        const char *v = p + nl;
        while(*v == ':' || *v == ' ') v++;
        return (size_t)strtoul(v, NULL, 10);
    }
    return 0;
}

int http_request(http *h, const char *host, const char *method, const char *path,
                 const char *body, const char *token, http_resp *out){
    char hdr[1024];
    size_t blen = body ? strlen(body) : 0;
    int n, r;
    char *resp;
    size_t cap = 65536, len = 0, total;
    char *end;
    size_t body_off;
    if(!h || !host || !method || !path || !out) return -1;
    memset(out, 0, sizeof *out);
    /* An empty or NULL token means "unauthenticated endpoint" (remote-auth
     * login): omit the header entirely. A literal "Authorization: " line is
     * not equivalent -- Discord's gateway rejects it as malformed auth. */
    if(token && *token)
        n = snprintf(hdr, sizeof hdr,
            "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: orbis-discord/0.1\r\n"
            "Authorization: %s\r\nContent-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n",
            method, path, host, token, blen);
    else
        n = snprintf(hdr, sizeof hdr,
            "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: orbis-discord/0.1\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n",
            method, path, host, blen);
    if(n <= 0 || (size_t)n >= sizeof hdr) return -2;
    if(h->io.write(h->io.ctx, hdr, (size_t)n) < 0) return -3;
    if(blen && h->io.write(h->io.ctx, body, blen) < 0) return -3;

    resp = malloc(cap);
    if(!resp) return -4;
    while((r = h->io.read(h->io.ctx, resp + len, cap - len - 1)) > 0){
        len += (size_t)r;
        if(len + 1 >= cap){
            if(cap >= HTTP_RESP_MAX) { free(resp); return -5; }
            cap *= 2;
            char *p = realloc(resp, cap);
            if(!p){ free(resp); return -4; }
            resp = p;
        }
    }
    resp[len] = 0;
    end = find_hdr_end(resp, len);
    if(!end){ free(resp); return -6; }
    if(strncmp(resp, "HTTP/1.", 7) != 0){ free(resp); return -6; }
    out->status = atoi(resp + 9);
    body_off = (size_t)(end - resp) + 4;
    total = len >= body_off ? len - body_off : 0;
    {
        size_t cl = header_value_num(resp, "Content-Length");
        if(cl && cl <= total) total = cl;
    }
    out->body = malloc(total + 1);
    if(!out->body){ free(resp); return -4; }
    memcpy(out->body, resp + body_off, total);
    out->body[total] = 0;
    out->len = total;
    free(resp);
    return 0;
}
