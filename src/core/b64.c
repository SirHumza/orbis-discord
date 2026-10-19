/* b64.c - standard base64 encoder */
#include "b64.h"
static const char b64t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
size_t b64_encode(const unsigned char *in, size_t len, char *out){
    size_t o=0, i;
    if(!out || (len && !in)) return 0;
    for(i=0;i<len;i+=3){
        unsigned char b0=in[i], b1=(i+1<len)?in[i+1]:0, b2=(i+2<len)?in[i+2]:0;
        unsigned v=((unsigned)b0<<16)|((unsigned)b1<<8)|b2;
        out[o++]=b64t[(v>>18)&0x3f];
        out[o++]=b64t[(v>>12)&0x3f];
        out[o++]=i+1<len?b64t[(v>>6)&0x3f]:'=';
        out[o++]=i+2<len?b64t[v&0x3f]:'=';
    }
    out[o]=0; /* NUL-terminate so callers can use it as a C string */
    return o;
}
static int b64val(char c){
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='a'&&c<='z') return c-'a'+26;
    if(c>='0'&&c<='9') return c-'0'+52;
    if(c=='+') return 62;
    if(c=='/') return 63;
    return -1;
}
size_t b64_decode(const char *in, unsigned char *out, size_t cap){
    unsigned acc=0; int bits=0; size_t o=0;
    if(!in || !out) return (size_t)-1;
    for(;*in;in++){
        char c=*in;
        int v;
        if(c=='=' ) break;
        if(c=='\r'||c=='\n'||c==' '||c=='\t') continue;
        v=b64val(c);
        if(v<0) return (size_t)-1;
        acc=(acc<<6)|(unsigned)v; bits+=6;
        if(bits>=8){
            bits-=8;
            if(o>=cap) return (size_t)-1;
            out[o++]=(unsigned char)((acc>>bits)&0xff);
        }
    }
    return o;
}
