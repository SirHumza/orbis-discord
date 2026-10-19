#include "voice.h"
#include "jsonlite.h"
#include "snowflake.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jl_val_t *obj(jl_val_t *v, const char *k){
    return v && v->type == JL_OBJECT ? jl_obj_get(v, k) : NULL;
}

int voice_state_json(uint64_t guild, uint64_t chan, char *out, size_t cap){
    int n;
    char chanbuf[32], guildbuf[32];
    if(!out || !cap) return 0;
    snprintf(guildbuf, sizeof guildbuf, "\"%llu\"",
             (unsigned long long)guild);
    if(chan)
        snprintf(chanbuf, sizeof chanbuf, "\"%llu\"",
                 (unsigned long long)chan);
    else
        snprintf(chanbuf, sizeof chanbuf, "null");
    n = snprintf(out, cap,
                 "{\"op\":4,\"d\":{\"guild_id\":%s,\"channel_id\":%s,"
                 "\"self_mute\":false,\"self_deaf\":false}}",
                 guild ? guildbuf : "null", chanbuf);
    return n > 0 && (size_t)n < cap ? n : 0;
}

int voice_identify_json(uint64_t guild, uint64_t user, const char *session,
                        const char *token, char *out, size_t cap){
    int n;
    if(!out || !session || !token) return 0;
    n = snprintf(out, cap,
                 "{\"op\":0,\"d\":{\"server_id\":\"%llu\",\"user_id\":\"%llu\","
                 "\"session_id\":\"%s\",\"token\":\"%s\",\"video\":false}}",
                 (unsigned long long)guild, (unsigned long long)user,
                 session, token);
    return n > 0 && (size_t)n < cap ? n : 0;
}

int voice_select_json(const char *ip, int port, const char *mode,
                      char *out, size_t cap){
    int n;
    if(!out || !ip || !mode) return 0;
    n = snprintf(out, cap,
                 "{\"op\":1,\"d\":{\"protocol\":\"udp\",\"data\":{"
                 "\"address\":\"%s\",\"port\":%d,\"mode\":\"%s\"}}}",
                 ip, port, mode);
    return n > 0 && (size_t)n < cap ? n : 0;
}

int voice_speaking_json(uint32_t ssrc, int speaking, char *out, size_t cap){
    int n;
    if(!out) return 0;
    n = snprintf(out, cap, "{\"op\":5,\"d\":{\"speaking\":%d,\"delay\":0,"
                           "\"ssrc\":%u}}",
                 speaking ? 1 : 0, (unsigned)ssrc);
    return n > 0 && (size_t)n < cap ? n : 0;
}

int voice_heartbeat_json(uint64_t nonce, char *out, size_t cap){
    int n;
    if(!out) return 0;
    n = snprintf(out, cap, "{\"op\":3,\"d\":%llu}",
                 (unsigned long long)nonce);
    return n > 0 && (size_t)n < cap ? n : 0;
}

static int mode_flag(const char *m){
    if(!strcmp(m, "aead_xchacha20_poly1305_rtpsize")) return VOICE_MODE_XCHACHA;
    if(!strcmp(m, "xsalsa20_poly1305")) return VOICE_MODE_XSALSA;
    if(!strcmp(m, "aead_aes256_gcm_rtpsize")) return VOICE_MODE_AES256;
    return 0;
}

