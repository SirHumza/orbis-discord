#include "ime.h"
#include <string.h>

size_t ime_utf16_to_utf8(const uint16_t *in, size_t in_len, char *out, size_t cap){
    size_t i, n = 0;
    if(!out || cap == 0) return 0;
    out[0] = 0;
    if(!in) return 0;
    for(i = 0; i < in_len; i++){
        uint32_t cp = in[i];
        unsigned char buf[4];
        size_t len;
        if(cp >= 0xD800 && cp <= 0xDBFF){
            if(i + 1 < in_len && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF){
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (in[i + 1] - 0xDC00u);
                i++;
            } else {
                cp = 0xFFFD;
            }
        } else if(cp >= 0xDC00 && cp <= 0xDFFF){
            cp = 0xFFFD;
        }
        if(cp < 0x80){
            buf[0] = (unsigned char)cp;
            len = 1;
        } else if(cp < 0x800){
            buf[0] = (unsigned char)(0xC0 | (cp >> 6));
            buf[1] = (unsigned char)(0x80 | (cp & 0x3F));
            len = 2;
        } else if(cp < 0x10000){
            buf[0] = (unsigned char)(0xE0 | (cp >> 12));
            buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            buf[2] = (unsigned char)(0x80 | (cp & 0x3F));
            len = 3;
        } else {
            buf[0] = (unsigned char)(0xF0 | (cp >> 18));
            buf[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            buf[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            buf[3] = (unsigned char)(0x80 | (cp & 0x3F));
            len = 4;
        }
        if(n + len >= cap) break;
        memcpy(out + n, buf, len);
        n += len;
        out[n] = 0;
    }
    return n;
}
