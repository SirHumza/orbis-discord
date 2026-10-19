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
#include "msgview.h"
#include "composer.h"
#include "../core/ws.h"
#include "../core/jsonlite.h"
#include "../core/clock.h"
#include "../core/log.h"
#include "../model/cdnurl.h"
#include "../model/msgjson.h"
#include "../model/snowflake.h"
#include "../image.h"
#include "../platform/input.h"
#include "../platform/ime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
#define CHAT_Y0 (HEADER_H + 1)
#define CHAT_H (SCR_H - CHAT_Y0)
#define COMPOSER_H 80
#define CHAT_VIEW_H (CHAT_H - COMPOSER_H)
#define MSG_PAD 16
#define MSG_INDENT 48

typedef struct {
    const guild *guilds[64];
    int ng;
    int gi;
    int dm;
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
    uint64_t chan_retry_at;
    uint64_t member_guild;
    int member_pending;
    uint64_t member_retry_at;
    int dms_loaded;
    int dms_pending;
    uint64_t dms_retry_at;
    nav gn, cn;
    int focus;
    char status[64];
    mv_layout mv;
    uint64_t last_rev;
    int stick;
    int prepend;
    int top_px;
    uint64_t chan_loaded;
    uint64_t hist_before;
    int has_older;
    int hist_pending;
    int hist_older_pending;
    int viewer;
    int view_n;
    int view_i;
    char view_urls[MV_IMG_MAX][320];
    int sel;
    uint64_t sel_id;
    int menu;
    int menu_i;
    int menu_n;
    int menu_items[5];
    int react_pick;
    int react_i;
    int pins;
    int pins_pending;
    int pins_loaded;
    uint64_t pins_chan;
    uint64_t pin_ids[50];
    int pin_ids_n;
    const message *pin_list[50];
    int pin_n;
    int pin_i;
    int search;
    int search_pending;
    int search_gen;
    char search_q[96];
    uint64_t srch_ids[50];
    int srch_ids_n;
    const message *srch_list[50];
    int srch_n;
    int srch_i;
} main_ctx;

typedef struct {
    main_ctx *ctx;
    uint64_t guild;
} chan_fetch;

typedef struct {
    main_ctx *ctx;
    uint64_t chan;
    int older;
} hist_fetch;

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
        jl_val_t *icon = o ? jl_obj_get(o, "icon") : NULL;
        guild g;
        if(!id || id->type != JL_STRING) continue;
        memset(&g, 0, sizeof g);
        g.id = sn_parse(id->str);
        if(name && name->type == JL_STRING)
            snprintf(g.name, sizeof g.name, "%s", name->str);
        cdn_guild_icon_url(g.icon_url, sizeof g.icon_url, g.id,
                           icon && icon->type == JL_STRING ? icon->str : "");
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
        L->chan_retry_at = mono_ms() + 5000;
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
                 guilds_cb, L) != 0){
        L->guilds_pending = 0;
        L->guilds_retry_at = mono_ms() + 5000;
    }
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
        L->chan_retry_at = mono_ms() + 5000;
        free(f);
    }
}

