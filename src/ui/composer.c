#include "composer.h"
#include <stdio.h>

int composer_payload(const char *content, uint64_t nonce, char *out, size_t cap){
    size_t n = 0;
    const unsigned char *s;
    if(!out || cap == 0) return -1;
    if(!content) content = "";
    s = (const unsigned char *)content;
    if(cap < 2) return -1;
    out[0] = 0;
    #define PUT(c) do { if(n + 1 >= cap) return -1; out[n++] = (char)(c); out[n] = 0; } while(0)
    PUT('{');
    PUT('"'); PUT('c'); PUT('o'); PUT('n'); PUT('t'); PUT('e'); PUT('n'); PUT('t');
    PUT('"'); PUT(':'); PUT('"');
    while(*s){
        unsigned char c = *s++;
        if(c == '"' || c == '\\'){
            PUT('\\'); PUT((char)c);
        } else if(c == '\n'){
            PUT('\\'); PUT('n');
        } else if(c == '\r'){
            PUT('\\'); PUT('r');
        } else if(c == '\t'){
            PUT('\\'); PUT('t');
        } else if(c < 0x20){
            char esc[8];
            snprintf(esc, sizeof esc, "\\u%04x", c);
            PUT(esc[0]); PUT(esc[1]); PUT(esc[2]); PUT(esc[3]); PUT(esc[4]); PUT(esc[5]);
        } else {
            PUT((char)c);
        }
    }
    {
        char tail[48];
        snprintf(tail, sizeof tail, "\",\"nonce\":\"%llu\"}", (unsigned long long)nonce);
        {
            const char *p = tail;
            while(*p) PUT(*p++);
        }
    }
    #undef PUT
    return (int)n;
}
