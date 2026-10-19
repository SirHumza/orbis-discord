#include "composer.h"
#include <stdio.h>

static int payload_build(const char *content, uint64_t nonce, uint64_t reply_id,
                         uint64_t reply_chan, int edit, char *out, size_t cap){
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
    if(edit){
        PUT('"'); PUT('}');
    } else {
        char tail[192];
        if(reply_id)
            snprintf(tail, sizeof tail,
                     "\",\"nonce\":\"%llu\",\"message_reference\":{"
                     "\"message_id\":\"%llu\",\"channel_id\":\"%llu\","
                     "\"fail_if_not_exists\":false}}",
                     (unsigned long long)nonce, (unsigned long long)reply_id,
                     (unsigned long long)reply_chan);
        else
            snprintf(tail, sizeof tail, "\",\"nonce\":\"%llu\"}",
                     (unsigned long long)nonce);
        {
            const char *p = tail;
            while(*p) PUT(*p++);
        }
    }
    #undef PUT
    return (int)n;
}

int composer_payload(const char *content, uint64_t nonce, char *out, size_t cap){
    return payload_build(content, nonce, 0, 0, 0, out, cap);
}

int composer_payload_reply(const char *content, uint64_t nonce, uint64_t reply_id,
                           uint64_t reply_chan, char *out, size_t cap){
    return payload_build(content, nonce, reply_id, reply_chan, 0, out, cap);
}

int composer_payload_edit(const char *content, char *out, size_t cap){
    return payload_build(content, 0, 0, 0, 1, out, cap);
}
