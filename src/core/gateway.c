#include "gateway.h"
#include "events.h"
#include "jsonlite.h"
#include "snowflake.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct gateway {
    store *st;
    char token[512];
    char session_id[128];
    char self_name[64];
    uint64_t self_id;
    int seq;
    int state;
    int interval_ms;
    uint64_t last_beat_ms;
    int awaiting_ack;
    int reconnect_attempts;
    uint64_t reconnect_at;
    int reconnect_pending;
    int msg_sound;
    char voice_session[128];
    char voice_token[256];
    char voice_endpoint[128];
    uint64_t voice_guild;
    uint64_t voice_channel;
    char ack_buf[64];
};

static char *xstrdup(const char *s){
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if(p) memcpy(p, s, n);
    return p;
}

gateway *gw_new(store *st, const char *token){
    gateway *g = calloc(1, sizeof *g);
    if(!g) return NULL;
    g->st = st;
    g->interval_ms = 41250;
    snprintf(g->token, sizeof g->token, "%s", token ? token : "");
    return g;
}

void gw_free(gateway *g){ free(g); }

void gw_note_msg_sound(gateway *g, int mentions){
    if(!g) return;
    if(mentions) g->msg_sound = 2;
    else if(g->msg_sound == 0) g->msg_sound = 1;
}

int gw_take_msg_sound(gateway *g){
    int k;
    if(!g) return 0;
    k = g->msg_sound;
    g->msg_sound = 0;
    return k;
}

static jl_val_t *obj(jl_val_t *v, const char *k){
    return v && v->type == JL_OBJECT ? jl_obj_get(v, k) : NULL;
}
static const char *str(jl_val_t *v, const char *k){
    jl_val_t *x = obj(v, k);
    return x && x->type == JL_STRING ? x->str : "";
}
static uint64_t u64(jl_val_t *v, const char *k){
    jl_val_t *x = obj(v, k);
    return x && x->type == JL_STRING ? sn_parse(x->str) : 0;
}
static int num(jl_val_t *v, const char *k){
    jl_val_t *x = obj(v, k);
    return x && x->type == JL_NUMBER ? (int)x->num : 0;
}

int gw_feed(gateway *g, const char *json, size_t len){
    jl_val_t *v, *op, *d;
    int ev = GW_EV_NONE;
    if(!g || !json) return -1;
    v = jl_parse(json, len);
    if(!v) return -2;
    op = obj(v, "op");
    d = obj(v, "d");
    if(!op || op->type != JL_NUMBER){ jl_free(v); return -3; }
    switch((int)op->num){
    case 0: {
        const char *t = str(v, "t");
        jl_val_t *s = obj(v, "s");
        if(s && s->type == JL_NUMBER) g->seq = (int)s->num;
        if(t[0] && !strcmp(t, "RESUMED")){
            gw_on_ready(g, "", "", 0);   /* keeps the existing identity */
            ev = GW_EV_READY;
        } else if(d && t[0]){
            ev = events_dispatch(g, t, d);
        } else if(t[0] && !strcmp(t, "READY")){
            /* Oversized READY: the streamed envelope has no d, so identity
             * and session arrive separately through gw_set_identity() and
             * gw_set_session(). Still report READY so the UI leaves
             * "Loading..." and heartbeat/online state settles. */
            ev = GW_EV_READY;
        }
        break;
    }
    case 10:
        g->interval_ms = num(d, "heartbeat_interval");
        if(g->interval_ms <= 0) g->interval_ms = 41250;
        ev = GW_EV_HELLO;
        break;
    case 11:
        g->awaiting_ack = 0;
        break;
    case 7:
        ev = GW_EV_RECONNECT;
        break;
    case 9:
        g->seq = 0;
        g->state = 0;
        if(d && d->type == JL_BOOL && d->num == 0) g->session_id[0] = 0;
        ev = GW_EV_INVALID_SESSION;
        break;
    case 1:
        break;
    default:
        break;
    }
    jl_free(v);
    return ev;
}

const char *gw_self_name(gateway *g){ return g ? g->self_name : ""; }
uint64_t gw_self_id(gateway *g){ return g ? g->self_id : 0; }
int gw_interval_ms(gateway *g){ return g ? g->interval_ms : 41250; }
int gw_seq(gateway *g){ return g ? g->seq : 0; }
int gw_state(gateway *g){ return g ? g->state : 0; }
void gw_force_resume(gateway *g){
    /* Only a session from a live READY (state 1) or an in-flight RESUME
     * (state 2) may be retried. After op9 the state is 0, so the next
     * reconnect falls back to IDENTIFY instead of resuming a dead session. */
    if(g && g->session_id[0] && (g->state == 1 || g->state == 2))
        g->state = 2;
}
int gw_has_session(gateway *g){
    return g && g->session_id[0] != 0;
}
void gw_set_session(gateway *g, const char *session_id){
    if(!g || !session_id || !session_id[0]) return;
    snprintf(g->session_id, sizeof g->session_id, "%s", session_id);
    g->state = 1;
}
void gw_set_identity(gateway *g, uint64_t id, const char *name){
    if(!g) return;
    if(id) g->self_id = id;
    if(name && name[0])
        snprintf(g->self_name, sizeof g->self_name, "%s", name);
}

store *gw_store(gateway *g){ return g ? g->st : NULL; }

const char *gw_voice_session(gateway *g){
    return g ? g->voice_session : "";
}

uint64_t gw_voice_channel(gateway *g){
    return g ? g->voice_channel : 0;
}

uint64_t gw_voice_guild(gateway *g){
    return g ? g->voice_guild : 0;
}

