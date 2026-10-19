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

static int ci_eq(const char *a, const char *b, size_t n){
    size_t i;
    for(i = 0; i < n; i++){
        char x = a[i], y = b[i];
        if(x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if(y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if(x != y) return 0;
    }
    return 1;
}

static int ci_contains(const char *hay, const char *needle){
    size_t nl = strlen(needle), hl = strlen(hay), i;
    for(i = 0; i + nl <= hl; i++)
        if(ci_eq(hay + i, needle, nl)) return 1;
    return 0;
}

/* Header names are case-insensitive. hdrs/hdr_len cover the status line plus
 * header block; out is always NUL-terminated. Returns value length. */
static size_t header_value(const char *hdrs, size_t hdr_len, const char *name,
                           char *out, size_t cap){
    size_t nl = strlen(name), pos = 0;
    if(out && cap) out[0] = 0;
    while(pos + nl < hdr_len){
        const char *line = hdrs + pos;
        const char *eol = strstr(line, "\r\n");
        const char *v, *ve;
        size_t n;
        if(!eol || (size_t)(eol - hdrs) > hdr_len) break;
        if((size_t)(eol - line) > nl && ci_eq(line, name, nl) && line[nl] == ':'){
            v = line + nl + 1;
            ve = eol;
            while(v < ve && (*v == ' ' || *v == '\t')) v++;
            while(ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
            n = (size_t)(ve - v);
            if(out && cap){
                if(n >= cap) n = cap - 1;
                memcpy(out, v, n);
                out[n] = 0;
            }
            return n;
        }
        pos = (size_t)(eol - hdrs) + 2;
    }
    return 0;
}

/* Decodes a chunked body in place. Returns decoded length, or (size_t)-1. */
static size_t dechunk(char *s, size_t n){
    size_t in = 0, out = 0;
    while(in < n){
        char *end;
        unsigned long sz = strtoul(s + in, &end, 16);
        if(end == s + in) return (size_t)-1;
        in = (size_t)(end - s);
        while(in < n && s[in] != '\n') in++;
        if(in >= n) return (size_t)-1;
        in++;
        if(sz == 0) break;
        if(sz > n - in) return (size_t)-1;
        memmove(s + out, s + in, sz);
        out += sz;
        in += sz;
        if(in + 1 < n && s[in] == '\r' && s[in + 1] == '\n') in += 2;
        else if(in < n && s[in] == '\n') in++;
        else if(in < n) return (size_t)-1;
    }
    return out;
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
        char clbuf[32], te[128];
        size_t hdr_len = (size_t)(end - resp);
        if(header_value(resp, hdr_len, "Transfer-Encoding", te, sizeof te) &&
           ci_contains(te, "chunked")){
            size_t d = dechunk(resp + body_off, total);
            if(d != (size_t)-1) total = d;
        } else if(header_value(resp, hdr_len, "Content-Length", clbuf, sizeof clbuf)){
            size_t cl = (size_t)strtoul(clbuf, NULL, 10);
            if(cl <= total) total = cl;
        }
    }
    out->body = malloc(total + 1);
    if(!out->body){ free(resp); return -4; }
    memcpy(out->body, resp + body_off, total);
    out->body[total] = 0;
    out->len = total;
    free(resp);
    return 0;
}
