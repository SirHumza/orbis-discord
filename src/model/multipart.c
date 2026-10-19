#include "multipart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t esc_name(const char *in, char *out, size_t cap){
    size_t n = 0;
    if(!in) in = "file";
    for(; *in && n + 3 < cap; in++){
        unsigned char c = (unsigned char)*in;
        if(c < 0x20 || c == 0x7f){
            out[n++] = '_';
            continue;
        }
        if(c == '"' || c == '\\') out[n++] = '\\';
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n;
}

int multipart_build(const char *boundary, const char *payload,
                    const char *filename, const char *ctype,
                    const void *data, size_t len, char **out, size_t *outlen){
    char safe[256];
    size_t cap, n = 0, plen = 0;
    char *buf;
    int k;
    if(!boundary || !*boundary || (!data && len) || !out || !outlen) return -1;
    esc_name(filename, safe, sizeof safe);
    if(!ctype || !*ctype) ctype = "application/octet-stream";
    if(payload) plen = strlen(payload);
    cap = 1024 + plen + len + strlen(safe) + strlen(ctype) + strlen(boundary);
    buf = malloc(cap);
    if(!buf) return -1;
    if(payload){
        k = snprintf(buf + n, cap - n,
                     "--%s\r\nContent-Disposition: form-data; "
                     "name=\"payload_json\"\r\nContent-Type: application/json"
                     "\r\n\r\n%s\r\n", boundary, payload);
        if(k < 0 || (size_t)k >= cap - n){ free(buf); return -1; }
        n += (size_t)k;
    }
    k = snprintf(buf + n, cap - n,
                 "--%s\r\nContent-Disposition: form-data; name=\"files[0]\"; "
                 "filename=\"%s\"\r\nContent-Type: %s\r\n\r\n",
                 boundary, safe, ctype);
    if(k < 0 || (size_t)k >= cap - n){ free(buf); return -1; }
    n += (size_t)k;
    if(len){
        memcpy(buf + n, data, len);
        n += len;
    }
    k = snprintf(buf + n, cap - n, "\r\n--%s--\r\n", boundary);
    if(k < 0 || (size_t)k >= cap - n){ free(buf); return -1; }
    n += (size_t)k;
    *out = buf;
    *outlen = n;
    return 0;
}