const char *gw_voice_token(gateway *g){
    return g ? g->voice_token : "";
}

const char *gw_voice_endpoint(gateway *g){
    return g ? g->voice_endpoint : "";
}

void gw_set_voice_state(gateway *g, const char *session, uint64_t guild,
                        uint64_t chan){
    if(!g) return;
    snprintf(g->voice_session, sizeof g->voice_session, "%s",
             session ? session : "");
    g->voice_guild = guild;
    g->voice_channel = chan;
    if(!chan){
        g->voice_token[0] = 0;
        g->voice_endpoint[0] = 0;
    }
}

void gw_set_voice_server(gateway *g, const char *token, const char *endpoint,
                         uint64_t guild){
    if(!g) return;
    snprintf(g->voice_token, sizeof g->voice_token, "%s", token ? token : "");
    snprintf(g->voice_endpoint, sizeof g->voice_endpoint, "%s",
             endpoint ? endpoint : "");
    if(guild) g->voice_guild = guild;
}

void gw_on_ready(gateway *g, const char *session_id, const char *name, uint64_t id){
    if(!g) return;
    if(session_id && session_id[0])
        snprintf(g->session_id, sizeof g->session_id, "%s", session_id);
    if(name && name[0])
        snprintf(g->self_name, sizeof g->self_name, "%s", name);
    if(id) g->self_id = id;
    g->state = 1;
}

int gw_identify(gateway *g, char *out, size_t cap){
    int n;
    if(!g || !out) return 0;
    n = snprintf(out, cap,
        "{\"op\":2,\"d\":{\"token\":\"%s\",\"capabilities\":30717,"
        "\"properties\":{\"os\":\"Windows\",\"browser\":\"Chrome\",\"device\":\"\","
        "\"system_locale\":\"en-US\",\"browser_user_agent\":\"Mozilla/5.0\","
        "\"browser_version\":\"120.0.0.0\",\"os_version\":\"10\","
        "\"release_channel\":\"stable\",\"client_build_number\":250000},"
        "\"presence\":{\"status\":\"online\",\"since\":0,\"activities\":[],\"afk\":false},"
        "\"compress\":false}}", g->token);
    return n > 0 && (size_t)n < cap ? n : 0;
}

int gw_presence_payload(const char *status, char *out, size_t cap){
    int n;
    if(!out || !status || !*status) return 0;
    n = snprintf(out, cap,
                 "{\"op\":3,\"d\":{\"status\":\"%s\",\"since\":0,"
                 "\"activities\":[],\"afk\":false}}", status);
    return n > 0 && (size_t)n < cap ? n : 0;
}

int gw_resume(gateway *g, char *out, size_t cap){
    int n;
    if(!g || !out || !g->session_id[0]) return 0;
    n = snprintf(out, cap,
        "{\"op\":6,\"d\":{\"token\":\"%s\",\"session_id\":\"%s\",\"seq\":%d}}",
        g->token, g->session_id, g->seq);
    return n > 0 && (size_t)n < cap ? n : 0;
}

const char *gw_ack(gateway *g, size_t *len){
    if(!g) return NULL;
    snprintf(g->ack_buf, sizeof g->ack_buf, "{\"op\":1,\"d\":%d}", g->seq > 0 ? g->seq : 0);
    if(len) *len = strlen(g->ack_buf);
    return g->ack_buf;
}

int gw_expired_heartbeat(gateway *g){ return g ? g->awaiting_ack : 0; }

uint64_t gw_backoff_ms(int attempt){
    uint64_t ms;
    if(attempt <= 0) return 0;
    ms = 1000ULL << (attempt - 1);
    return ms > 30000ULL ? 30000ULL : ms;
}

void gw_schedule_reconnect(gateway *g, uint64_t now_ms){
    if(!g) return;
    g->reconnect_at = now_ms + gw_backoff_ms(g->reconnect_attempts);
    g->reconnect_pending = 1;
}

int gw_reconnect_due(gateway *g, uint64_t now_ms){
    if(!g || !g->reconnect_pending) return 0;
    return now_ms >= g->reconnect_at;
}

void gw_on_reconnect_failed(gateway *g, uint64_t now_ms){
    if(!g) return;
    g->reconnect_attempts++;
    g->reconnect_at = now_ms + gw_backoff_ms(g->reconnect_attempts);
    g->reconnect_pending = 1;
}

void gw_on_reconnected(gateway *g){
    if(!g) return;
    g->reconnect_attempts = 0;
    g->reconnect_at = 0;
    g->reconnect_pending = 0;
    g->awaiting_ack = 0;
}

int gw_ack_expired(gateway *g, uint64_t now_ms){
    if(!g || !g->awaiting_ack) return 0;
    return now_ms - g->last_beat_ms >= (uint64_t)g->interval_ms;
}
int gw_heartbeat_due(gateway *g, uint64_t now_ms){
    if(!g) return 0;
    return now_ms - g->last_beat_ms >= (uint64_t)g->interval_ms;
}
void gw_mark_beat(gateway *g, uint64_t now_ms){
    if(!g) return;
    g->last_beat_ms = now_ms;
    g->awaiting_ack = 1;
}

/* helpers shared with events.c */
const char *gw_string(jl_val_t *v, const char *k){ return str(v, k); }
uint64_t gw_u64(jl_val_t *v, const char *k){ return u64(v, k); }
int gw_num(jl_val_t *v, const char *k){ return num(v, k); }
jl_val_t *gw_obj(jl_val_t *v, const char *k){ return obj(v, k); }

char *gw_strdup(const char *s){ return xstrdup(s ? s : ""); }
