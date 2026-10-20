/* ws_env.c - see ws_env.h. Pure; no sockets, no TLS. */
#include "ws_env.h"
#include <string.h>
#include <stdio.h>

void ws_env_init(ws_env_t *e){
    if(!e) return;
    memset(e, 0, sizeof *e);
}

/* Copy the value of a depth-1 string key out of a JSON prefix.
 *
 * Depth matters: Discord's envelope is {"op":..,"s":..,"t":..,"d":{..}}, and
 * "d" contains keys that collide with ours ("s" for session id, "t" for
 * timestamps). Only depth 1 counts, so those nested ones are never matched.
 *
 * Returns the value length, or 0 when the key is absent OR its value is not
 * yet fully buffered. That second case is the subtle one: fed one byte at a
 * time, a naive scan captures "4" out of "41" and then stops rescanning,
 * which is how "s":41 came back as "s":4. A value counts only once a real
 * terminator has been seen -- closing quote for strings, comma or brace for
 * numbers. Everything else is retried on the next feed. */
static size_t copy_field(const char *w, size_t n, const char *key,
                         char *out, size_t out_cap){
    if(!w || !key || !out || out_cap == 0) return 0;
    size_t klen = strlen(key);
    int depth = 0, instr = 0, esc = 0;

    for(size_t i = 0; i < n; i++){
        char c = w[i];
        if(instr){
            if(esc) esc = 0;
            else if(c == '\\') esc = 1;
            else if(c == '"') instr = 0;
            continue;
        }
        if(c == '"'){
            /* Candidate key: "key" then optional space then ':' */
            if(depth == 1 &&
               i + klen + 2 <= n &&
               !memcmp(w + i + 1, key, klen) &&
               w[i + 1 + klen] == '"'){
                size_t j = i + klen + 2;
                while(j < n && (w[j] == ' ' || w[j] == '\t')) j++;
                if(j < n && w[j] == ':'){
                    j++;
                    while(j < n && (w[j] == ' ' || w[j] == '\t')) j++;
                    size_t o = 0;
                    int done = 0;
                    if(j < n && w[j] == '"'){
                        /* string value; escapes copied verbatim */
                        j++;
                        while(j < n && o + 1 < out_cap){
                            if(w[j] == '"'){ done = 1; break; }
                            if(w[j] == '\\' && j + 1 < n) out[o++] = w[j++];
                            out[o++] = w[j++];
                        }
                    } else {
                        /* number / literal: must reach a delimiter first */
                        while(j < n && o + 1 < out_cap){
                            if(w[j] == ',' || w[j] == '}'){ done = 1; break; }
                            out[o++] = w[j++];
                        }
                    }
                    if(!done) return 0;   /* value runs past the window */
                    out[o] = 0;
                    return o;
                }
            }
            instr = 1;
            continue;
        }
        if(c == '{' || c == '[') depth++;
        else if(c == '}' || c == ']') depth--;
    }
    return 0;
}

static char *auth_dst(ws_env_t *e, size_t *cap){
    if(e->target == 1){ *cap = sizeof e->session; return e->session; }
    if(e->target == 2){ *cap = sizeof e->user_id; return e->user_id; }
    if(e->target == 3){ *cap = sizeof e->user_name; return e->user_name; }
    *cap = 0;
    return NULL;
}

static void auth_done(ws_env_t *e){
    if(e->target == 1) e->have_session = e->session[0] != 0;
    else if(e->target == 2) e->have_user_id = e->user_id[0] != 0;
    else if(e->target == 3) e->have_user_name = e->user_name[0] != 0;
}

/* Track JSON depth over the whole payload and pull d.session_id plus the
 * d.user id/username out of READY. A plain substring search would match the
 * session_id inside guild voice_states, so keys only count at the depth of
 * the d object (2) or of d.user's fields (3). */