static void dms_cb(int status, const char *body, void *ud){
    main_ctx *L = ud;
    jl_val_t *v;
    int i;
    L->dms_pending = 0;
    if(status != 200){
        log_msg("rest: dms failed (%d)", status);
        L->dms_retry_at = mono_ms() + 5000;
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    if(v && v->type == JL_ARRAY){
        for(i = 0; i < v->count; i++){
            jl_val_t *o = jl_arr_at(v, i);
            jl_val_t *id = o ? jl_obj_get(o, "id") : NULL;
            jl_val_t *type = o ? jl_obj_get(o, "type") : NULL;
            jl_val_t *name = o ? jl_obj_get(o, "name") : NULL;
            jl_val_t *rec = o ? jl_obj_get(o, "recipients") : NULL;
            jl_val_t *icon = o ? jl_obj_get(o, "icon") : NULL;
            channel ch;
            if(!id || id->type != JL_STRING) continue;
            memset(&ch, 0, sizeof ch);
            ch.id = sn_parse(id->str);
            if(type && type->type == JL_NUMBER) ch.type = (int)type->num;
            ch.position = i;
            if(name && name->type == JL_STRING && name->str[0])
                snprintf(ch.name, sizeof ch.name, "%s", name->str);
            else if(rec && rec->type == JL_ARRAY){
                jl_val_t *r0 = jl_arr_at(rec, 0);
                jl_val_t *un = r0 ? jl_obj_get(r0, "username") : NULL;
                if(un && un->type == JL_STRING)
                    snprintf(ch.name, sizeof ch.name, "%s", un->str);
            }
            if(ch.type == 3)
                cdn_channel_icon_url(ch.avatar_url, sizeof ch.avatar_url, ch.id,
                                     icon && icon->type == JL_STRING ? icon->str : "");
            else if(rec && rec->type == JL_ARRAY && rec->count == 1){
                jl_val_t *r0 = jl_arr_at(rec, 0);
                jl_val_t *rid = r0 ? jl_obj_get(r0, "id") : NULL;
                jl_val_t *rav = r0 ? jl_obj_get(r0, "avatar") : NULL;
                if(rid && rid->type == JL_STRING)
                    cdn_avatar_url(ch.avatar_url, sizeof ch.avatar_url,
                                   sn_parse(rid->str),
                                   rav && rav->type == JL_STRING ? rav->str : "");
            }
            store_put_channel(L->app->st, &ch);
        }
        log_msg("rest: %d dm channels", v->count);
        L->dms_loaded = 1;
    }
    jl_free(v);
}

static void fetch_dms(main_ctx *L){
    L->dms_pending = 1;
    if(rest_get_dm_channels(L->app->rest, dms_cb, L) != 0){
        L->dms_pending = 0;
        L->dms_retry_at = mono_ms() + 5000;
    }
}

static void members_cb(int status, const char *body, void *ud){
    chan_fetch *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v;
    int i;
    L->member_pending = 0;
    if(status != 200){
        log_msg("rest: members failed (%d)", status);
        L->member_guild = 0;
        L->member_retry_at = mono_ms() + 5000;
        free(f);
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    if(v && v->type == JL_ARRAY){
        for(i = 0; i < v->count; i++){
            jl_val_t *o = jl_arr_at(v, i);
            jl_val_t *u = o ? jl_obj_get(o, "user") : NULL;
            jl_val_t *uid = u ? jl_obj_get(u, "id") : NULL;
            jl_val_t *uname = u ? jl_obj_get(u, "username") : NULL;
            jl_val_t *uav = u ? jl_obj_get(u, "avatar") : NULL;
            jl_val_t *nick = o ? jl_obj_get(o, "nick") : NULL;
            member mem;
            if(!uid || uid->type != JL_STRING) continue;
            memset(&mem, 0, sizeof mem);
            mem.user = sn_parse(uid->str);
            mem.guild = f->guild;
            if(nick && nick->type == JL_STRING && nick->str[0])
                snprintf(mem.name, sizeof mem.name, "%s", nick->str);
            else if(uname && uname->type == JL_STRING)
                snprintf(mem.name, sizeof mem.name, "%s", uname->str);
            cdn_avatar_url(mem.avatar_url, sizeof mem.avatar_url, mem.user,
                           uav && uav->type == JL_STRING ? uav->str : "");
            store_put_member(L->app->st, &mem);
        }
        log_msg("rest: %d members for %llu", v->count,
                (unsigned long long)f->guild);
    }
    jl_free(v);
    free(f);
}

static void fetch_members(main_ctx *L, uint64_t guild_id){
    chan_fetch *f = calloc(1, sizeof *f);
    if(!f){ L->member_guild = 0; return; }
    f->ctx = L;
    f->guild = guild_id;
    L->member_pending = 1;
    if(rest_get_guild_members(L->app->rest, guild_id, members_cb, f) != 0){
        L->member_pending = 0;
        L->member_guild = 0;
        L->member_retry_at = mono_ms() + 5000;
        free(f);
    }
}

static void put_message_json(main_ctx *L, jl_val_t *o){
    message m;
    if(!msg_from_json(&m, o)) return;
    msg_resolve_mentions(L->app->st, &m, gw_self_id(L->app->gw));
    store_put_message(L->app->st, &m);
}

typedef struct {
    main_ctx *ctx;
    uint64_t chan;
} pins_fetch;

static void pins_recollect(main_ctx *L){
    int i;
    L->pin_n = 0;
    for(i = 0; i < L->pin_ids_n; i++){
        const message *m = store_message(L->app->st, L->pins_chan,
                                         L->pin_ids[i]);
        if(m) L->pin_list[L->pin_n++] = m;
    }
}

static void pins_cb(int status, const char *body, void *ud){
    pins_fetch *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v;
    int i, j;
    L->pins_pending = 0;
    if(status != 200){
        log_msg("rest: pins failed (%d)", status);
        set_status(L, "Pins failed");
        free(f);
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    L->pin_ids_n = 0;
    if(v && v->type == JL_ARRAY){
        for(i = 0; i < v->count && L->pin_ids_n < 50; i++){
            jl_val_t *o = jl_arr_at(v, i);
            message m;
            if(!msg_from_json(&m, o)) continue;
            msg_resolve_mentions(L->app->st, &m, gw_self_id(L->app->gw));
            L->pin_ids[L->pin_ids_n++] = m.id;
            store_put_message(L->app->st, &m);
        }
    }
    jl_free(v);
    for(i = 1; i < L->pin_ids_n; i++){
        uint64_t k = L->pin_ids[i];
        for(j = i - 1; j >= 0 && L->pin_ids[j] < k; j--)
            L->pin_ids[j + 1] = L->pin_ids[j];
        L->pin_ids[j + 1] = k;
    }
    pins_recollect(L);
    L->pins_loaded = 1;
    L->pins_chan = f->chan;
    L->pin_i = 0;
    log_msg("rest: %d pins for %llu", L->pin_n,
            (unsigned long long)f->chan);
    free(f);
}

static void pins_open(main_ctx *L, uint64_t chan){
    char path[96];
    pins_fetch *f;
    L->pins = 1;
    if(L->pins_loaded && L->pins_chan == chan){
        pins_recollect(L);
        L->pin_i = 0;
        return;
    }
    L->pins_loaded = 0;
    L->pin_n = 0;
    L->pin_ids_n = 0;
    L->pins_chan = chan;
    L->pin_i = 0;
    if(L->pins_pending) return;
    f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = chan;
    snprintf(path, sizeof path, "/api/v9/channels/%llu/pins",
             (unsigned long long)chan);
    L->pins_pending = 1;
    if(rest_send(L->app->rest, "GET", path, NULL, pins_cb, f) != 0){
        L->pins_pending = 0;
        free(f);
        set_status(L, "Pins failed");
    }
}

static char lower_ascii(char c){
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int has_nocase(const char *hay, const char *needle){
    size_t n = strlen(needle), i;
    const char *p;
    if(n == 0) return 0;
    for(p = hay; *p; p++){
        for(i = 0; i < n && p[i] && lower_ascii(p[i]) == lower_ascii(needle[i]); i++){}
        if(i == n) return 1;
    }
    return 0;
}

typedef struct {
    main_ctx *ctx;
    uint64_t chan;
    int gen;
} search_fetch;

static void search_recollect(main_ctx *L){
    int i;
    L->srch_n = 0;
    for(i = 0; i < L->srch_ids_n; i++){
        const message *m = store_message(L->app->st, L->chan_loaded,
                                         L->srch_ids[i]);
        if(m) L->srch_list[L->srch_n++] = m;
    }
}

static void search_cb(int status, const char *body, void *ud){
    search_fetch *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v, *msgs;
    int i;
    if(f->gen != L->search_gen){
        free(f);
        return;
    }
    L->search_pending = 0;
    if(status != 200){
        log_msg("rest: search failed (%d)", status);
        set_status(L, "Search failed");
        L->srch_ids_n = 0;
        L->srch_n = 0;
        free(f);
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    msgs = v && v->type == JL_OBJECT ? jl_obj_get(v, "messages") : NULL;
    L->srch_ids_n = 0;
    if(msgs && msgs->type == JL_ARRAY){
        for(i = 0; i < msgs->count && L->srch_ids_n < 50; i++){
            jl_val_t *g = jl_arr_at(msgs, i);
            message pick;
            int have = 0, k;
            memset(&pick, 0, sizeof pick);
            if(g->type == JL_ARRAY){
                for(k = 0; k < g->count; k++){
                    message m;
                    if(!msg_from_json(&m, jl_arr_at(g, k))) continue;
                    if(!have && has_nocase(m.content, L->search_q)){
                        pick = m;
                        have = 1;
                    }
                }
                if(!have && g->count > 0 &&
                   msg_from_json(&pick, jl_arr_at(g, 0)))
                    have = 1;
            } else if(g->type == JL_OBJECT &&
                      msg_from_json(&pick, g)){
                have = 1;
            }
            if(!have || pick.chan != f->chan) continue;
            for(k = 0; k < L->srch_ids_n; k++)
                if(L->srch_ids[k] == pick.id) break;
            if(k < L->srch_ids_n) continue;
            L->srch_ids[L->srch_ids_n++] = pick.id;
            msg_resolve_mentions(L->app->st, &pick, gw_self_id(L->app->gw));
            store_put_message(L->app->st, &pick);
        }
    }
    jl_free(v);
    search_recollect(L);
    L->srch_i = 0;
    log_msg("rest: %d search hits for %llu", L->srch_n,
            (unsigned long long)f->chan);
    free(f);
}

static void search_start(main_ctx *L, uint64_t chan, const char *query){
    char enc[384], path[512];
    const channel *ch = store_channel(L->app->st, chan);
    search_fetch *f;
    if(!ch || !query || !*query) return;
    if(rest_path_encode(query, enc, sizeof enc) < 0) return;
    if(ch->guild)
        snprintf(path, sizeof path,
                 "/api/v9/guilds/%llu/messages/search?limit=25"
                 "&channel_id=%llu&content=%s",
                 (unsigned long long)ch->guild, (unsigned long long)chan, enc);
    else
        snprintf(path, sizeof path,
                 "/api/v9/channels/%llu/messages/search?limit=25&content=%s",
                 (unsigned long long)chan, enc);
    snprintf(L->search_q, sizeof L->search_q, "%s", query);
    L->search = 1;
    L->search_pending = 1;
    L->search_gen++;
    L->srch_ids_n = 0;
    L->srch_n = 0;
    L->srch_i = 0;
    f = calloc(1, sizeof *f);
    if(!f){
        L->search_pending = 0;
        return;
    }
    f->ctx = L;
    f->chan = chan;
    f->gen = L->search_gen;
    if(rest_send(L->app->rest, "GET", path, NULL, search_cb, f) != 0){
        L->search_pending = 0;
        free(f);
        set_status(L, "Search failed");
    }
}

static void search_prompt(main_ctx *L, uint64_t chan, int prefill){
    char q[96];
    memset(q, 0, sizeof q);
    if(prefill) snprintf(q, sizeof q, "%s", L->search_q);
    if(!ime_prompt("Search messages", "", q, sizeof q)) return;
    if(!q[0]) return;
    search_start(L, chan, q);
}

static void messages_cb(int status, const char *body, void *ud){
    hist_fetch *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v;
    int i;
    if(f->older) L->hist_older_pending = 0;
    else L->hist_pending = 0;
    if(status != 200){
        log_msg("rest: messages failed (%d)", status);
        free(f);
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    if(v && v->type == JL_ARRAY){
        for(i = 0; i < v->count; i++)
            put_message_json(L, jl_arr_at(v, i));
        if(f->older && v->count < 50) L->has_older = 0;
        if(!f->older) L->has_older = v->count >= 50;
        log_msg("rest: %d messages for %llu%s", v->count,
                (unsigned long long)f->chan, f->older ? " (older)" : "");
    }
    jl_free(v);
    if(f->chan == L->chan_loaded){
        const message *head = store_messages(L->app->st, f->chan);
        L->hist_before = head ? head->id : 0;
    }
    if(f->older) L->prepend = 1;
    else L->stick = 1;
    free(f);
}

static void fetch_history(main_ctx *L, uint64_t chan, int older){
    hist_fetch *f;
    if(older && (L->hist_older_pending || !L->has_older ||
                 (L->hist_before == 0 && L->mv.n > 0)))
        return;
    if(!older && L->hist_pending) return;
    f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = chan;
    f->older = older;
    if(older) L->hist_older_pending = 1;
    else L->hist_pending = 1;
    if(rest_get_messages(L->app->rest, chan, older ? L->hist_before : 0,
                         messages_cb, f) != 0){
        if(older) L->hist_older_pending = 0;
        else L->hist_pending = 0;
        free(f);
    }
}

typedef struct {
    main_ctx *ctx;
    uint64_t chan;
    uint64_t nonce;
} send_fetch;

static void send_cb(int status, const char *body, void *ud){
    send_fetch *f = ud;
    main_ctx *L = f->ctx;
    store_del_pending(L->app->st, f->chan, f->nonce);
    if(status == 200 && body){
        jl_val_t *v = jl_parse(body, strlen(body));
        if(v && v->type == JL_OBJECT)
            put_message_json(L, v);
        jl_free(v);
        log_msg("rest: sent to %llu", (unsigned long long)f->chan);
    } else {
        log_msg("rest: send failed (%d)", status);
        set_status(L, "Send failed");
    }
    if(L->chan_loaded == f->chan) L->stick = 1;
    free(f);
}

static void reply_text_copy(char *dst, size_t cap, const char *src){
    size_t n = 0;
    if(!dst || cap == 0) return;
    while(src && *src && n + 1 < cap){
        char c = *src++;
        if(c == '\n' || c == '\r' || c == '\t') c = ' ';
        dst[n++] = c;
    }
    dst[n] = 0;
}

static void send_message(main_ctx *L, uint64_t chan, const char *content,
                         uint64_t reply_id, uint64_t reply_chan){
    static uint64_t seq;
    char payload[8192];
    char path[96];
    uint64_t nonce;
    message pm;
    send_fetch *f;
    if(!content || !*content) return;
    nonce = (uint64_t)time(NULL) * 1000ULL + (seq++ % 1000);
    if((reply_id ? composer_payload_reply(content, nonce, reply_id, reply_chan,
                                          payload, sizeof payload)
                 : composer_payload(content, nonce, payload, sizeof payload)) < 0){
        log_msg("send: payload overflow");
        return;
    }
    memset(&pm, 0, sizeof pm);
    pm.id = (nonce << 22) | 1u;
    pm.chan = chan;
    pm.author_id = gw_self_id(L->app->gw);
    if(L->app->self_name[0])
        snprintf(pm.author_name, sizeof pm.author_name, "%s", L->app->self_name);
    else
        snprintf(pm.author_name, sizeof pm.author_name, "%s",
                 gw_self_name(L->app->gw));
    snprintf(pm.content, sizeof pm.content, "%s", content);
    if(reply_id){
        const message *orig = store_message(L->app->st, reply_chan, reply_id);
        pm.reply_to = reply_id;
        if(orig){
            snprintf(pm.reply_name, sizeof pm.reply_name, "%s", orig->author_name);
            reply_text_copy(pm.reply_text, sizeof pm.reply_text, orig->content);
        }
    }
    pm.pending = 1;
    pm.nonce = nonce;
    store_put_message(L->app->st, &pm);
    L->stick = 1;
    f = calloc(1, sizeof *f);
    if(!f){
        store_del_pending(L->app->st, chan, nonce);
        return;
    }
    f->ctx = L;
    f->chan = chan;
    f->nonce = nonce;
    snprintf(path, sizeof path, "/api/v9/channels/%llu/messages",
             (unsigned long long)chan);
    if(rest_send(L->app->rest, "POST", path, payload, send_cb, f) != 0){
        store_del_pending(L->app->st, chan, nonce);
        free(f);
        set_status(L, "Send failed");
        return;
    }
    set_status(L, "Sending...");
}

typedef struct {
    main_ctx *ctx;
    uint64_t chan, id;
    int del;
} msg_op;

static void msg_op_cb(int status, const char *body, void *ud){
    msg_op *f = ud;
    main_ctx *L = f->ctx;
    (void)body;
    if(f->del ? (status == 200 || status == 204) : status == 200){
        if(f->del) store_del_message(L->app->st, f->chan, f->id);
        log_msg("rest: message %s ok", f->del ? "delete" : "edit");
    } else {
        log_msg("rest: message %s failed (%d)", f->del ? "delete" : "edit",
                status);
        set_status(L, f->del ? "Delete failed" : "Edit failed");
    }
    free(f);
}

static void edit_message(main_ctx *L, const message *m, const char *text){
    char payload[4096];
    char path[128];
    msg_op *f;
    if(composer_payload_edit(text, payload, sizeof payload) < 0) return;
    f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = m->chan;
    f->id = m->id;
    snprintf(path, sizeof path, "/api/v9/channels/%llu/messages/%llu",
             (unsigned long long)m->chan, (unsigned long long)m->id);
    if(rest_send(L->app->rest, "PATCH", path, payload, msg_op_cb, f) != 0){
        free(f);
        set_status(L, "Edit failed");
        return;
    }
    set_status(L, "Editing...");
}

static void delete_message(main_ctx *L, const message *m){
    char path[128];
    msg_op *f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = m->chan;
    f->id = m->id;
    f->del = 1;
    snprintf(path, sizeof path, "/api/v9/channels/%llu/messages/%llu",
             (unsigned long long)m->chan, (unsigned long long)m->id);
    if(rest_send(L->app->rest, "DELETE", path, NULL, msg_op_cb, f) != 0){
        free(f);
        set_status(L, "Delete failed");
        return;
    }
    set_status(L, "Deleting...");
}

static int measure_adapter(void *ud, const char *s, size_t len){
    char buf[MV_MEASURE_MAX + 1];
    (void)ud;
    if(len > MV_MEASURE_MAX) len = MV_MEASURE_MAX;
    memcpy(buf, s, len);
    buf[len] = 0;
    return ui_text_width(buf, MV_LINE_H - 6);
}

static void scroll_clamp(main_ctx *L){
    int max = L->mv.height - CHAT_VIEW_H;
    if(max < 0) max = 0;
    if(L->top_px > max) L->top_px = max;
    if(L->top_px < 0) L->top_px = 0;
}

static void view_rebuild(main_ctx *L, uint64_t chan){
    int old_h = L->mv.height;
    const message *head = store_messages(L->app->st, chan);
    mv_build(&L->mv, head, CHAT_W - 2 * MSG_PAD - MSG_INDENT,
             measure_adapter, NULL);
    if(L->prepend && !L->stick) L->top_px += L->mv.height - old_h;
    L->prepend = 0;
    if(L->stick) L->top_px = L->mv.height;
    scroll_clamp(L);
    L->last_rev = store_revision(L->app->st);
}

static void sel_into_view(main_ctx *L){
    int i, top = -1, bottom = 0;
    for(i = 0; i < L->mv.n; i++){
        const mv_line *ln = &L->mv.lines[i];
        int lh = ln->att_h > 0 ? ln->att_h : MV_LINE_H;
        if(ln->m->id != L->sel_id) continue;
        if(top < 0 || ln->y < top) top = ln->y;
        if(ln->y + lh > bottom) bottom = ln->y + lh;
    }
    if(top < 0) return;
    L->stick = 0;
    if(top < L->top_px) L->top_px = top;
    if(bottom > L->top_px + CHAT_VIEW_H) L->top_px = bottom - CHAT_VIEW_H;
    scroll_clamp(L);
}

static void sel_begin(main_ctx *L, uint64_t chan){
    const message *m = store_newest_message(L->app->st, chan);
    if(!m) return;
    L->sel = 1;
    L->sel_id = m->id;
    sel_into_view(L);
}

static const char *QUICK_EMOJI[] = {
    "\xF0\x9F\x91\x8D", "\xE2\x9D\xA4\xEF\xB8\x8F",
    "\xF0\x9F\x98\x82", "\xF0\x9F\x8E\x89",
    "\xF0\x9F\x94\xA5", "\xF0\x9F\x91\x80"
};

static const char *menu_label(int item){
    switch(item){
    case 1: return "Reply";
    case 2: return "Edit";
    case 3: return "Delete";
    case 4: return "React";
    default: return "Cancel";
    }
}

static void menu_open(main_ctx *L, const message *m){
    int own = m->author_id && m->author_id == gw_self_id(L->app->gw) &&
              !m->pending;
    L->menu_n = 0;
    L->menu_items[L->menu_n++] = 1;
    L->menu_items[L->menu_n++] = 4;
    if(own){
        L->menu_items[L->menu_n++] = 2;
        L->menu_items[L->menu_n++] = 3;
    }
    L->menu_items[L->menu_n++] = 0;
    L->menu_i = 0;
    L->menu = 1;
}

static int react_has_me(const message *m, const char *emoji){
    int i;
    for(i = 0; i < m->nreact; i++)
        if(m->react[i].me && strcmp(m->react[i].emoji, emoji) == 0) return 1;
    return 0;
}

typedef struct {
    main_ctx *ctx;
} react_op;

static void react_cb(int status, const char *body, void *ud){
    react_op *f = ud;
    (void)body;
    if(status != 200 && status != 204){
        log_msg("rest: reaction failed (%d)", status);
        set_status(f->ctx, "Reaction failed");
    } else {
        log_msg("rest: reaction ok");
    }
    free(f);
}

static void react_toggle(main_ctx *L, const message *m, const char *emoji){
    char enc[256], path[384];
    react_op *f;
    int remove = react_has_me(m, emoji);
    if(rest_path_encode(emoji, enc, sizeof enc) < 0) return;
    snprintf(path, sizeof path,
             "/api/v9/channels/%llu/messages/%llu/reactions/%s/@me",
             (unsigned long long)m->chan, (unsigned long long)m->id, enc);
    f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    if(rest_send(L->app->rest, remove ? "DELETE" : "PUT", path, NULL,
                 react_cb, f) != 0){
        free(f);
        set_status(L, "Reaction failed");
        return;
    }
}

static void menu_choose(main_ctx *L, const message *m, int item){
    char text[1024], title[96];
    if(item == 1){
        snprintf(title, sizeof title, "Reply to @%s", m->author_name);
        memset(text, 0, sizeof text);
        if(ime_prompt(title, "", text, sizeof text))
            send_message(L, m->chan, text, m->id, m->chan);
    } else if(item == 2){
        snprintf(text, sizeof text, "%s", m->content);
        if(ime_prompt("Edit message", "", text, sizeof text) && text[0])
            edit_message(L, m, text);
    } else if(item == 3){
        delete_message(L, m);
    } else if(item == 4){
        L->react_pick = 1;
        L->react_i = 0;
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
    int sel;
    v->ng = (int)store_guilds(L->app->st, v->guilds, 64);
    nav_step(&L->gn, IE_NONE, v->ng + 1);
    sel = nav_index(&L->gn);
    v->dm = sel == 0;
    v->gi = sel - 1;
    v->nt = 0;
    v->active = NULL;
    if(v->dm){
        const channel *dms[64];
        nc = store_dm_channels(L->app->st, dms, 64);
        for(i = 0; i < nc && v->nt < 64; i++)
            v->text[v->nt++] = dms[i];
    } else if(v->ng > 0){
        nc = store_channels_for_guild(L->app->st, v->guilds[v->gi]->id, cids, 64);
        for(i = 0; i < nc && v->nt < 64; i++){
            const channel *c = store_channel(L->app->st, cids[i]);
            if(c && c->type == 0) v->text[v->nt++] = c;
        }
    }
    nav_step(&L->cn, IE_NONE, v->nt);
    v->ci = nav_index(&L->cn);
    if(v->nt > 0) v->active = v->text[v->ci];
}

static void draw_members(main_ctx *L, const frame_view *v){
    const member *ms[256];
    size_t n, i;
    int y = 52, on = 0;
    char buf[96], nb[64];
    if(v->dm || v->gi < 0 || v->ng <= 0) return;
    n = store_members(L->app->st, v->guilds[v->gi]->id, ms, 256);
    for(i = 0; i < n; i++)
        if(ms[i]->online) on++;
    snprintf(buf, sizeof buf, "ONLINE - %d", on);
    ui_text(MEMBER_X + 16, y, buf, UI_TEXT_DIM, 12);
    y += 22;
    for(i = 0; i < n && y < SCR_H - 40; i++){
        if(!ms[i]->online) continue;
        ui_rrect(MEMBER_X + 16, y + 4, 20, 20, 10, UI_SEP);
        if(ms[i]->avatar_url[0])
            ui_img(ms[i]->avatar_url, MEMBER_X + 16, y + 4, 20, 20);
        ui_rect(MEMBER_X + 28, y + 16, 8, 8, UI_ONLINE);
        trunc_utf8(nb, sizeof nb, ms[i]->name, 20);
        ui_text(MEMBER_X + 44, y, nb, ms[i]->color ? ms[i]->color : UI_TEXT, 16);
        y += 28;
    }
    snprintf(buf, sizeof buf, "OFFLINE - %d", (int)n - on);
    y += 8;
    ui_text(MEMBER_X + 16, y, buf, UI_TEXT_DIM, 12);
    y += 22;
    for(i = 0; i < n && y < SCR_H - 40; i++){
        if(ms[i]->online) continue;
        ui_rrect(MEMBER_X + 16, y + 4, 20, 20, 10, UI_SEP);
        if(ms[i]->avatar_url[0])
            ui_img(ms[i]->avatar_url, MEMBER_X + 16, y + 4, 20, 20);
        ui_rect(MEMBER_X + 28, y + 16, 8, 8, UI_OFFLINE);
        trunc_utf8(nb, sizeof nb, ms[i]->name, 20);
        ui_text(MEMBER_X + 44, y, nb, ms[i]->color ? ms[i]->color : UI_TEXT_DIM, 16);
        y += 28;
    }
}

static uint32_t user_color(store *st, uint64_t chan, uint64_t user){
    const channel *ch = store_channel(st, chan);
    const member *ms[200];
    size_t n, i;
    if(!ch || !ch->guild) return UI_TEXT;
    n = store_members(st, ch->guild, ms, 200);
    for(i = 0; i < n; i++)
        if(ms[i]->user == user && ms[i]->color) return ms[i]->color;
    return UI_TEXT;
}

static void draw_messages(main_ctx *L){
    int i;
    if(L->mv.n == 0) return;
    ui_set_clip(CHAT_X, CHAT_Y0, CHAT_W, CHAT_VIEW_H);
    for(i = 0; i < L->mv.n; i++){
        const mv_line *ln = &L->mv.lines[i];
        int y = CHAT_Y0 + ln->y - L->top_px;
        int lh = ln->att_h > 0 ? ln->att_h : MV_LINE_H;
        if(y + lh < CHAT_Y0) continue;
        if(y > CHAT_Y0 + CHAT_VIEW_H) break;
        if(!ln->is_sep && ln->m->mentions_me){
            ui_rect(CHAT_X, y, CHAT_W, lh, 0x2AFAA61Au);
            ui_rect(CHAT_X, y, 2, lh, 0xFAA61Au);
        }
        if(!ln->is_sep && L->sel && ln->m->id == L->sel_id){
            ui_rect(CHAT_X, y, CHAT_W, lh, 0x335865F2u);
            ui_rect(CHAT_X, y, 3, lh, UI_BLURPLE);
        }
        if(ln->is_sep){
            char d[48];
            int tw, half;
            uint64_t t = (uint64_t)((ln->m->id >> 22) + 1420070400000ULL);
            mv_day_str(t, d, sizeof d);
            tw = ui_text_width(d, 12);
            half = (CHAT_W - 2 * MSG_PAD - tw) / 2 - 12;
            if(half > 0){
                ui_rect(CHAT_X + MSG_PAD, y + MV_SEP_H / 2, half, 1, UI_SEP);
                ui_rect(CHAT_X + CHAT_W - MSG_PAD - half, y + MV_SEP_H / 2,
                        half, 1, UI_SEP);
            }
            ui_text(CHAT_X + (CHAT_W - tw) / 2, y + 10, d, UI_TEXT_DIM, 12);
        } else if(ln->att >= 0 && ln->att < ln->m->natt){
            const attachment *a = &ln->m->att[ln->att];
            char fb[128];
            int ax = CHAT_X + MSG_PAD + MSG_INDENT;
            if(a->image && ui_img(a->url, ax, y, ln->att_w, ln->att_h))
                continue;
            trunc_utf8(fb, sizeof fb, a->name[0] ? a->name : "attachment", 56);
            ui_text(ax, y + 4, fb, UI_TEXT_LINK, 14);
        } else if(ln->emb >= 0 && ln->emb < ln->m->nemb){
            const embed *e = &ln->m->emb[ln->emb];
            int x0 = CHAT_X + MSG_PAD + MSG_INDENT;
            char fb[MV_MEASURE_MAX + 1];
            uint32_t bar = e->has_color ? e->color : UI_SEP;
            ui_rect(x0 + 4, y, CHAT_X + CHAT_W - MSG_PAD - x0 - 4, lh, UI_CHANNEL_BG);
            ui_rect(x0, y, 4, lh, bar);
            if(ln->emb_kind == 3){
                if(!ui_img(e->image_url, x0 + 16, y, ln->att_w, ln->att_h))
                    ui_text(x0 + 16, y + 4, "embed image", UI_TEXT_DIM, 14);
            } else if(ln->emb_kind == 1){
                trunc_utf8(fb, sizeof fb, e->title, 70);
                ui_text(x0 + 16, y + 3, fb, UI_TEXT, 16);
            } else {
                int len = ln->len;
                if(len > MV_MEASURE_MAX) len = MV_MEASURE_MAX;
                memcpy(fb, e->desc + ln->off, (size_t)len);
                fb[len] = 0;
                ui_text(x0 + 16, y + 2, fb, UI_TEXT_DIM, 14);
            }
        } else if(ln->react >= 0){
            const reaction *rs = ln->m->react;
            int r, rx = CHAT_X + MSG_PAD + MSG_INDENT;
            int rmax = CHAT_X + CHAT_W - MSG_PAD;
            for(r = 0; r < ln->m->nreact && rx < rmax - 40; r++){
                char cnt[16];
                int ew, cw, w;
                ew = ui_text_width(rs[r].emoji, 14);
                snprintf(cnt, sizeof cnt, "%d", rs[r].count);
                cw = ui_text_width(cnt, 12);
                w = 10 + ew + 6 + cw + 10;
                ui_rrect(rx, y + 2, w, 22, 11,
                         rs[r].me ? UI_BLURPLE : UI_CHANNEL_BG);
                ui_text(rx + 10, y + 4, rs[r].emoji, UI_TEXT, 14);
                ui_text(rx + 10 + ew + 6, y + 6, cnt, UI_TEXT_DIM, 12);
                rx += w + 6;
            }
        } else if(ln->reply >= 0){
            const message *m = ln->m;
            char who[80], snip[100];
            int x = CHAT_X + MSG_PAD + MSG_INDENT;
            int wx;
            ui_rect(x - 16, y + 8, 10, 1, UI_SEP);
            if(m->reply_name[0])
                snprintf(who, sizeof who, "@%s", m->reply_name);
            else
                snprintf(who, sizeof who, "reply");
            trunc_utf8(who, sizeof who, who, 18);
            ui_text(x, y + 1, who, UI_TEXT, 13);
            wx = ui_text_width(who, 13);
            trunc_utf8(snip, sizeof snip, m->reply_text, 60);
            if(snip[0])
                ui_text(x + wx + 8, y + 1, snip, UI_TEXT_DIM, 13);
        } else if(ln->author){
            char tbuf[8];
            int nw, tx;
            uint64_t t = (uint64_t)((ln->m->id >> 22) + 1420070400000ULL);
            mv_time_str(t, tbuf, sizeof tbuf);
            ui_rrect(CHAT_X + MSG_PAD, y + 1, 40, 40, 20, UI_CHANNEL_BG);
            ui_img(ln->m->author_avatar, CHAT_X + MSG_PAD, y + 1, 40, 40);
            tx = CHAT_X + MSG_PAD + MSG_INDENT;
            ui_text(tx, y + 3, ln->m->author_name,
                    user_color(L->app->st, ln->m->chan, ln->m->author_id), 16);
            nw = ui_text_width(ln->m->author_name, 16);
            tx += nw + 12;
            ui_text(tx, y + 5, tbuf, UI_TEXT_DIM, 12);
            tx += ui_text_width(tbuf, 12) + 8;
            if(ln->m->has_edit){
                ui_text(tx, y + 5, "(edited)", UI_TEXT_DIM, 12);
                tx += ui_text_width("(edited)", 12) + 8;
            }
            if(ln->m->pending)
                ui_text(tx, y + 5, "sending...", UI_TEXT_DIM, 12);
        } else {
            char buf[MV_MEASURE_MAX + 1];
            int len = ln->len;
            if(len > MV_MEASURE_MAX) len = MV_MEASURE_MAX;
            memcpy(buf, ln->m->content + ln->off, (size_t)len);
            buf[len] = 0;
            ui_text(CHAT_X + MSG_PAD + MSG_INDENT, y + 2, buf, UI_TEXT, 16);
        }
    }
    ui_clear_clip();
}

static void open_viewer(main_ctx *L, uint64_t chan){
    const char *urls[MV_IMG_MAX];
    int i, n = mv_images(store_messages(L->app->st, chan), urls, MV_IMG_MAX);
    if(n <= 0) return;
    L->view_n = n;
    for(i = 0; i < n; i++)
        snprintf(L->view_urls[i], sizeof L->view_urls[i], "%s", urls[i]);
    L->view_i = n - 1;
    L->viewer = 1;
}

static void draw_viewer(main_ctx *L){
    const char *url = L->view_urls[L->view_i];
    image *im;
    char cap[64];
    ui_rect(0, 0, SCR_W, SCR_H, 0xF0000000u);
    im = image_get(url);
    if(im && image_ready(im)){
        int iw = image_w(im), ih = image_h(im);
        if(iw > 0 && ih > 0){
            int maxw = SCR_W - 160, maxh = SCR_H - 140;
            int w = iw, h = ih;
            if(w > maxw){ h = h * maxw / w; w = maxw; }
            if(h > maxh){ w = w * maxh / h; h = maxh; }
            ui_img(url, (SCR_W - w) / 2, (SCR_H - h) / 2 - 10, w, h);
        }
    } else {
        const char *s = "Loading image...";
        ui_text((SCR_W - ui_text_width(s, 20)) / 2, SCR_H / 2 - 12, s,
                UI_TEXT_DIM, 20);
    }
    snprintf(cap, sizeof cap, "%d / %d   UP/DOWN browse   CROSS close",
             L->view_i + 1, L->view_n);
    ui_text((SCR_W - ui_text_width(cap, 14)) / 2, SCR_H - 52, cap, UI_TEXT, 14);
}

static void draw_search(main_ctx *L){
    int w = 760, h = 560;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i, first;
    char title[128];
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    trunc_utf8(title, sizeof title, L->search_q, 50);
    snprintf(title + strlen(title), sizeof title - strlen(title),
             " - search");
    ui_text(x + 20, y + 16, title, UI_TEXT, 18);
    if(L->search_pending){
        ui_text(x + 20, y + 60, "Searching...", UI_TEXT_DIM, 16);
    } else if(L->srch_n == 0){
        ui_text(x + 20, y + 60, "No results", UI_TEXT_DIM, 16);
    } else {
        first = L->srch_i - 4;
        if(first < 0) first = 0;
        if(first > L->srch_n - 9) first = L->srch_n - 9;
        if(first < 0) first = 0;
        for(i = first; i < L->srch_n && i < first + 9; i++){
            const message *m = L->srch_list[i];
            char snip[128], who[96];
            int ry = y + 56 + (i - first) * 52;
            if(i == L->srch_i)
                ui_rrect(x + 12, ry - 4, w - 24, 46, 6, UI_BLURPLE);
            snprintf(who, sizeof who, "@%s", m->author_name);
            trunc_utf8(snip, sizeof snip, m->content, 70);
            ui_text(x + 24, ry, who, UI_TEXT, 14);
            if(snip[0]) ui_text(x + 24, ry + 20, snip, UI_TEXT_DIM, 14);
        }
    }
    ui_text(x + 20, y + h - 30,
            "UP/DOWN browse   CROSS jump   R2 new search   CIRCLE close",
            UI_TEXT_DIM, 12);
}

static void draw_pins(main_ctx *L){
    int w = 760, h = 560;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i, first;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 20, y + 16, "Pinned messages", UI_TEXT, 18);
    if(L->pins_pending){
        ui_text(x + 20, y + 60, "Loading pins...", UI_TEXT_DIM, 16);
    } else if(L->pin_n == 0){
        ui_text(x + 20, y + 60, "No pinned messages", UI_TEXT_DIM, 16);
    } else {
        first = L->pin_i - 4;
        if(first < 0) first = 0;
        if(first > L->pin_n - 9) first = L->pin_n - 9;
        if(first < 0) first = 0;
        for(i = first; i < L->pin_n && i < first + 9; i++){
            const message *m = L->pin_list[i];
            char snip[128], who[96];
            int ry = y + 56 + (i - first) * 52;
            if(i == L->pin_i)
                ui_rrect(x + 12, ry - 4, w - 24, 46, 6, UI_BLURPLE);
            snprintf(who, sizeof who, "@%s", m->author_name);
            trunc_utf8(snip, sizeof snip, m->content, 70);
            ui_text(x + 24, ry, who, UI_TEXT, 14);
            if(snip[0]) ui_text(x + 24, ry + 20, snip, UI_TEXT_DIM, 14);
        }
    }
    ui_text(x + 20, y + h - 30,
            L->pin_n ? "UP/DOWN browse   CROSS jump   CIRCLE close"
                     : "CIRCLE close",
            UI_TEXT_DIM, 12);
}

static void draw_react_pick(main_ctx *L){
    int n = (int)(sizeof QUICK_EMOJI / sizeof QUICK_EMOJI[0]);
    int cell = 52, w = 32 + n * cell, h = 110;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 16, y + 10, "Add reaction", UI_TEXT, 14);
    for(i = 0; i < n; i++){
        int cx = x + 16 + i * cell;
        if(i == L->react_i) ui_rrect(cx, y + 30, 44, 44, 8, UI_BLURPLE);
        ui_text(cx + 8, y + 38, QUICK_EMOJI[i], UI_TEXT, 28);
    }
    ui_text(x + 16, y + 84, "CROSS toggle   CIRCLE close", UI_TEXT_DIM, 12);
}

static void draw_menu(main_ctx *L){
    int w = 320, item_h = 40, h = 52 + L->menu_n * item_h;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 16, y + 14, "Message actions", UI_TEXT, 16);
    for(i = 0; i < L->menu_n; i++){
        int iy = y + 44 + i * item_h;
        if(i == L->menu_i) ui_rrect(x + 8, iy, w - 16, item_h - 6, 6,
                                    UI_BLURPLE);
        ui_text(x + 20, iy + 6, menu_label(L->menu_items[i]), UI_TEXT, 16);
    }
}

static void draw_main(main_ctx *L, const frame_view *v){
    char buf[160];
    int i, first, y;

    ui_rect(0, 0, SCR_W, SCR_H, UI_CHAT_BG);

    ui_rect(0, 0, RAIL_W, SCR_H, UI_RAIL);
    {
        int total = v->ng + 1;
        int cur = v->dm ? 0 : v->gi + 1;
        first = cur - GUILD_VIS / 2;
        if(first < 0) first = 0;
        if(first > total - GUILD_VIS) first = total - GUILD_VIS;
        if(first < 0) first = 0;
        for(i = first; i < total && i < first + GUILD_VIS; i++){
            int sel = i == cur;
            y = 16 + (i - first) * GUILD_PITCH;
            ui_rrect(12, y, 48, 48, sel ? 10 : 24, sel ? UI_BLURPLE : UI_CHANNEL_BG);
            if(sel) ui_rect(0, y + 12, 4, 24, UI_TEXT);
            if(i == 0){
                ui_text(36 - ui_text_width("DM", 18) / 2, y + 14, "DM", 0xFFFFFFFFu, 18);
            } else if(!v->guilds[i - 1]->icon_url[0] ||
                      !ui_img(v->guilds[i - 1]->icon_url, 16, y + 4, 40, 40)){
                first_codepoint(v->guilds[i - 1]->name, buf, sizeof buf);
                ui_text(36 - ui_text_width(buf, 20) / 2, y + 13, buf, 0xFFFFFFFFu, 20);
            }
        }
    }

    ui_rect(RAIL_W, 0, CHAN_W, SCR_H, UI_CHANNEL_BG);
    if(v->dm){
        ui_text(RAIL_W + 16, 20, "Direct Messages", UI_TEXT, 18);
    } else if(v->ng > 0){
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
        int tx = RAIL_W + 20;
        y = CHAN_TOP + (i - first) * CHAN_ROW_H;
        if(sel) ui_rrect(RAIL_W + 8, y, CHAN_W - 16, CHAN_ROW_H - 4, 4, UI_SEP);
        if(v->text[i]->avatar_url[0] &&
           ui_img(v->text[i]->avatar_url, RAIL_W + 16, y + 3, 24, 24))
            tx = RAIL_W + 48;
        snprintf(buf, sizeof buf, "%s %s", v->dm ? "@" : "#", v->text[i]->name);
        trunc_utf8(buf, sizeof buf, buf, 26);
        ui_text(tx, y + 7, buf, sel ? UI_TEXT : UI_TEXT_DIM, 16);
        if(store_unread(L->app->st, v->text[i]->id))
            ui_rrect(RAIL_W + CHAN_W - 24, y + 12, 8, 8, 4, UI_TEXT);
    }

    ui_rect(CHAT_X, 0, CHAT_W, SCR_H, UI_CHAT_BG);
    ui_rect(CHAT_X, 0, CHAT_W, HEADER_H, UI_HEADER_BG);
    ui_rect(CHAT_X, HEADER_H, CHAT_W, 1, UI_SEP);
    if(v->active){
        snprintf(buf, sizeof buf, "%s %s", v->dm ? "@" : "#", v->active->name);
        trunc_utf8(buf, sizeof buf, buf, 48);
        ui_text(CHAT_X + 16, 14, buf, UI_TEXT, 18);
    }
    if(L->focus == 2) ui_rect(CHAT_X, HEADER_H - 2, CHAT_W, 2, UI_BLURPLE);
    if(!v->active){
        const char *hint = "Select a text channel";
        ui_text(CHAT_X + (CHAT_W - ui_text_width(hint, 16)) / 2,
                CHAT_Y0 + CHAT_VIEW_H / 2 - 8, hint, UI_TEXT_DIM, 16);
    } else if(L->mv.n == 0){
        const char *hint = L->hist_pending ? "Loading messages..." : "No messages yet";
        ui_text(CHAT_X + (CHAT_W - ui_text_width(hint, 16)) / 2,
                CHAT_Y0 + CHAT_VIEW_H / 2 - 8, hint, UI_TEXT_DIM, 16);
    } else {
        draw_messages(L);
    }

    {
        int cy = SCR_H - COMPOSER_H;
        ui_rect(CHAT_X, cy, CHAT_W, COMPOSER_H, UI_CHAT_BG);
        ui_rect(CHAT_X, cy, CHAT_W, 1, UI_SEP);
        if(v->active){
            char typ[160];
            store_typing_str(L->app->st, v->active->id, orbis_mono_s(),
                             typ, sizeof typ);
            if(typ[0])
                ui_text(CHAT_X + MSG_PAD + 4, cy + 5, typ, UI_TEXT_DIM, 14);
        }
        ui_rrect(CHAT_X + MSG_PAD, cy + 26, CHAT_W - 2 * MSG_PAD,
                 COMPOSER_H - 34, 8, UI_CHANNEL_BG);
        if(L->focus == 2)
            ui_rect(CHAT_X + MSG_PAD, cy + 26, CHAT_W - 2 * MSG_PAD, 2, UI_BLURPLE);
        if(v->active)
            snprintf(buf, sizeof buf, "Message %s%s", v->dm ? "@" : "#",
                     v->active->name);
        else
            snprintf(buf, sizeof buf, "Message");
        trunc_utf8(buf, sizeof buf, buf, 60);
        ui_text(CHAT_X + MSG_PAD + 12, cy + 41, buf, UI_TEXT_DIM, 16);
    }

    ui_rect(MEMBER_X, 0, MEMBER_W, SCR_H, UI_CHANNEL_BG);
    ui_rect(MEMBER_X, 0, 1, SCR_H, UI_SEP);
    if(v->dm)
        ui_text(MEMBER_X + 16, 20, "Direct Messages", UI_TEXT_DIM, 14);
    else {
        ui_text(MEMBER_X + 16, 20, "Members", UI_TEXT_DIM, 14);
        draw_members(L, v);
    }

    if(!L->up || !L->identified){
        const char *warn = "Connection lost. Reconnecting...";
        int wy = SCR_H - COMPOSER_H - 26;
        ui_rect(CHAT_X, wy, CHAT_W, 26, UI_DANGER);
        ui_text(CHAT_X + (CHAT_W - ui_text_width(warn, 13)) / 2, wy + 6,
                warn, 0xFFFFFFFFu, 13);
    }

    {
        uint32_t dot = !L->up ? UI_OFFLINE : L->identified ? UI_ONLINE : UI_IDLE;
        ui_rect(RAIL_W + 16, SCR_H - 30, 8, 8, dot);
        ui_text(RAIL_W + 32, SCR_H - 36, L->status, UI_TEXT_DIM, 12);
    }
}

int screen_main_run(app_state *st){
    main_ctx L;
    frame_view v;
    int auth_failed = 0;
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
                L.member_guild = 0;
                L.dms_loaded = 0;
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
                    if(code == 4004){
                        log_msg("gw: authentication failed (4004)");
                        ws_close(&L.ws);
                        L.up = 0;
                        auth_failed = 1;
                        break;
                    }
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
        if(auth_failed) break;

        view_build(&L, &v);

        if(v.active && v.active->id != L.chan_loaded){
            L.chan_loaded = v.active->id;
            L.stick = 1;
            L.prepend = 0;
            L.top_px = 0;
            L.has_older = 1;
            L.hist_before = 0;
            L.hist_pending = 0;
            L.hist_older_pending = 0;
            L.last_rev = 0;
            L.mv.n = 0;
            L.mv.height = 0;
            fetch_history(&L, L.chan_loaded, 0);
        }

        if(L.up && L.identified && !L.guilds_loaded && !L.guilds_pending &&
           now >= L.guilds_retry_at)
            fetch_guilds(&L);
        if(L.up && L.identified && !v.dm && v.gi >= 0 && v.ng > 0 &&
           !L.chan_pending && now >= L.chan_retry_at &&
           L.chan_guild != v.guilds[v.gi]->id){
            L.chan_guild = v.guilds[v.gi]->id;
            fetch_channels(&L, L.chan_guild);
        }
        if(L.up && L.identified && L.guilds_loaded && !L.dms_loaded &&
           !L.dms_pending && now >= L.dms_retry_at)
            fetch_dms(&L);
        if(L.up && L.identified && !v.dm && v.gi >= 0 && v.ng > 0 &&
           !L.member_pending && now >= L.member_retry_at &&
           L.member_guild != v.guilds[v.gi]->id){
            L.member_guild = v.guilds[v.gi]->id;
            fetch_members(&L, L.member_guild);
        }

        rest_pump(st->rest, now);
        image_tick(now);
        view_build(&L, &v);

        if(L.chan_loaded && store_revision(st->st) != L.last_rev){
            view_rebuild(&L, L.chan_loaded);
            store_mark_read(st->st, L.chan_loaded);
        }

        {
            input_event evs[16];
            int ne = input_poll(evs, 16), i;
            for(i = 0; i < ne; i++){
                input_event e = evs[i];
                if(L.viewer){
                    if(e == IE_VIEW || e == IE_SELECT || e == IE_BACK ||
                       e == IE_MENU)
                        L.viewer = 0;
                    else if(e == IE_UP && L.view_i > 0)
                        L.view_i--;
                    else if(e == IE_DOWN && L.view_i + 1 < L.view_n)
                        L.view_i++;
                    continue;
                }
                if(L.menu){
                    if(e == IE_UP || e == IE_DOWN){
                        L.menu_i += e == IE_UP ? -1 : 1;
                        if(L.menu_i < 0) L.menu_i = L.menu_n - 1;
                        if(L.menu_i >= L.menu_n) L.menu_i = 0;
                    } else if(e == IE_SELECT){
                        const message *m = store_message(
                            L.app->st, L.chan_loaded, L.sel_id);
                        int item = L.menu_items[L.menu_i];
                        L.menu = 0;
                        if(m){
                            if(item != 4) L.sel = 0;
                            menu_choose(&L, m, item);
                        } else {
                            L.sel = 0;
                        }
                    } else if(e == IE_BACK || e == IE_MENU){
                        L.menu = 0;
                        L.sel = 0;
                    }
                    continue;
                }
                if(L.react_pick){
                    int n = (int)(sizeof QUICK_EMOJI / sizeof QUICK_EMOJI[0]);
                    if(e == IE_LEFT || e == IE_UP){
                        L.react_i = (L.react_i + n - 1) % n;
                    } else if(e == IE_RIGHT || e == IE_DOWN){
                        L.react_i = (L.react_i + 1) % n;
                    } else if(e == IE_SELECT){
                        const message *m = store_message(
                            L.app->st, L.chan_loaded, L.sel_id);
                        if(m) react_toggle(&L, m, QUICK_EMOJI[L.react_i]);
                        L.react_pick = 0;
                        L.sel = 0;
                    } else if(e == IE_BACK || e == IE_MENU){
                        L.react_pick = 0;
                        L.sel = 0;
                    }
                    continue;
                }
                if(L.pins){
                    if(e == IE_UP && L.pin_i > 0){
                        L.pin_i--;
                    } else if(e == IE_DOWN && L.pin_i + 1 < L.pin_n){
                        L.pin_i++;
                    } else if(e == IE_SELECT && L.pin_n > 0){
                        const message *m = L.pin_list[L.pin_i];
                        if(m){
                            L.pins = 0;
                            L.sel = 1;
                            L.sel_id = m->id;
                            sel_into_view(&L);
                        }
                    } else if(e == IE_BACK || e == IE_MENU || e == IE_PINS){
                        L.pins = 0;
                    }
                    continue;
                }
                if(L.search){
                    if(e == IE_UP && L.srch_i > 0){
                        L.srch_i--;
                    } else if(e == IE_DOWN && L.srch_i + 1 < L.srch_n){
                        L.srch_i++;
                    } else if(e == IE_SELECT && L.srch_n > 0){
                        const message *m = L.srch_list[L.srch_i];
                        if(m && m->chan == L.chan_loaded){
                            L.search = 0;
                            L.sel = 1;
                            L.sel_id = m->id;
                            sel_into_view(&L);
                        }
                    } else if(e == IE_SEARCH){
                        search_prompt(&L, L.chan_loaded, 1);
                    } else if(e == IE_BACK || e == IE_MENU){
                        L.search = 0;
                    }
                    continue;
                }
                if(L.sel){
                    if(e == IE_UP || e == IE_DOWN){
                        const message *m = store_message_step(
                            L.app->st, L.chan_loaded, L.sel_id,
                            e == IE_UP ? -1 : 1);
                        if(m){
                            L.sel_id = m->id;
                            sel_into_view(&L);
                        }
                    } else if(e == IE_SELECT){
                        const message *m = store_message(
                            L.app->st, L.chan_loaded, L.sel_id);
                        if(m) menu_open(&L, m);
                    } else if(e == IE_BACK || e == IE_MENU){
                        L.sel = 0;
                    }
                    continue;
                }
                if(e == IE_LEFT){
                    if(L.focus > 0) L.focus--;
                } else if(e == IE_RIGHT){
                    if(L.focus < 2) L.focus++;
                } else if(e == IE_UP || e == IE_DOWN){
                    if(L.focus < 2)
                        nav_step(L.focus == 0 ? &L.gn : &L.cn, e,
                                 L.focus == 0 ? v.ng + 1 : v.nt);
                    else {
                        L.stick = 0;
                        L.top_px += (e == IE_UP ? -3 : 3) * MV_LINE_H;
                        scroll_clamp(&L);
                    }
                } else if(e == IE_PGUP){
                    if(L.focus == 2 && v.active){
                        L.stick = 0;
                        L.top_px -= CHAT_VIEW_H - 2 * MV_LINE_H;
                        scroll_clamp(&L);
                        if(L.top_px == 0 && L.mv.n > 0 && L.chan_loaded)
                            fetch_history(&L, L.chan_loaded, 1);
                    }
                } else if(e == IE_PGDN){
                    if(L.focus == 2){
                        int max = L.mv.height - CHAT_VIEW_H;
                        if(max < 0) max = 0;
                        L.top_px += CHAT_VIEW_H - 2 * MV_LINE_H;
                        scroll_clamp(&L);
                        if(L.top_px >= max) L.stick = 1;
                    }
                } else if(e == IE_BOTTOM){
                    if(L.focus == 2){
                        L.stick = 1;
                        L.top_px = L.mv.height;
                        scroll_clamp(&L);
                    }
                } else if(e == IE_SELECT){
                    if(L.focus == 2 && v.active){
                        char text[1024];
                        char tpath[96];
                        memset(text, 0, sizeof text);
                        snprintf(tpath, sizeof tpath,
                                 "/api/v9/channels/%llu/typing",
                                 (unsigned long long)v.active->id);
                        rest_send(st->rest, "POST", tpath, NULL, NULL, NULL);
                        if(ime_prompt("Message", v.active->name, text, sizeof text))
                            send_message(&L, v.active->id, text, 0, 0);
                    }
                } else if(e == IE_VIEW){
                    if(L.focus == 2 && v.active)
                        open_viewer(&L, v.active->id);
                } else if(e == IE_MENU){
                    if(L.focus == 2 && v.active)
                        sel_begin(&L, v.active->id);
                } else if(e == IE_PINS){
                    if(L.focus == 2 && v.active)
                        pins_open(&L, v.active->id);
                } else if(e == IE_SEARCH){
                    if(L.focus == 2 && v.active)
                        search_prompt(&L, v.active->id, 0);
                } else if(e == IE_BACK)
                    log_msg("input: back unbound");
            }
        }

        view_build(&L, &v);
        ui_frame_begin();
        draw_main(&L, &v);
        if(L.viewer) draw_viewer(&L);
        if(L.menu) draw_menu(&L);
        if(L.react_pick) draw_react_pick(&L);
        if(L.pins) draw_pins(&L);
        if(L.search) draw_search(&L);
        ui_frame_end();
        ui_pump();
        usleep(15000);
    }

    free(L.recv);
    ws_close(&L.ws);
    return auth_failed ? 1 : 0;
}
