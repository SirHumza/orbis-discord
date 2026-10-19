/* screen_main.c - main layout shell + live gateway session.
 *
 * Bulk state (guilds/channels) comes from REST: a real account's READY and
 * GUILD_CREATE frames exceed WS_BODY_MAX and are streamed to an envelope
 * (op/s/t only), so the store is bootstrapped over HTTP instead. The gateway
 * is kept for realtime deltas (messages, presences) and heartbeats.
 *
 * No RESUME: session_id lives past the oversized guilds array in READY and is
 * lost to the envelope. Reconnects re-IDENTIFY, which Discord accepts. */
#include "screens.h"
#include "renderer.h"
#include "theme.h"
#include "nav.h"
#include "../core/ws.h"
#include "../core/jsonlite.h"
#include "../core/clock.h"
#include "../core/log.h"
#include "../model/snowflake.h"
#include "../platform/input.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GW_HOST "gateway.discord.gg"
#define GW_RESOURCE "/?v=9&encoding=json"
#define GW_RECV 65536

#define SCR_W 1920
#define SCR_H 1080
#define RAIL_W 72
#define CHAN_W 240
#define MEMBER_W 240
#define CHAT_X (RAIL_W + CHAN_W)
#define CHAT_W (SCR_W - CHAT_X - MEMBER_W)
#define MEMBER_X (SCR_W - MEMBER_W)
#define HEADER_H 48
#define CHAN_ROW_H 34
#define CHAN_TOP 72
#define GUILD_PITCH 56
#define GUILD_VIS 18
#define CHAN_VIS 27

typedef struct {
    const guild *guilds[64];
    int ng;
    int gi;
    const channel *text[64];
    int nt;
    int ci;
    const channel *active;
} frame_view;

typedef struct {
    app_state *app;
    ws_t ws;
    char *recv;
    int up;
    int identified;
    int guilds_loaded;
    int guilds_pending;
    uint64_t guilds_retry_at;
    uint64_t chan_guild;
    int chan_pending;
    nav gn, cn;
    int focus;
    char status[64];
} main_ctx;

typedef struct {
    main_ctx *ctx;
    uint64_t guild;
} chan_fetch;

static uint64_t mono_ms(void){
    return (uint64_t)orbis_mono_s() * 1000u;
}

static void set_status(main_ctx *L, const char *s){
    snprintf(L->status, sizeof L->status, "%s", s);
}