static void auth_scan(ws_env_t *e, const unsigned char *p, size_t n){
    size_t i;
    if(e->have_session && e->have_user_id && e->have_user_name) return;
    for(i = 0; i < n; i++){
        char c = (char)p[i];
        if(e->instr){
            size_t cap;
            char *dst;
            if(e->esc){
                e->esc = 0;
                if(e->val_active){
                    dst = auth_dst(e, &cap);
                    if(dst && e->val_len + 1 < cap) dst[e->val_len++] = c;
                }
                continue;
            }
            if(c == '\\'){ e->esc = 1; continue; }
            if(c == '"'){
                e->instr = 0;
                if(e->key_active){
                    e->key_active = 0;
                    e->key[e->key_len] = 0;
                    if(e->depth == 2 && !strcmp(e->key, "session_id")){
                        e->target = 1;
                        e->key_match = 1;
                    } else if(e->depth == 2 && !strcmp(e->key, "user")){
                        e->user_pending = 1;
                    } else if(e->depth == 3 && e->user_active &&
                              !strcmp(e->key, "id")){
                        e->target = 2;
                        e->key_match = 1;
                    } else if(e->depth == 3 && e->user_active &&
                              !strcmp(e->key, "username")){
                        e->target = 3;
                        e->key_match = 1;
                    }
                } else if(e->val_active){
                    size_t cap;
                    char *dst = auth_dst(e, &cap);
                    if(dst && e->val_len < cap) dst[e->val_len] = 0;
                    e->val_active = 0;
                    auth_done(e);
                }
                continue;
            }
            if(e->key_active){
                if(e->key_len + 1 < sizeof e->key)
                    e->key[e->key_len++] = c;
            } else if(e->val_active){
                size_t cap;
                char *dst = auth_dst(e, &cap);
                if(dst && e->val_len + 1 < cap) dst[e->val_len++] = c;
            }
            continue;
        }
        if(c == '"'){
            e->instr = 1;
            if(e->want_value){
                e->want_value = 0;
                e->val_active = 1;
                e->val_len = 0;
                {
                    size_t cap;
                    char *dst = auth_dst(e, &cap);
                    if(dst) dst[0] = 0;
                }
            } else {
                e->key_active = 1;
                e->key_len = 0;
            }
            continue;
        }
        if(c == '{' || c == '['){
            if(e->user_pending && c == '{' && e->depth == 2){
                e->user_active = 1;
                e->user_depth = e->depth + 1;
                e->user_pending = 0;
            }
            e->depth++;
            e->key_match = 0;
            e->want_value = 0;
        } else if(c == '}' || c == ']'){
            if(e->user_active && e->depth == e->user_depth)
                e->user_active = 0;
            e->depth--;
            if(e->depth < 0) e->depth = 0;
            e->key_match = 0;
            e->want_value = 0;
            e->user_pending = 0;
        } else if(c == ':'){
            if(e->key_match) e->want_value = 1;
        } else if(c == ','){
            e->key_match = 0;
            e->want_value = 0;
            e->user_pending = 0;
        }
    }
}

void ws_env_feed(ws_env_t *e, const unsigned char *p, size_t n){
    if(!e || !p || n == 0) return;
    auth_scan(e, p, n);
    /* Retain a bounded prefix. Once saturated, further bytes are discarded,
     * which is the whole point: memory does not track payload size. */
    if(e->winlen < WS_ENV_WINDOW){
        size_t room = WS_ENV_WINDOW - e->winlen;
        size_t take = n < room ? n : room;
        memcpy(e->win + e->winlen, p, take);
        e->winlen += take;
    }
    /* Rescan the whole window each feed so a key split across a chunk
     * boundary still resolves. copy_field only returns a complete value. */
    if(!e->have_op && copy_field(e->win, e->winlen, "op", e->op, sizeof e->op))
        e->have_op = 1;
    if(!e->have_s && copy_field(e->win, e->winlen, "s", e->s, sizeof e->s))
        e->have_s = 1;
    if(!e->have_t && copy_field(e->win, e->winlen, "t", e->t, sizeof e->t))
        e->have_t = 1;
}

int ws_env_complete(const ws_env_t *e){
    if(!e) return 0;
    /* op and t are what the callers read. Requiring "s" as well meant a
     * single unmatched field silently blocked the report for the whole
     * 5.8 MB drain -- the timeout seen on console 2026-10-09. gw_seq()
     * treats a missing "s" as "not a sequence" and returns quietly, so
     * dropping it from the requirement is safe. */
    if(e->have_op && e->have_t) return 1;
    /* Window saturated: nothing further can ever match, so waiting out the
     * rest of the frame would not change the result. */
    return e->winlen >= WS_ENV_WINDOW;
}

size_t ws_env_render(const ws_env_t *e, char *out, size_t cap){
    if(!e || !out || cap == 0) return 0;
    char tmp[192];
    size_t t = 0;
    tmp[0] = 0;
    /* The braces are load-bearing. discord.c's top_val() only returns keys at
     * JSON depth 1, and depth is counted by '{' and '['. Emitting a bare
     * "op":0,"t":"READY" left depth at 0, so is_ready() never matched and the
     * handshake timed out even though the envelope arrived in one second:
     * "ws: envelope reported early" immediately followed by "no READY after
     * identify (timeout)" on console 2026-10-09.
     *
     * "d" is deliberately absent -- it is the megabytes nothing here reads. */
    /* Nothing captured: report 0 so the caller keeps its "frame skipped"
     * path. Rendering "{}" here would hand back a 2-byte pseudo-frame that no
     * gateway event ever looks like. */
    if(!e->have_op && !e->have_s && !e->have_t) return 0;

    if(e->have_op && t + 24 < sizeof tmp)
        t += (size_t)snprintf(tmp + t, sizeof tmp - t, "{\"op\":%s,", e->op);
    else
        t += (size_t)snprintf(tmp + t, sizeof tmp - t, "{");
    if(e->have_s && t + 24 < sizeof tmp)
        t += (size_t)snprintf(tmp + t, sizeof tmp - t, "\"s\":%s,", e->s);
    if(e->have_t && t + 80 < sizeof tmp)
        t += (size_t)snprintf(tmp + t, sizeof tmp - t, "\"t\":\"%s\",", e->t);
    if(t == 0) return 0;
    if(tmp[t - 1] == ',') t--;          /* drop trailing comma */
    if(t + 2 >= sizeof tmp) return 0;
    tmp[t++] = '}';                     /* close the object */
    if(t >= cap) return 0;              /* caller's buffer too small */
    memcpy(out, tmp, t);
    out[t] = 0;
    return t;
}