int voice_ready_parse(const char *json, voice_ready *out){
    jl_val_t *v, *d, *j, *m;
    size_t i, cnt;
    if(!json || !out) return 0;
    v = jl_parse(json, strlen(json));
    if(!v) return 0;
    d = obj(v, "d");
    if(!d){ jl_free(v); return 0; }
    memset(out, 0, sizeof *out);
    j = obj(d, "ssrc");
    if(!j || j->type != JL_NUMBER){ jl_free(v); return 0; }
    out->ssrc = (uint32_t)j->num;
    j = obj(d, "ip");
    if(j && j->type == JL_STRING)
        snprintf(out->ip, sizeof out->ip, "%s", j->str);
    j = obj(d, "port");
    if(j && j->type == JL_NUMBER) out->port = (int)j->num;
    m = obj(d, "modes");
    if(m && m->type == JL_ARRAY)
        for(i = 0, cnt = (size_t)m->count; i < cnt; i++){
            jl_val_t *e = jl_arr_at(m, i);
            if(e && e->type == JL_STRING) out->modes |= mode_flag(e->str);
        }
    jl_free(v);
    return out->ssrc && out->ip[0] && out->port > 0;
}

int voice_session_parse(const char *json, char *mode, size_t mode_cap,
                        unsigned char *key, size_t key_cap, size_t *key_len){
    jl_val_t *v, *d, *j, *k;
    size_t i, n = 0, cnt;
    if(!json || !mode || !mode_cap || !key || !key_len) return 0;
    v = jl_parse(json, strlen(json));
    if(!v) return 0;
    d = obj(v, "d");
    if(!d){ jl_free(v); return 0; }
    j = obj(d, "mode");
    if(j && j->type == JL_STRING)
        snprintf(mode, mode_cap, "%s", j->str);
    else
        mode[0] = 0;
    k = obj(d, "secret_key");
    if(k && k->type == JL_ARRAY){
        for(i = 0, cnt = (size_t)k->count; i < cnt && n < key_cap; i++){
            jl_val_t *e = jl_arr_at(k, i);
            if(e && e->type == JL_NUMBER) key[n++] = (unsigned char)e->num;
        }
    }
    *key_len = n;
    jl_free(v);
    return n > 0;
}

const char *voice_pick_mode(int modes){
    if(modes & VOICE_MODE_XCHACHA) return "aead_xchacha20_poly1305_rtpsize";
    if(modes & VOICE_MODE_XSALSA) return "xsalsa20_poly1305";
    if(modes & VOICE_MODE_AES256) return "aead_aes256_gcm_rtpsize";
    return "";
}

int voice_discovery_packet(uint32_t ssrc, unsigned char *out, size_t cap){
    if(!out || cap < 74) return 0;
    memset(out, 0, 74);
    out[0] = 0; out[1] = 1;
    out[2] = 0; out[3] = 70;
    out[4] = (unsigned char)(ssrc >> 24);
    out[5] = (unsigned char)(ssrc >> 16);
    out[6] = (unsigned char)(ssrc >> 8);
    out[7] = (unsigned char)ssrc;
    return 74;
}

int voice_discovery_parse(const unsigned char *buf, size_t len, char *ip,
                          size_t ip_cap, int *port){
    size_t i, max;
    if(!buf || len < 74 || !ip || !ip_cap || !port) return 0;
    if(buf[0] != 0 || buf[1] != 2) return 0;
    max = ip_cap - 1;
    if(max > 63) max = 63;
    for(i = 0; i < max && buf[8 + i]; i++) ip[i] = (char)buf[8 + i];
    ip[i] = 0;
    *port = (buf[72] << 8) | buf[73];
    return ip[0] && *port;
}

int voice_endpoint_split(const char *endpoint, char *host, size_t host_cap,
                         int *port){
    const char *p, *colon;
    size_t n;
    if(!endpoint || !host || !host_cap || !port) return 0;
    p = endpoint;
    if(!strncmp(p, "wss://", 6)) p += 6;
    colon = strchr(p, ':');
    n = colon ? (size_t)(colon - p) : strcspn(p, "/");
    if(!n || n >= host_cap) return 0;
    memcpy(host, p, n);
    host[n] = 0;
    *port = 443;
    if(colon && colon[1]){
        char *end = NULL;
        long v = strtol(colon + 1, &end, 10);
        if(v > 0 && v < 65536) *port = (int)v;
    }
    return 1;
}