static void first_codepoint(const char *s, char *out, size_t cap){
    size_t len, n = 1;
    if(!out || cap == 0) return;
    out[0] = 0;
    if(!s || !*s || cap < 2) return;
    len = strlen(s);
    if((unsigned char)s[0] >= 0x80){
        if(((unsigned char)s[0] & 0xE0) == 0xC0) n = 2;
        else if(((unsigned char)s[0] & 0xF0) == 0xE0) n = 3;
        else n = 4;
    }
    if(n > len) n = len;
    if(n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
}

static void trunc_utf8(char *dst, size_t cap, const char *src, size_t max_bytes){
    size_t n;
    if(!dst || cap == 0) return;
    n = src ? strlen(src) : 0;
    if(n > max_bytes) n = max_bytes;
    while(n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    if(n >= cap) n = cap - 1;
    if(n) memcpy(dst, src, n);
    dst[n] = 0;
}

static void guilds_cb(int status, const char *body, void *ud){
    main_ctx *L = ud;
    jl_val_t *v;
    int i;
    L->guilds_pending = 0;
    if(status != 200){
        log_msg("rest: guilds failed (%d)", status);
        L->guilds_retry_at = mono_ms() + 5000;
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    if(!v || v->type != JL_ARRAY){
        log_msg("rest: guilds bad payload");
        jl_free(v);
        L->guilds_retry_at = mono_ms() + 5000;
        return;
    }
    for(i = 0; i < v->count; i++){
        jl_val_t *o = jl_arr_at(v, i);
        jl_val_t *id = o ? jl_obj_get(o, "id") : NULL;
        jl_val_t *name = o ? jl_obj_get(o, "name") : NULL;
        guild g;
        if(!id || id->type != JL_STRING) continue;
        memset(&g, 0, sizeof g);
        g.id = sn_parse(id->str);
        if(name && name->type == JL_STRING)
            snprintf(g.name, sizeof g.name, "%s", name->str);
        store_put_guild(L->app->st, &g);
    }
    log_msg("rest: %d guilds", v->count);
    jl_free(v);
    L->guilds_loaded = 1;
}

static void channels_cb(int status, const char *body, void *ud){
    chan_fetch *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v;
    int i;
    L->chan_pending = 0;
    if(status != 200){
        log_msg("rest: channels failed (%d)", status);
        L->chan_guild = 0;
        free(f);
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    if(v && v->type == JL_ARRAY){
        for(i = 0; i < v->count; i++){
            jl_val_t *o = jl_arr_at(v, i);
            jl_val_t *id = o ? jl_obj_get(o, "id") : NULL;
            jl_val_t *type = o ? jl_obj_get(o, "type") : NULL;
            jl_val_t *pos = o ? jl_obj_get(o, "position") : NULL;
            jl_val_t *name = o ? jl_obj_get(o, "name") : NULL;
            jl_val_t *nsfw = o ? jl_obj_get(o, "nsfw") : NULL;
            channel ch;
            if(!id || id->type != JL_STRING) continue;
            if(type && type->type == JL_NUMBER && (int)type->num != 0 && (int)type->num != 2)
                continue;
            memset(&ch, 0, sizeof ch);
            ch.id = sn_parse(id->str);
            ch.guild = f->guild;
            if(type && type->type == JL_NUMBER) ch.type = (int)type->num;
            if(pos && pos->type == JL_NUMBER) ch.position = (int)pos->num;
            if(name && name->type == JL_STRING)
                snprintf(ch.name, sizeof ch.name, "%s", name->str);
            if(nsfw && nsfw->type == JL_BOOL) ch.nsfw = (int)nsfw->num;
            store_put_channel(L->app->st, &ch);
        }
        log_msg("rest: %d channels for %llu", v->count,
                (unsigned long long)f->guild);
    }
    jl_free(v);
    free(f);
}

static void fetch_guilds(main_ctx *L){
    L->guilds_pending = 1;
    if(rest_send(L->app->rest, "GET", "/api/v9/users/@me/guilds", NULL,
                 guilds_cb, L) != 0)
        L->guilds_pending = 0;
}

static void fetch_channels(main_ctx *L, uint64_t guild_id){
    chan_fetch *f = calloc(1, sizeof *f);
    char path[96];
    if(!f){ L->chan_guild = 0; return; }
    f->ctx = L;
    f->guild = guild_id;
    snprintf(path, sizeof path, "/api/v9/guilds/%llu/channels",
             (unsigned long long)guild_id);
    L->chan_pending = 1;
    if(rest_send(L->app->rest, "GET", path, NULL, channels_cb, f) != 0){
        L->chan_pending = 0;
        L->chan_guild = 0;
        free(f);
    }
}

static void gw_drop(main_ctx *L, uint64_t now, const char *why){
    app_state *app = L->app;
    if(L->up){
        ws_close(&L->ws);
        L->up = 0;
    }
    L->identified = 0;
    L->chan_pending = 0;
    L->chan_guild = 0;
    gw_on_reconnect_failed(app->gw, now);
    set_status(L, "Reconnecting...");
    log_msg("gw: dropped (%s)", why);
}

static void view_build(main_ctx *L, frame_view *v){
    uint64_t cids[64];
    size_t nc = 0, i;
    v->ng = (int)store_guilds(L->app->st, v->guilds, 64);
    nav_step(&L->gn, IE_NONE, v->ng);
    v->gi = nav_index(&L->gn);
    v->nt = 0;
    v->active = NULL;
    if(v->ng > 0)
        nc = store_channels_for_guild(L->app->st, v->guilds[v->gi]->id, cids, 64);
    for(i = 0; i < nc && v->nt < 64; i++){
        const channel *c = store_channel(L->app->st, cids[i]);
        if(c && c->type == 0) v->text[v->nt++] = c;
    }
    nav_step(&L->cn, IE_NONE, v->nt);
    v->ci = nav_index(&L->cn);
    if(v->nt > 0) v->active = v->text[v->ci];
}

static void draw_main(main_ctx *L, const frame_view *v){
    char buf[160];
    int i, first, y;

    ui_rect(0, 0, SCR_W, SCR_H, UI_CHAT_BG);

    ui_rect(0, 0, RAIL_W, SCR_H, UI_RAIL);
    first = v->gi - GUILD_VIS / 2;
    if(first < 0) first = 0;
    if(first > v->ng - GUILD_VIS) first = v->ng - GUILD_VIS;
    if(first < 0) first = 0;
    for(i = first; i < v->ng && i < first + GUILD_VIS; i++){
        int sel = i == v->gi;
        y = 16 + (i - first) * GUILD_PITCH;
        ui_rrect(12, y, 48, 48, sel ? 10 : 24, sel ? UI_BLURPLE : UI_CHANNEL_BG);
        if(sel) ui_rect(0, y + 12, 4, 24, UI_TEXT);
        first_codepoint(v->guilds[i]->name, buf, sizeof buf);
        ui_text(36 - ui_text_width(buf, 20) / 2, y + 13, buf, 0xFFFFFFFFu, 20);
    }

    ui_rect(RAIL_W, 0, CHAN_W, SCR_H, UI_CHANNEL_BG);
    if(v->ng > 0){
        trunc_utf8(buf, sizeof buf, v->guilds[v->gi]->name, 22);
        ui_text(RAIL_W + 16, 20, buf, UI_TEXT, 18);
    }
    ui_rect(RAIL_W, 56, CHAN_W, 1, UI_SEP);

    first = v->ci - CHAN_VIS / 2;
    if(first < 0) first = 0;
    if(first > v->nt - CHAN_VIS) first = v->nt - CHAN_VIS;
    if(first < 0) first = 0;
    for(i = first; i < v->nt && i < first + CHAN_VIS; i++){
        int sel = i == v->ci;
        y = CHAN_TOP + (i - first) * CHAN_ROW_H;
        if(sel) ui_rrect(RAIL_W + 8, y, CHAN_W - 16, CHAN_ROW_H - 4, 4, UI_SEP);
        snprintf(buf, sizeof buf, "# %s", v->text[i]->name);
        trunc_utf8(buf, sizeof buf, buf, 26);
        ui_text(RAIL_W + 20, y + 7, buf, sel ? UI_TEXT : UI_TEXT_DIM, 16);
    }

    ui_rect(CHAT_X, 0, CHAT_W, SCR_H, UI_CHAT_BG);
    ui_rect(CHAT_X, 0, CHAT_W, HEADER_H, UI_HEADER_BG);
    ui_rect(CHAT_X, HEADER_H, CHAT_W, 1, UI_SEP);
    if(v->active){
        snprintf(buf, sizeof buf, "# %s", v->active->name);
        trunc_utf8(buf, sizeof buf, buf, 48);
        ui_text(CHAT_X + 16, 14, buf, UI_TEXT, 18);
    }
    {
        const char *hint = v->active ? "Message history lands with Task 10"
                                     : "Select a text channel";
        ui_text(CHAT_X + (CHAT_W - ui_text_width(hint, 16)) / 2, 520,
                hint, UI_TEXT_DIM, 16);
    }

    ui_rect(MEMBER_X, 0, MEMBER_W, SCR_H, UI_CHANNEL_BG);
    ui_rect(MEMBER_X, 0, 1, SCR_H, UI_SEP);
    ui_text(MEMBER_X + 16, 20, "Members", UI_TEXT_DIM, 14);

    {
        uint32_t dot = !L->up ? UI_OFFLINE : L->identified ? UI_ONLINE : UI_IDLE;
        ui_rect(RAIL_W + 16, SCR_H - 30, 8, 8, dot);
        ui_text(RAIL_W + 32, SCR_H - 36, L->status, UI_TEXT_DIM, 12);
    }
}

int screen_main_run(app_state *st){
    main_ctx L;
    frame_view v;
    input_init();
    memset(&L, 0, sizeof L);
    L.app = st;
    L.recv = malloc(GW_RECV);
    if(!L.recv){
        log_msg("main: recv alloc fail");
        return 0;
    }
    nav_init(&L.gn);
    nav_init(&L.cn);
    set_status(&L, "Connecting...");
    gw_schedule_reconnect(st->gw, 0);
    log_msg("main: screen up");

    for(;;){
        uint64_t now = mono_ms();
        gateway *g = st->gw;

        if(!L.up && gw_reconnect_due(g, now)){
            char key[32];
            if(ws_client_key(key, sizeof key) > 0 &&
               ws_connect(&L.ws, GW_HOST, 443, GW_RESOURCE, key, NULL) == 0){
                L.up = 1;
                L.identified = 0;
                L.guilds_loaded = 0;
                L.guilds_pending = 0;
                L.chan_guild = 0;
                L.chan_pending = 0;
                gw_on_reconnected(g);
                set_status(&L, "Identifying...");
                log_msg("gw: connected");
            } else {
                gw_on_reconnect_failed(g, now);
                set_status(&L, "Reconnecting...");
                log_msg("gw: connect failed");
            }
        }

        if(L.up){
            int n, op, fin, frames = 0;
            while(frames < 64 &&
                  (n = ws_recv_frame(&L.ws, L.recv, GW_RECV - 1, &op, &fin)) != 0){
                int ev;
                if(n < 0) break;
                frames++;
                if(op == 9){ ws_pong(&L.ws); continue; }
                if(op == 8){
                    char why[32];
                    int code = n >= 2 ? (((unsigned char)L.recv[0] << 8) |
                                         (unsigned char)L.recv[1]) : 1006;
                    snprintf(why, sizeof why, "close %d", code);
                    gw_drop(&L, now, why);
                    break;
                }
                if(op != 1) continue;
                L.recv[n] = 0;
                ev = gw_feed(g, L.recv, (size_t)n);
                if(ev & GW_EV_HELLO){
                    char out[2048];
                    int in = gw_identify(g, out, sizeof out);
                    if(in > 0) ws_send_text(&L.ws, out, (size_t)in);
                    {
                        size_t len = 0;
                        const char *hb = gw_ack(g, &len);
                        if(hb && len) ws_send_text(&L.ws, hb, len);
                        gw_mark_beat(g, now);
                    }
                    L.identified = 1;
                    set_status(&L, "Loading...");
                    log_msg("gw: hello -> identify");
                }
                if(ev & GW_EV_READY){
                    log_msg("gw: ready (%s)", gw_self_name(g));
                    set_status(&L, "Online");
                }
                if(ev & GW_EV_RECONNECT){
                    gw_drop(&L, now, "op7");
                    break;
                }
                if(ev & GW_EV_INVALID_SESSION){
                    gw_drop(&L, now, "invalid session");
                    break;
                }
            }
            if(L.up && L.identified && gw_heartbeat_due(g, now)){
                if(gw_expired_heartbeat(g)){
                    gw_drop(&L, now, "heartbeat unacked");
                } else {
                    size_t len = 0;
                    const char *hb = gw_ack(g, &len);
                    if(hb && len) ws_send_text(&L.ws, hb, len);
                    gw_mark_beat(g, now);
                }
            }
        }

        view_build(&L, &v);

        if(L.up && L.identified && !L.guilds_loaded && !L.guilds_pending &&
           now >= L.guilds_retry_at)
            fetch_guilds(&L);
        if(L.up && L.identified && v.ng > 0 && !L.chan_pending &&
           L.chan_guild != v.guilds[v.gi]->id){
            L.chan_guild = v.guilds[v.gi]->id;
            fetch_channels(&L, L.chan_guild);
        }

        rest_pump(st->rest, now);

        {
            input_event evs[16];
            int ne = input_poll(evs, 16), i;
            for(i = 0; i < ne; i++){
                input_event e = evs[i];
                if(e == IE_LEFT) L.focus = 0;
                else if(e == IE_RIGHT) L.focus = 1;
                else if(e == IE_UP || e == IE_DOWN)
                    nav_step(L.focus == 0 ? &L.gn : &L.cn, e,
                             L.focus == 0 ? v.ng : v.nt);
                else if(e == IE_SELECT)
                    log_msg("input: select unbound");
                else if(e == IE_BACK)
                    log_msg("input: back unbound");
            }
        }

        view_build(&L, &v);
        ui_frame_begin();
        draw_main(&L, &v);
        ui_frame_end();
        ui_pump();
        usleep(15000);
    }

    free(L.recv);
    ws_close(&L.ws);
    return 0;
}
