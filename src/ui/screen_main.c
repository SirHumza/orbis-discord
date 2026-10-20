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
#include "../model/multipart.h"
#include "../model/perm.h"
#include "../model/snowflake.h"
#include "../model/tenor.h"
#include "../model/voice.h"
#include "../image.h"
#include "../platform/input.h"
#include "../platform/files.h"
#include "../platform/ime.h"
#include "../platform/sound.h"
#include "../platform/tenor_net.h"
#include "../platform/voice_net.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TENOR_KEY "LIVDSRZULELA"

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

#define FRIEND_MAX 64
#define EMOJI_MAX 64
#define EMOJI_PICK 8

typedef struct {
    uint64_t id;
    char name[64];
    char avatar[128];
    int type;
} friend_entry;

typedef struct {
    uint64_t id;
    char name[64];
    int animated;
    char url[160];
} emoji_entry;

typedef struct {
    uint64_t guild;
    int unread;
    int mentions;
} guild_badge;

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
    int menu_items[6];
    int react_pick;
    int react_i;
    int emoji_pending;
    int emoji_loaded;
    int emoji_gen;
    uint64_t emoji_guild;
    int react_custom_n;
    emoji_entry react_custom[EMOJI_MAX];
    int gifs;
    int gif_n;
    int gif_i;
    uint64_t gif_chan;
    char gif_q[96];
    gif_item gif_list[16];
    int gview;
    int gview_pending;
    int gview_n;
    int gview_i;
    uint64_t gview_chan;
    recip_item gview_list[32];
    int iview;
    int iview_pending;
    int iview_n;
    int iview_i;
    uint64_t iview_chan;
    invite_item iview_list[16];
    int tview;
    int tview_pending;
    int tview_n;
    int tview_i;
    uint64_t tview_chan;
    uint64_t tview_guild;
    int tview_forum;
    thread_item tview_list[16];
    uint64_t thread_chan;
    int thread_jump;
    int upick;
    int upick_i;
    int upick_n;
    uint64_t upick_chan;
    char upick_names[32][FILE_NAME_MAX];
    int voice_state;
    ws_t vws;
    int vsock;
    uint64_t voice_chan;
    uint64_t voice_guild;
    uint64_t voice_ssrc;
    uint64_t voice_started;
    uint64_t voice_hb_at;
    uint64_t voice_discover_at;
    int voice_hb_ms;
    int voice_modes;
    int voice_port;
    char voice_ip[64];
    char voice_session[128];
    char voice_token[256];
    uint64_t badge_rev;
    uint64_t badge_last_read;
    int badge_n;
    guild_badge badges[64];
    uint64_t cat_ids[16];
    int cat_n;
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
    int friends;
    int friends_pending;
    int friends_loaded;
    friend_entry friends_list[FRIEND_MAX];
    int friend_n;
    int friend_i;
    int friend_sel[FRIEND_MAX];
    uint64_t friends_add;
    char presence[16];
    int status_menu;
    int status_i;
    int settings;
    int settings_i;
    int logout;
    int profile;
    int profile_pending;
    int profile_gen;
    int profile_bot;
    uint64_t profile_user;
    uint32_t profile_color;
    char profile_name[64];
    char profile_tag[64];
    char profile_avatar[128];
    char profile_joined[32];
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

static const char *STATUS_CHOICES[] = {
    "online", "idle", "dnd", "invisible"
};

static const char *STATUS_LABELS[] = {
    "Online", "Idle", "Do Not Disturb", "Invisible", "Settings"
};

static void send_presence(main_ctx *L){
    char payload[160];
    int n;
    if(!L->up || !L->identified || !L->presence[0]) return;
    n = gw_presence_payload(L->presence, payload, sizeof payload);
    if(n > 0) ws_send_text(&L->ws, payload, (size_t)n);
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
            jl_val_t *parent = o ? jl_obj_get(o, "parent_id") : NULL;
            jl_val_t *ows = o ? jl_obj_get(o, "permission_overwrites") : NULL;
            channel ch;
            if(!id || id->type != JL_STRING) continue;
            if(type && type->type == JL_NUMBER && (int)type->num != 0 &&
               (int)type->num != 2 && (int)type->num != 4)
                continue;
            memset(&ch, 0, sizeof ch);
            ch.id = sn_parse(id->str);
            ch.guild = f->guild;
            if(type && type->type == JL_NUMBER) ch.type = (int)type->num;
            if(pos && pos->type == JL_NUMBER) ch.position = (int)pos->num;
            if(name && name->type == JL_STRING)
                snprintf(ch.name, sizeof ch.name, "%s", name->str);
            if(nsfw && nsfw->type == JL_BOOL) ch.nsfw = (int)nsfw->num;
            if(parent && parent->type == JL_STRING)
                ch.parent = sn_parse(parent->str);
            overwrites_from_json(ows, &ch);
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

static void friends_cb(int status, const char *body, void *ud){
    main_ctx *L = ud;
    jl_val_t *v;
    int i;
    L->friends_pending = 0;
    if(status != 200){
        log_msg("rest: friends failed (%d)", status);
        set_status(L, "Friends failed");
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    L->friend_n = 0;
    if(v && v->type == JL_ARRAY){
        for(i = 0; i < v->count && L->friend_n < FRIEND_MAX; i++){
            jl_val_t *o = jl_arr_at(v, i);
            jl_val_t *type = o ? jl_obj_get(o, "type") : NULL;
            jl_val_t *u = o ? jl_obj_get(o, "user") : NULL;
            jl_val_t *uid = u ? jl_obj_get(u, "id") : NULL;
            jl_val_t *uname = u ? jl_obj_get(u, "username") : NULL;
            jl_val_t *gname = u ? jl_obj_get(u, "global_name") : NULL;
            jl_val_t *uav = u ? jl_obj_get(u, "avatar") : NULL;
            jl_val_t *rid = o ? jl_obj_get(o, "id") : NULL;
            friend_entry *fr = &L->friends_list[L->friend_n];
            uint64_t id = 0;
            if(uid && uid->type == JL_STRING) id = sn_parse(uid->str);
            if(!id && rid && rid->type == JL_STRING) id = sn_parse(rid->str);
            if(!id) continue;
            memset(fr, 0, sizeof *fr);
            fr->id = id;
            if(type && type->type == JL_NUMBER) fr->type = (int)type->num;
            if(gname && gname->type == JL_STRING && gname->str[0])
                snprintf(fr->name, sizeof fr->name, "%s", gname->str);
            else if(uname && uname->type == JL_STRING)
                snprintf(fr->name, sizeof fr->name, "%s", uname->str);
            else
                snprintf(fr->name, sizeof fr->name, "user");
            cdn_avatar_url(fr->avatar, sizeof fr->avatar, id,
                           uav && uav->type == JL_STRING ? uav->str : "");
            L->friend_n++;
        }
    }
    jl_free(v);
    L->friends_loaded = 1;
    log_msg("rest: %d relationships", L->friend_n);
}

static void friends_open(main_ctx *L){
    L->friends = 1;
    L->friend_i = 0;
    L->friends_add = 0;
    memset(L->friend_sel, 0, sizeof L->friend_sel);
    if(L->friends_loaded || L->friends_pending) return;
    L->friends_pending = 1;
    if(rest_send(L->app->rest, "GET", "/api/v9/users/@me/relationships",
                 NULL, friends_cb, L) != 0){
        L->friends_pending = 0;
        set_status(L, "Friends failed");
    }
}

typedef struct {
    main_ctx *ctx;
    uint64_t chan;
} gview_op;

static void gview_cb(int status, const char *body, void *ud){
    gview_op *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v = (status == 200 && body) ? jl_parse(body, strlen(body))
                                          : NULL;
    L->gview_pending = 0;
    L->gview_n = 0;
    if(v && v->type == JL_OBJECT)
        L->gview_n = recipients_from_json(jl_obj_get(v, "recipients"),
                                          L->gview_list, 32);
    if(status != 200) set_status(L, "Members failed");
    if(L->gview_i >= L->gview_n) L->gview_i = L->gview_n > 0
                                                  ? L->gview_n - 1 : 0;
    jl_free(v);
    free(f);
}

static void gview_fetch(main_ctx *L, uint64_t chan){
    char path[96];
    gview_op *f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = chan;
    L->gview_pending = 1;
    snprintf(path, sizeof path, "/api/v9/channels/%llu",
             (unsigned long long)chan);
    if(rest_send(L->app->rest, "GET", path, NULL, gview_cb, f) != 0){
        L->gview_pending = 0;
        free(f);
        set_status(L, "Members failed");
    }
}

static void gview_open(main_ctx *L, uint64_t chan){
    L->gview = 1;
    L->gview_i = 0;
    L->gview_n = 0;
    L->gview_chan = chan;
    gview_fetch(L, chan);
}

static void gview_remove_cb(int status, const char *body, void *ud){
    gview_op *f = ud;
    main_ctx *L = f->ctx;
    (void)body;
    if(status == 204 || status == 200){
        set_status(L, "Removed");
        gview_fetch(L, f->chan);
    } else {
        set_status(L, "Remove failed");
    }
    free(f);
}

static void gview_remove(main_ctx *L, uint64_t chan, uint64_t user){
    char path[128];
    gview_op *f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = chan;
    snprintf(path, sizeof path, "/api/v9/channels/%llu/recipients/%llu",
             (unsigned long long)chan, (unsigned long long)user);
    if(rest_send(L->app->rest, "DELETE", path, NULL, gview_remove_cb, f) != 0){
        free(f);
        set_status(L, "Remove failed");
    }
}

static void gview_add_cb(int status, const char *body, void *ud){
    main_ctx *L = ud;
    (void)body;
    if(status == 204 || status == 200) set_status(L, "Added to group");
    else set_status(L, "Add failed");
}

static void gview_add(main_ctx *L, uint64_t chan, uint64_t user){
    char path[128];
    snprintf(path, sizeof path, "/api/v9/channels/%llu/recipients/%llu",
             (unsigned long long)chan, (unsigned long long)user);
    if(rest_send(L->app->rest, "PUT", path, NULL, gview_add_cb, L) != 0)
        set_status(L, "Add failed");
}

static void iview_cb(int status, const char *body, void *ud){
    gview_op *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v = (status == 200 && body) ? jl_parse(body, strlen(body))
                                          : NULL;
    L->iview_pending = 0;
    L->iview_n = 0;
    if(v && v->type == JL_ARRAY)
        L->iview_n = invites_from_json(v, L->iview_list, 16);
    if(status != 200) set_status(L, "Invites failed");
    if(L->iview_i >= L->iview_n) L->iview_i = L->iview_n > 0
                                                  ? L->iview_n - 1 : 0;
    jl_free(v);
    free(f);
}

static void iview_fetch(main_ctx *L, uint64_t chan){
    char path[128];
    gview_op *f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = chan;
    L->iview_pending = 1;
    snprintf(path, sizeof path, "/api/v9/channels/%llu/invites",
             (unsigned long long)chan);
    if(rest_send(L->app->rest, "GET", path, NULL, iview_cb, f) != 0){
        L->iview_pending = 0;
        free(f);
        set_status(L, "Invites failed");
    }
}

static void iview_open(main_ctx *L, uint64_t chan){
    L->iview = 1;
    L->iview_i = 0;
    L->iview_n = 0;
    L->iview_chan = chan;
    iview_fetch(L, chan);
}

static void iview_create_cb(int status, const char *body, void *ud){
    gview_op *f = ud;
    main_ctx *L = f->ctx;
    (void)body;
    if(status == 200 || status == 201){
        set_status(L, "Invite created");
        iview_fetch(L, f->chan);
    } else {
        set_status(L, "Invite failed");
    }
    free(f);
}

static void iview_create(main_ctx *L, uint64_t chan){
    char path[128];
    gview_op *f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = chan;
    snprintf(path, sizeof path, "/api/v9/channels/%llu/invites",
             (unsigned long long)chan);
    if(rest_send(L->app->rest, "POST", path,
                 "{\"max_age\":86400,\"max_uses\":0}",
                 iview_create_cb, f) != 0){
        free(f);
        set_status(L, "Invite failed");
        return;
    }
    set_status(L, "Creating invite...");
}

static void iview_revoke_cb(int status, const char *body, void *ud){
    gview_op *f = ud;
    main_ctx *L = f->ctx;
    (void)body;
    if(status == 200 || status == 204){
        set_status(L, "Invite revoked");
        iview_fetch(L, f->chan);
    } else {
        set_status(L, "Revoke failed");
    }
    free(f);
}

static void iview_revoke(main_ctx *L, uint64_t chan, const char *code){
    char path[128];
    gview_op *f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = chan;
    snprintf(path, sizeof path, "/api/v9/invites/%s", code);
    if(rest_send(L->app->rest, "DELETE", path, NULL, iview_revoke_cb, f) != 0){
        free(f);
        set_status(L, "Revoke failed");
    }
}

static void tview_cb(int status, const char *body, void *ud){
    gview_op *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v = (status == 200 && body) ? jl_parse(body, strlen(body))
                                          : NULL;
    L->tview_pending = 0;
    L->tview_n = 0;
    if(v)
        L->tview_n = threads_from_json(v, L->tview_chan, L->tview_list, 16);
    if(status != 200) set_status(L, "Threads failed");
    if(L->tview_i >= L->tview_n) L->tview_i = L->tview_n > 0
                                                  ? L->tview_n - 1 : 0;
    jl_free(v);
    free(f);
}

static void tview_open(main_ctx *L, uint64_t chan, uint64_t guild){
    char path[128];
    gview_op *f;
    L->tview = 1;
    L->tview_i = 0;
    L->tview_n = 0;
    L->tview_chan = chan;
    L->tview_guild = guild;
    {
        const channel *tc = store_channel(L->app->st, chan);
        L->tview_forum = tc && tc->type == 15;
    }
    f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->chan = chan;
    L->tview_pending = 1;
    snprintf(path, sizeof path, "/api/v9/guilds/%llu/threads/active",
             (unsigned long long)guild);
    if(rest_send(L->app->rest, "GET", path, NULL, tview_cb, f) != 0){
        L->tview_pending = 0;
        free(f);
        set_status(L, "Threads failed");
    }
}

static void tview_join(main_ctx *L){
    const thread_item *t;
    channel ch;
    if(L->tview_n <= 0) return;
    t = &L->tview_list[L->tview_i];
    memset(&ch, 0, sizeof ch);
    ch.id = t->id;
    ch.guild = L->tview_guild;
    ch.type = 11;
    ch.parent = L->tview_chan;
    ch.position = 100000;
    snprintf(ch.name, sizeof ch.name, "%s", t->name);
    store_put_channel(L->app->st, &ch);
    L->thread_chan = t->id;
    L->thread_jump = 1;
    L->tview = 0;
    L->focus = 1;
}

static void voice_reset(main_ctx *L, int announce){
    if(!L->voice_state) return;
    ws_close(&L->vws);
    if(L->vsock >= 0){
        voice_udp_close(L->vsock);
        L->vsock = -1;
    }
    L->voice_state = 0;
    L->voice_chan = 0;
    L->voice_guild = 0;
    L->voice_ssrc = 0;
    L->voice_hb_ms = 0;
    L->voice_hb_at = 0;
    L->voice_modes = 0;
    if(announce) set_status(L, "Voice disconnected");
}

static void voice_fail(main_ctx *L, const char *why){
    log_msg("voice: %s", why);
    set_status(L, why);
    voice_reset(L, 0);
}

static void voice_leave(main_ctx *L){
    char buf[128];
    int n;
    if(!L->voice_state) return;
    n = voice_state_json(L->voice_guild, 0, buf, sizeof buf);
    if(n > 0 && L->up) ws_send_text(&L->ws, buf, (size_t)n);
    voice_reset(L, 1);
}

static void voice_join(main_ctx *L, uint64_t guild, uint64_t chan){
    char buf[256];
    int n;
    if(L->voice_state && L->voice_chan == chan){
        voice_leave(L);
        return;
    }
    voice_reset(L, 0);
    L->voice_chan = chan;
    L->voice_guild = guild;
    L->voice_state = 1;
    L->voice_started = mono_ms();
    n = voice_state_json(guild, chan, buf, sizeof buf);
    if(n > 0 && L->up) ws_send_text(&L->ws, buf, (size_t)n);
    set_status(L, "Joining voice...");
    log_msg("voice: joining %llu", (unsigned long long)chan);
}

static void voice_connect(main_ctx *L){
    gateway *g = L->app->gw;
    char host[80], key[32], sid[128], tok[256];
    int port = 0;
    const char *s, *t, *e;
    if(L->voice_state != 1 || !g) return;
    s = gw_voice_session(g);
    t = gw_voice_token(g);
    e = gw_voice_endpoint(g);
    if(!s[0] || !t[0] || !e[0]) return;
    if(!voice_endpoint_split(e, host, sizeof host, &port)){
        voice_fail(L, "Voice endpoint invalid");
        return;
    }
    snprintf(sid, sizeof sid, "%s", s);
    snprintf(tok, sizeof tok, "%s", t);
    if(gw_voice_guild(g)) L->voice_guild = gw_voice_guild(g);
    if(gw_voice_channel(g)) L->voice_chan = gw_voice_channel(g);
    if(ws_client_key(key, sizeof key) <= 0 ||
       ws_connect(&L->vws, host, port, "/?v=4", key, NULL) != 0){
        voice_fail(L, "Voice connect failed");
        return;
    }
    snprintf(L->voice_session, sizeof L->voice_session, "%s", sid);
    snprintf(L->voice_token, sizeof L->voice_token, "%s", tok);
    L->voice_state = 2;
    L->voice_started = mono_ms();
    set_status(L, "Voice handshake...");
    log_msg("voice: ws connected to %s:%d", host, port);
}

static void voice_handle(main_ctx *L, const char *json){
    jl_val_t *v = jl_parse(json, strlen(json));
    jl_val_t *opv, *d;
    int op;
    if(!v) return;
    opv = jl_obj_get(v, "op");
    d = jl_obj_get(v, "d");
    op = opv && opv->type == JL_NUMBER ? (int)opv->num : -1;
    if(op == 8){
        jl_val_t *iv = d ? jl_obj_get(d, "heartbeat_interval") : NULL;
        char buf[512];
        int n;
        L->voice_hb_ms = iv && iv->type == JL_NUMBER ? (int)iv->num : 41250;
        L->voice_hb_at = mono_ms() + (uint64_t)L->voice_hb_ms;
        n = voice_identify_json(L->voice_guild, gw_self_id(L->app->gw),
                                L->voice_session, L->voice_token, buf,
                                sizeof buf);
        if(n > 0) ws_send_text(&L->vws, buf, (size_t)n);
        log_msg("voice: hello, identify sent");
    } else if(op == 2){
        voice_ready r;
        unsigned char pkt[80];
        if(!voice_ready_parse(json, &r)){
            voice_fail(L, "Voice ready invalid");
        } else if((L->vsock = voice_udp_open()) < 0){
            voice_fail(L, "Voice UDP failed");
        } else if(!voice_discovery_packet(r.ssrc, pkt, sizeof pkt) ||
                  voice_udp_send(L->vsock, r.ip, r.port, pkt, 74) != 0){
            voice_fail(L, "Voice discovery failed");
        } else {
            L->voice_ssrc = r.ssrc;
            L->voice_modes = r.modes;
            L->voice_port = r.port;
            snprintf(L->voice_ip, sizeof L->voice_ip, "%s", r.ip);
            L->voice_state = 3;
            L->voice_discover_at = mono_ms() + 5000;
            log_msg("voice: ready, discovering %s:%d", r.ip, r.port);
        }
    } else if(op == 4){
        char mode[48];
        unsigned char key[64];
        size_t klen = 0;
        if(voice_session_parse(json, mode, sizeof mode, key, sizeof key,
                               &klen)){
            L->voice_state = 5;
            L->voice_hb_at = mono_ms() + (uint64_t)L->voice_hb_ms;
            set_status(L, "Voice connected");
            log_msg("voice: session (%s, %dB key)", mode, (int)klen);
        } else {
            voice_fail(L, "Voice session invalid");
        }
    }
    jl_free(v);
}

#define UPLOAD_DIR "/data/orbis-discord/downloads"
#define UPLOAD_MAX (8u * 1024u * 1024u)
#define UPLOAD_BOUNDARY "----orbisDiscord0f3a"

static const char *upload_ctype(const char *name){
    const char *dot = strrchr(name, '.');
    if(!dot) return "application/octet-stream";
    if(!strcmp(dot, ".png")) return "image/png";
    if(!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg")) return "image/jpeg";
    if(!strcmp(dot, ".gif")) return "image/gif";
    if(!strcmp(dot, ".webp")) return "image/webp";
    if(!strcmp(dot, ".mp4")) return "video/mp4";
    return "application/octet-stream";
}

static void upload_cb(int status, const char *body, void *ud){
    main_ctx *L = ud;
    (void)body;
    set_status(L, status >= 200 && status < 300 ? "File uploaded"
                                                : "Upload failed");
}

static void upick_open(main_ctx *L, uint64_t chan){
    L->upick_n = file_list(UPLOAD_DIR, L->upick_names, 32);
    L->upick_i = 0;
    L->upick_chan = chan;
    L->upick = 1;
    if(L->upick_n == 0) set_status(L, "No files in downloads");
}

static void upick_upload(main_ctx *L){
    char path[256], ctype[96], rpath[96];
    char *data = NULL, *body = NULL;
    size_t len = 0, blen = 0;
    FILE *f;
    long sz;
    if(L->upick_n <= 0) return;
    snprintf(path, sizeof path, "%s/%s", UPLOAD_DIR,
             L->upick_names[L->upick_i]);
    f = fopen(path, "rb");
    if(!f){
        set_status(L, "Open failed");
        return;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    if(sz <= 0 || sz > (long)UPLOAD_MAX){
        fclose(f);
        set_status(L, "File too large");
        return;
    }
    rewind(f);
    len = (size_t)sz;
    data = malloc(len);
    if(!data){
        fclose(f);
        set_status(L, "Upload failed");
        return;
    }
    if(fread(data, 1, len, f) != len){
        free(data);
        fclose(f);
        set_status(L, "Read failed");
        return;
    }
    fclose(f);
    if(multipart_build(UPLOAD_BOUNDARY, NULL,
                       L->upick_names[L->upick_i],
                       upload_ctype(L->upick_names[L->upick_i]), data, len,
                       &body, &blen) != 0){
        free(data);
        set_status(L, "Upload failed");
        return;
    }
    free(data);
    snprintf(ctype, sizeof ctype, "multipart/form-data; boundary=%s",
             UPLOAD_BOUNDARY);
    snprintf(rpath, sizeof rpath, "/api/v9/channels/%llu/messages",
             (unsigned long long)L->upick_chan);
    if(rest_send_ex(L->app->rest, "POST", rpath, ctype, body, blen,
                    upload_cb, L) != 0)
        set_status(L, "Upload failed");
    else
        set_status(L, "Uploading...");
    free(body);
}

typedef struct {
    main_ctx *ctx;
    uint64_t uid;
    char name[64];
    char avatar[128];
} dm_open;

static void dm_cb(int status, const char *body, void *ud){
    dm_open *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v = NULL;
    channel ch;
    const channel *dms[64];
    size_t n, i;
    int idx = -1;
    if(status == 200 && body)
        v = jl_parse(body, strlen(body));
    if(v && v->type == JL_OBJECT){
        jl_val_t *id = jl_obj_get(v, "id");
        jl_val_t *type = jl_obj_get(v, "type");
        if(id && id->type == JL_STRING){
            jl_val_t *rname = jl_obj_get(v, "name");
            jl_val_t *ricon = jl_obj_get(v, "icon");
            memset(&ch, 0, sizeof ch);
            ch.id = sn_parse(id->str);
            ch.type = type && type->type == JL_NUMBER ? (int)type->num : 1;
            ch.position = -1;
            if(ch.type == 3){
                snprintf(ch.name, sizeof ch.name, "%s",
                         rname && rname->type == JL_STRING && rname->str[0]
                             ? rname->str : f->name);
                cdn_channel_icon_url(ch.avatar_url, sizeof ch.avatar_url, ch.id,
                                     ricon && ricon->type == JL_STRING
                                         ? ricon->str : "");
            } else {
                snprintf(ch.name, sizeof ch.name, "%s", f->name);
                snprintf(ch.avatar_url, sizeof ch.avatar_url, "%s", f->avatar);
            }
            store_put_channel(L->app->st, &ch);
            n = store_dm_channels(L->app->st, dms, 64);
            for(i = 0; i < n; i++)
                if(dms[i]->id == ch.id){ idx = (int)i; break; }
            L->gn.index = 0;
            if(idx >= 0) L->cn.index = idx;
            L->focus = 2;
            L->friends = 0;
        }
    } else {
        log_msg("rest: dm open failed (%d)", status);
        set_status(L, "DM failed");
    }
    jl_free(v);
    free(f);
}

static void friend_dm(main_ctx *L, const friend_entry *fr){
    char payload[128];
    dm_open *f;
    if(!fr || fr->type == 2) return;
    snprintf(payload, sizeof payload, "{\"recipient_id\":\"%llu\"}",
             (unsigned long long)fr->id);
    f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    f->uid = fr->id;
    snprintf(f->name, sizeof f->name, "%s", fr->name);
    snprintf(f->avatar, sizeof f->avatar, "%s", fr->avatar);
    if(rest_send(L->app->rest, "POST", "/api/v9/users/@me/channels",
                 payload, dm_cb, f) != 0){
        free(f);
        set_status(L, "DM failed");
        return;
    }
    set_status(L, "Opening DM...");
}

static void group_create(main_ctx *L, const uint64_t *ids, int n){
    char payload[1664];
    dm_open *f;
    if(!ids || n < 1) return;
    if(composer_group_dm(ids, n, payload, sizeof payload) < 0){
        set_status(L, "Group DM failed");
        return;
    }
    f = calloc(1, sizeof *f);
    if(!f) return;
    f->ctx = L;
    snprintf(f->name, sizeof f->name, "Group DM");
    if(rest_send(L->app->rest, "POST", "/api/v9/users/@me/channels",
                 payload, dm_cb, f) != 0){
        free(f);
        set_status(L, "Group DM failed");
        return;
    }
    set_status(L, "Creating group...");
}

static void group_rename(main_ctx *L, const channel *ch){
    char name[100];
    char payload[256];
    char path[96];
    memset(name, 0, sizeof name);
    if(!ime_prompt("Group name", ch->name, name, sizeof name)) return;
    if(!name[0]) return;
    if(composer_name_payload(name, payload, sizeof payload) < 0) return;
    snprintf(path, sizeof path, "/api/v9/channels/%llu",
             (unsigned long long)ch->id);
    if(rest_send(L->app->rest, "PATCH", path, payload, NULL, NULL) != 0){
        set_status(L, "Rename failed");
        return;
    }
    set_status(L, "Renaming...");
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

typedef struct {
    main_ctx *ctx;
    int gen;
} emoji_fetch_op;

static void emoji_cb(int status, const char *body, void *ud){
    emoji_fetch_op *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v;
    int i;
    if(f->gen != L->emoji_gen){
        free(f);
        return;
    }
    L->emoji_pending = 0;
    if(status != 200){
        log_msg("rest: emojis failed (%d)", status);
        free(f);
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    L->react_custom_n = 0;
    if(v && v->type == JL_ARRAY){
        for(i = 0; i < v->count && L->react_custom_n < EMOJI_MAX; i++){
            emoji_item it;
            emoji_entry *e;
            if(!emoji_from_json(&it, jl_arr_at(v, i))) continue;
            e = &L->react_custom[L->react_custom_n++];
            e->id = it.id;
            snprintf(e->name, sizeof e->name, "%s", it.name);
            e->animated = it.animated;
            snprintf(e->url, sizeof e->url,
                     "https://cdn.discordapp.com/emojis/%llu.%s",
                     (unsigned long long)it.id, it.animated ? "gif" : "png");
        }
    }
    jl_free(v);
    L->emoji_loaded = 1;
    log_msg("rest: %d custom emojis", L->react_custom_n);
    free(f);
}

static void emoji_fetch(main_ctx *L, uint64_t guild){
    char path[128];
    emoji_fetch_op *f;
    if(L->emoji_pending && L->emoji_guild == guild) return;
    L->emoji_pending = 1;
    L->emoji_loaded = 0;
    L->emoji_guild = guild;
    L->emoji_gen++;
    L->react_custom_n = 0;
    f = calloc(1, sizeof *f);
    if(!f){
        L->emoji_pending = 0;
        return;
    }
    f->ctx = L;
    f->gen = L->emoji_gen;
    snprintf(path, sizeof path, "/api/v9/guilds/%llu/emojis",
             (unsigned long long)guild);
    if(rest_send(L->app->rest, "GET", path, NULL, emoji_cb, f) != 0){
        L->emoji_pending = 0;
        free(f);
    }
}

static void picker_open(main_ctx *L){
    const channel *ch = store_channel(L->app->st, L->chan_loaded);
    L->react_pick = 1;
    L->react_i = 0;
    if(ch && ch->guild &&
       (ch->guild != L->emoji_guild || !L->emoji_loaded))
        emoji_fetch(L, ch->guild);
}

static void gif_prompt(main_ctx *L){
    char q[96], enc[384], path[640], *body = NULL;
    size_t len = 0;
    memset(q, 0, sizeof q);
    if(!ime_prompt("Search GIFs", "", q, sizeof q)) return;
    if(rest_path_encode(q, enc, sizeof enc) < 0) return;
    if(q[0])
        snprintf(path, sizeof path,
                 "/v2/search?key=%s&limit=16&media_filter=tinygif,gif&q=%s",
                 TENOR_KEY, enc);
    else
        snprintf(path, sizeof path,
                 "/v2/featured?key=%s&limit=16&media_filter=tinygif,gif",
                 TENOR_KEY);
    if(tenor_net_get(path, &body, &len) != 0){
        set_status(L, "GIF search failed");
        return;
    }
    L->gif_n = gif_from_tenor(body, L->gif_list, 16);
    free(body);
    if(L->gif_n <= 0){
        set_status(L, "No GIFs found");
        return;
    }
    snprintf(L->gif_q, sizeof L->gif_q, "%s", q);
    L->gif_chan = L->chan_loaded;
    L->gif_i = 0;
    L->gifs = 1;
}

static const char *menu_label(int item){
    switch(item){
    case 1: return "Reply";
    case 2: return "Edit";
    case 3: return "Delete";
    case 4: return "React";
    case 5: return "Profile";
    default: return "Cancel";
    }
}

static void menu_open(main_ctx *L, const message *m){
    int own = m->author_id && m->author_id == gw_self_id(L->app->gw) &&
              !m->pending;
    L->menu_n = 0;
    L->menu_items[L->menu_n++] = 1;
    L->menu_items[L->menu_n++] = 4;
    L->menu_items[L->menu_n++] = 5;
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

static void profile_open(main_ctx *L, const message *m);

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
        picker_open(L);
    } else if(item == 5){
        profile_open(L, m);
    }
}

static void gw_drop(main_ctx *L, uint64_t now, const char *why){
    app_state *app = L->app;
    voice_reset(L, 0);
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

static uint64_t chan_perms(main_ctx *L, const channel *ch){
    const guild *gu;
    if(!ch || !ch->guild) return ~0ull;
    gu = store_guild(L->app->st, ch->guild);
    return perm_effective(ch, gu, gw_self_id(L->app->gw));
}

static int can_view_chan(main_ctx *L, const channel *ch){
    if(!ch || !ch->guild) return 1;
    return perm_can(chan_perms(L, ch), PERM_VIEW_CHANNEL);
}

static int can_send_chan(main_ctx *L, const channel *ch){
    if(!ch || !ch->guild) return 1;
    return perm_can(chan_perms(L, ch), PERM_SEND_MESSAGES);
}

static void forum_post_cb(int status, const char *body, void *ud){
    main_ctx *L = ud;
    (void)body;
    if(status != 200 && status != 201){
        set_status(L, "Post failed");
        return;
    }
    set_status(L, "Post created");
    if(L->tview) tview_open(L, L->tview_chan, L->tview_guild);
}

static void forum_create(main_ctx *L, uint64_t chan){
    const channel *ch = store_channel(L->app->st, chan);
    char title[128], body[1024], payload[2048], path[96];
    if(!ch || ch->type != 15) return;
    if(!can_send_chan(L, ch)){
        set_status(L, "No permission to post");
        return;
    }
    memset(title, 0, sizeof title);
    memset(body, 0, sizeof body);
    if(!ime_prompt("Post title", "", title, sizeof title)) return;
    if(!title[0]) return;
    if(!ime_prompt("Post message", "", body, sizeof body)) return;
    if(!body[0]) return;
    if(composer_forum_post(title, body, payload, sizeof payload) < 0){
        set_status(L, "Post failed");
        return;
    }
    snprintf(path, sizeof path, "/api/v9/channels/%llu/threads",
             (unsigned long long)chan);
    if(rest_send(L->app->rest, "POST", path, payload, forum_post_cb, L) != 0){
        set_status(L, "Post failed");
        return;
    }
    set_status(L, "Creating post...");
}

static int cat_collapsed(const main_ctx *L, uint64_t id){
    int i;
    for(i = 0; i < L->cat_n; i++)
        if(L->cat_ids[i] == id) return 1;
    return 0;
}

static void cat_toggle(main_ctx *L, uint64_t id){
    int i;
    for(i = 0; i < L->cat_n; i++)
        if(L->cat_ids[i] == id){
            L->cat_ids[i] = L->cat_ids[--L->cat_n];
            return;
        }
    if(L->cat_n < 16) L->cat_ids[L->cat_n++] = id;
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
        const channel *cats[16];
        int ncats = 0, c;
        int emitted[64];
        memset(emitted, 0, sizeof emitted);
        nc = store_channels_for_guild(L->app->st, v->guilds[v->gi]->id, cids, 64);
        for(i = 0; i < nc; i++){
            const channel *c2 = store_channel(L->app->st, cids[i]);
            if(c2 && c2->type == 4 && ncats < 16 && can_view_chan(L, c2))
                cats[ncats++] = c2;
        }
        for(c = 0; c < ncats; c++){
            if(v->nt < 64) v->text[v->nt++] = cats[c];
            for(i = 0; i < nc && v->nt < 64; i++){
                const channel *c2 = store_channel(L->app->st, cids[i]);
                if(!c2 || (c2->type != 0 && c2->type != 15 &&
                           c2->type != 2) ||
                   c2->parent != cats[c]->id) continue;
                emitted[i] = 1;
                if(!cat_collapsed(L, cats[c]->id) && can_view_chan(L, c2))
                    v->text[v->nt++] = c2;
            }
        }
        for(i = 0; i < nc && v->nt < 64; i++){
            const channel *c2;
            if(emitted[i]) continue;
            c2 = store_channel(L->app->st, cids[i]);
            if(c2 && (c2->type == 0 || c2->type == 15 || c2->type == 2) &&
               can_view_chan(L, c2))
                v->text[v->nt++] = c2;
        }
    }
    if(!v->dm && L->thread_chan && v->ng > 0){
        const channel *tc = store_channel(L->app->st, L->thread_chan);
        if(tc && tc->guild == v->guilds[v->gi]->id && v->nt < 64){
            v->text[v->nt++] = tc;
            if(L->thread_jump) L->cn.index = v->nt - 1;
        }
    }
    L->thread_jump = 0;
    nav_step(&L->cn, IE_NONE, v->nt);
    v->ci = nav_index(&L->cn);
    if(v->nt > 0 && v->text[v->ci]->type != 4)
        v->active = v->text[v->ci];
}

static void badges_refresh(main_ctx *L){
    store *st = L->app->st;
    const channel *lc = L->chan_loaded ? store_channel(st, L->chan_loaded)
                                       : NULL;
    uint64_t lr = lc ? lc->last_read : 0;
    uint64_t rev = store_revision(st);
    const guild *gs[64];
    size_t n, i;
    if(rev == L->badge_rev && lr == L->badge_last_read) return;
    L->badge_rev = rev;
    L->badge_last_read = lr;
    n = store_guilds(st, gs, 64);
    L->badge_n = (int)n;
    for(i = 0; i < n; i++){
        L->badges[i].guild = gs[i]->id;
        store_guild_unread(st, gs[i]->id, &L->badges[i].unread,
                           &L->badges[i].mentions);
    }
}

static void draw_member_row(const member *m, int y, int online){
    char nb[64];
    ui_rrect(MEMBER_X + 16, y + 4, 20, 20, 10, UI_SEP);
    if(m->avatar_url[0]) ui_img(m->avatar_url, MEMBER_X + 16, y + 4, 20, 20);
    ui_rect(MEMBER_X + 28, y + 16, 8, 8, online ? UI_ONLINE : UI_OFFLINE);
    trunc_utf8(nb, sizeof nb, m->name, 20);
    ui_text(MEMBER_X + 44, y, nb,
            m->color ? m->color : (online ? UI_TEXT : UI_TEXT_DIM), 16);
}

static void draw_members(main_ctx *L, const frame_view *v){
    const member *ms[256];
    const role *hr[64];
    size_t n, nr, k, i;
    int y = 52, on = 0, grouped = 0;
    char buf[96], nb[64];
    if(v->dm || v->gi < 0 || v->ng <= 0) return;
    n = store_members(L->app->st, v->guilds[v->gi]->id, ms, 256);
    for(i = 0; i < n; i++)
        if(ms[i]->online) on++;
    nr = store_guild_hoisted_roles(L->app->st, v->guilds[v->gi]->id, hr, 64);
    for(k = 0; k < nr && y < SCR_H - 40; k++){
        int cnt = 0;
        uint32_t hc;
        for(i = 0; i < n; i++)
            if(ms[i]->online && ms[i]->top_role == hr[k]->id) cnt++;
        if(!cnt) continue;
        hc = hr[k]->color ? (0xFF000000u | hr[k]->color) : UI_TEXT_DIM;
        trunc_utf8(nb, sizeof nb, hr[k]->name, 18);
        snprintf(buf, sizeof buf, "%s - %d", nb, cnt);
        ui_text(MEMBER_X + 16, y, buf, hc, 12);
        y += 22;
        for(i = 0; i < n && y < SCR_H - 40; i++){
            if(!ms[i]->online || ms[i]->top_role != hr[k]->id) continue;
            draw_member_row(ms[i], y, 1);
            y += 28;
        }
        grouped += cnt;
    }
    snprintf(buf, sizeof buf, "ONLINE - %d", on - grouped);
    ui_text(MEMBER_X + 16, y, buf, UI_TEXT_DIM, 12);
    y += 22;
    for(i = 0; i < n && y < SCR_H - 40; i++){
        if(!ms[i]->online) continue;
        if(ms[i]->top_role) continue;
        draw_member_row(ms[i], y, 1);
        y += 28;
    }
    snprintf(buf, sizeof buf, "OFFLINE - %d", (int)n - on);
    y += 8;
    ui_text(MEMBER_X + 16, y, buf, UI_TEXT_DIM, 12);
    y += 22;
    for(i = 0; i < n && y < SCR_H - 40; i++){
        if(ms[i]->online) continue;
        draw_member_row(ms[i], y, 0);
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

typedef struct {
    main_ctx *ctx;
    uint64_t chan;
    int gen;
} profile_fetch;

static void profile_cb(int status, const char *body, void *ud){
    profile_fetch *f = ud;
    main_ctx *L = f->ctx;
    jl_val_t *v = NULL, *u, *uid, *uname, *gname, *uav, *bot, *nick, *joined;
    jl_val_t *disc;
    uint64_t id;
    char tag[64];
    if(f->gen != L->profile_gen){
        free(f);
        return;
    }
    L->profile_pending = 0;
    if(status != 200 || !body){
        log_msg("rest: profile failed (%d)", status);
        set_status(L, "Profile unavailable");
        free(f);
        return;
    }
    v = jl_parse(body, strlen(body));
    if(v && v->type == JL_OBJECT){
        u = jl_obj_get(v, "user");
        if(!u || u->type != JL_OBJECT) u = v;
        uid = jl_obj_get(u, "id");
        uname = jl_obj_get(u, "username");
        gname = jl_obj_get(u, "global_name");
        uav = jl_obj_get(u, "avatar");
        bot = jl_obj_get(u, "bot");
        disc = jl_obj_get(u, "discriminator");
        nick = jl_obj_get(v, "nick");
        joined = jl_obj_get(v, "joined_at");
        id = uid && uid->type == JL_STRING ? sn_parse(uid->str)
                                           : L->profile_user;
        if(id) L->profile_user = id;
        if(nick && nick->type == JL_STRING && nick->str[0])
            snprintf(L->profile_name, sizeof L->profile_name, "%s",
                     nick->str);
        else if(gname && gname->type == JL_STRING && gname->str[0])
            snprintf(L->profile_name, sizeof L->profile_name, "%s",
                     gname->str);
        else if(uname && uname->type == JL_STRING)
            snprintf(L->profile_name, sizeof L->profile_name, "%s",
                     uname->str);
        tag[0] = 0;
        if(uname && uname->type == JL_STRING && uname->str[0]){
            snprintf(tag, sizeof tag, "@%s", uname->str);
            if(disc && disc->type == JL_STRING && disc->str[0] &&
               strcmp(disc->str, "0") != 0)
                snprintf(tag + strlen(tag), sizeof tag - strlen(tag), "#%s",
                         disc->str);
            snprintf(L->profile_tag, sizeof L->profile_tag, "%s", tag);
        }
        cdn_avatar_url(L->profile_avatar, sizeof L->profile_avatar, id,
                       uav && uav->type == JL_STRING ? uav->str : "");
        L->profile_bot = bot && bot->type == JL_BOOL && bot->num;
        L->profile_color = user_color(L->app->st, f->chan, id);
        L->profile_joined[0] = 0;
        if(joined && joined->type == JL_STRING && strlen(joined->str) >= 10){
            memcpy(L->profile_joined, joined->str, 10);
            L->profile_joined[10] = 0;
        }
    }
    jl_free(v);
    free(f);
}

static void profile_open(main_ctx *L, const message *m){
    const channel *ch;
    char path[256];
    profile_fetch *f;
    L->profile = 1;
    L->profile_pending = 1;
    L->profile_gen++;
    L->profile_user = m->author_id;
    L->profile_bot = 0;
    L->profile_color = user_color(L->app->st, m->chan, m->author_id);
    snprintf(L->profile_name, sizeof L->profile_name, "%s", m->author_name);
    snprintf(L->profile_avatar, sizeof L->profile_avatar, "%s",
             m->author_avatar);
    L->profile_tag[0] = 0;
    L->profile_joined[0] = 0;
    ch = store_channel(L->app->st, m->chan);
    if(ch && ch->guild)
        snprintf(path, sizeof path, "/api/v9/guilds/%llu/members/%llu",
                 (unsigned long long)ch->guild,
                 (unsigned long long)m->author_id);
    else
        snprintf(path, sizeof path, "/api/v9/users/%llu",
                 (unsigned long long)m->author_id);
    f = calloc(1, sizeof *f);
    if(!f){
        L->profile_pending = 0;
        return;
    }
    f->ctx = L;
    f->chan = m->chan;
    f->gen = L->profile_gen;
    if(rest_send(L->app->rest, "GET", path, NULL, profile_cb, f) != 0){
        L->profile_pending = 0;
        free(f);
        set_status(L, "Profile unavailable");
    }
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
            } else if(ln->emb_kind == 4){
                char cap[140];
                snprintf(cap, sizeof cap, "%s%s%s", e->site,
                         e->site[0] && e->author[0] ? " - " : "", e->author);
                trunc_utf8(fb, sizeof fb, cap, 84);
                ui_text(x0 + 16, y + 3, fb, UI_TEXT_DIM, 12);
            } else if(ln->emb_kind == 5){
                trunc_utf8(fb, sizeof fb, e->footer, 90);
                ui_text(x0 + 16, y + 3, fb, UI_TEXT_DIM, 12);
            } else if(ln->emb_kind == 6){
                if(ln->fld >= 0 && ln->fld < e->nfields){
                    trunc_utf8(fb, sizeof fb, e->field[ln->fld].name, 70);
                    ui_text(x0 + 16, y + 2, fb, UI_TEXT, 14);
                }
            } else if(ln->emb_kind == 7){
                if(ln->fld >= 0 && ln->fld < e->nfields){
                    int len = ln->len;
                    if(len > MV_MEASURE_MAX) len = MV_MEASURE_MAX;
                    memcpy(fb, e->field[ln->fld].value + ln->off, (size_t)len);
                    fb[len] = 0;
                    ui_text(x0 + 16, y + 2, fb, UI_TEXT_DIM, 14);
                }
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
    snprintf(cap, sizeof cap,
             "%d / %d   UP/DOWN browse   SQUARE save   CROSS close",
             L->view_i + 1, L->view_n);
    ui_text((SCR_W - ui_text_width(cap, 14)) / 2, SCR_H - 52, cap, UI_TEXT, 14);
}

static const char *friend_type_label(int type){
    switch(type){
    case 1: return "friend";
    case 2: return "blocked";
    case 3: return "incoming request";
    case 4: return "outgoing request";
    default: return "relationship";
    }
}

static void draw_status_menu(main_ctx *L){
    int n = (int)(sizeof STATUS_LABELS / sizeof STATUS_LABELS[0]);
    int w = 320, item_h = 40, h = 52 + n * item_h;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 16, y + 14, "Set status", UI_TEXT, 16);
    for(i = 0; i < n; i++){
        int iy = y + 44 + i * item_h;
        if(i == L->status_i) ui_rrect(x + 8, iy, w - 16, item_h - 6, 6,
                                      UI_BLURPLE);
        ui_text(x + 20, iy + 6, STATUS_LABELS[i], UI_TEXT, 16);
    }
}

static void draw_upick(main_ctx *L){
    int w = 640, h = 480;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i, first;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 20, y + 16, "Attach file", UI_TEXT, 18);
    if(L->upick_n == 0){
        ui_text(x + 20, y + 60, "No files in downloads", UI_TEXT_DIM, 16);
    } else {
        first = L->upick_i - 4;
        if(first < 0) first = 0;
        if(first > L->upick_n - 9) first = L->upick_n - 9;
        if(first < 0) first = 0;
        for(i = first; i < L->upick_n && i < first + 9; i++){
            int ry = y + 56 + (i - first) * 40;
            if(i == L->upick_i)
                ui_rrect(x + 12, ry - 4, w - 24, 34, 6, UI_BLURPLE);
            ui_text(x + 24, ry, L->upick_names[i], UI_TEXT, 15);
        }
    }
    ui_text(x + 20, y + h - 30, "CROSS upload   CIRCLE close",
            UI_TEXT_DIM, 12);
}

static void draw_settings(main_ctx *L){
    int w = 640, h = 480;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2;
    char line[128];
    const char *name = L->app->gw ? gw_self_name(L->app->gw) : "";
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 20, y + 16, "Settings", UI_TEXT, 18);
    snprintf(line, sizeof line, "Account: %s",
             name && name[0] ? name : "unknown");
    ui_text(x + 20, y + 52, line, UI_TEXT_DIM, 14);
    if(L->app->gw){
        snprintf(line, sizeof line, "User ID: %llu",
                 (unsigned long long)gw_self_id(L->app->gw));
        ui_text(x + 20, y + 72, line, UI_TEXT_DIM, 14);
    }
    snprintf(line, sizeof line, "Gateway: %s  Token: %s",
             L->up ? "connected" : "offline",
             L->app->token[0] ? "saved" : "none");
    ui_text(x + 20, y + 92, line, UI_TEXT_DIM, 14);
    ui_text(x + 20, y + 126, "Controls", UI_TEXT, 15);
    ui_text(x + 20, y + 148, "CROSS select/send   CIRCLE back", UI_TEXT_DIM, 13);
    ui_text(x + 20, y + 168, "TRIANGLE threads/members   SQUARE actions",
            UI_TEXT_DIM, 13);
    ui_text(x + 20, y + 188, "L2 pins   R2 search   OPTIONS friends",
            UI_TEXT_DIM, 13);
    ui_text(x + 20, y + 208, "L3 status/settings   touchpad GIF",
            UI_TEXT_DIM, 13);
    if(L->settings_i == 0)
        ui_rrect(x + 12, y + 256, w - 24, 42, 6, UI_BLURPLE);
    else if(L->settings_i == 1)
        ui_rrect(x + 12, y + 306, w - 24, 42, 6, UI_BLURPLE);
    snprintf(line, sizeof line, "Notification sounds: %s",
             sound_enabled() ? "On" : "Off");
    ui_text(x + 20, y + 268, line, UI_TEXT, 16);
    ui_text(x + 20, y + 318, "Log out", UI_TEXT, 16);
    ui_text(x + 20, y + h - 30, "CROSS choose   CIRCLE close",
            UI_TEXT_DIM, 12);
}

static void draw_friends(main_ctx *L){
    int w = 720, h = 560;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i, first;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 20, y + 16, L->friends_add ? "Add to group" : "Friends",
            UI_TEXT, 18);
    if(L->friends_pending){
        ui_text(x + 20, y + 60, "Loading friends...", UI_TEXT_DIM, 16);
    } else if(L->friend_n == 0){
        ui_text(x + 20, y + 60, "No friends", UI_TEXT_DIM, 16);
    } else {
        first = L->friend_i - 4;
        if(first < 0) first = 0;
        if(first > L->friend_n - 9) first = L->friend_n - 9;
        if(first < 0) first = 0;
        for(i = first; i < L->friend_n && i < first + 9; i++){
            const friend_entry *fr = &L->friends_list[i];
            char label[64];
            int ry = y + 56 + (i - first) * 52;
            if(i == L->friend_i)
                ui_rrect(x + 12, ry - 4, w - 24, 46, 6, UI_BLURPLE);
            ui_rrect(x + 20, ry, 32, 32, 16, UI_CHANNEL_BG);
            ui_img(fr->avatar, x + 20, ry, 32, 32);
            ui_text(x + 64, ry, fr->name, UI_TEXT, 16);
            snprintf(label, sizeof label, "%s", friend_type_label(fr->type));
            ui_text(x + 64, ry + 20, label, UI_TEXT_DIM, 12);
            if(!L->friends_add){
                ui_rrect(x + w - 54, ry + 6, 22, 22, 6,
                          L->friend_sel[i] ? UI_BLURPLE : UI_CHANNEL_BG);
                if(L->friend_sel[i])
                    ui_rect(x + w - 49, ry + 11, 12, 12, UI_TEXT);
            }
        }
    }
    if(L->friends_add)
        ui_text(x + 20, y + h - 30, "CROSS add   CIRCLE close",
                UI_TEXT_DIM, 12);
    else
        ui_text(x + 20, y + h - 30,
                "CROSS open   SQUARE select   CIRCLE close",
                UI_TEXT_DIM, 12);
}

static void draw_gview(main_ctx *L){
    int w = 640, h = 560;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i, first;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 20, y + 16, "Group members", UI_TEXT, 18);
    if(L->gview_pending){
        ui_text(x + 20, y + 60, "Loading members...", UI_TEXT_DIM, 16);
    } else if(L->gview_n == 0){
        ui_text(x + 20, y + 60, "No members", UI_TEXT_DIM, 16);
    } else {
        first = L->gview_i - 4;
        if(first < 0) first = 0;
        if(first > L->gview_n - 9) first = L->gview_n - 9;
        if(first < 0) first = 0;
        for(i = first; i < L->gview_n && i < first + 9; i++){
            const recip_item *r = &L->gview_list[i];
            int ry = y + 56 + (i - first) * 52;
            if(i == L->gview_i)
                ui_rrect(x + 12, ry - 4, w - 24, 46, 6, UI_BLURPLE);
            ui_rrect(x + 20, ry, 32, 32, 16, UI_CHANNEL_BG);
            ui_img(r->avatar, x + 20, ry, 32, 32);
            ui_text(x + 64, ry + 8, r->name, UI_TEXT, 16);
        }
    }
    ui_text(x + 20, y + h - 30,
            "CROSS remove   SQUARE add friend   CIRCLE close",
            UI_TEXT_DIM, 12);
}

static void draw_iview(main_ctx *L){
    int w = 640, h = 480;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i, first;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 20, y + 16, "Channel invites", UI_TEXT, 18);
    if(L->iview_pending){
        ui_text(x + 20, y + 60, "Loading invites...", UI_TEXT_DIM, 16);
    } else if(L->iview_n == 0){
        ui_text(x + 20, y + 60, "No active invites", UI_TEXT_DIM, 16);
    } else {
        first = L->iview_i - 4;
        if(first < 0) first = 0;
        if(first > L->iview_n - 9) first = L->iview_n - 9;
        if(first < 0) first = 0;
        for(i = first; i < L->iview_n && i < first + 9; i++){
            const invite_item *iv = &L->iview_list[i];
            char sub[64];
            int ry = y + 56 + (i - first) * 44;
            if(i == L->iview_i)
                ui_rrect(x + 12, ry - 4, w - 24, 38, 6, UI_BLURPLE);
            if(iv->max_uses)
                snprintf(sub, sizeof sub, "uses %d/%d", iv->uses,
                         iv->max_uses);
            else
                snprintf(sub, sizeof sub, "uses %d", iv->uses);
            ui_text(x + 24, ry, iv->code, UI_TEXT, 16);
            ui_text(x + 24, ry + 18, sub, UI_TEXT_DIM, 12);
        }
    }
    ui_text(x + 20, y + h - 30,
            "CROSS revoke   SQUARE create   CIRCLE close",
            UI_TEXT_DIM, 12);
}

static void draw_tview(main_ctx *L){
    int w = 640, h = 480;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i, first;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 20, y + 16, L->tview_forum ? "Forum posts" : "Active threads",
            UI_TEXT, 18);
    if(L->tview_pending){
        ui_text(x + 20, y + 60, "Loading...", UI_TEXT_DIM, 16);
    } else if(L->tview_n == 0){
        ui_text(x + 20, y + 60, L->tview_forum ? "No posts yet"
                                               : "No active threads",
                UI_TEXT_DIM, 16);
    } else {
        first = L->tview_i - 4;
        if(first < 0) first = 0;
        if(first > L->tview_n - 9) first = L->tview_n - 9;
        if(first < 0) first = 0;
        for(i = first; i < L->tview_n && i < first + 9; i++){
            const thread_item *t = &L->tview_list[i];
            char sub[64];
            int ry = y + 56 + (i - first) * 44;
            if(i == L->tview_i)
                ui_rrect(x + 12, ry - 4, w - 24, 38, 6, UI_BLURPLE);
            snprintf(sub, sizeof sub, "%d messages", t->messages);
            ui_text(x + 24, ry, t->name, UI_TEXT, 16);
            ui_text(x + 24, ry + 18, sub, UI_TEXT_DIM, 12);
        }
    }
    ui_text(x + 20, y + h - 30,
            L->tview_forum ? "CROSS open   SQUARE new post   CIRCLE close"
                           : "CROSS open   CIRCLE close",
            UI_TEXT_DIM, 12);
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
    int nu = (int)(sizeof QUICK_EMOJI / sizeof QUICK_EMOJI[0]);
    int nc = L->react_custom_n > EMOJI_PICK ? EMOJI_PICK : L->react_custom_n;
    int n = nu + nc;
    int cell = 52, w = 32 + n * cell, h = 110;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_text(x + 16, y + 10, "Add reaction", UI_TEXT, 14);
    for(i = 0; i < nu; i++){
        int cx = x + 16 + i * cell;
        if(i == L->react_i) ui_rrect(cx, y + 30, 44, 44, 8, UI_BLURPLE);
        ui_text(cx + 8, y + 38, QUICK_EMOJI[i], UI_TEXT, 28);
    }
    for(i = 0; i < nc; i++){
        const emoji_entry *e = &L->react_custom[i];
        int cx = x + 16 + (nu + i) * cell;
        if(nu + i == L->react_i)
            ui_rrect(cx, y + 30, 44, 44, 8, UI_BLURPLE);
        if(!ui_img(e->url, cx + 8, y + 38, 28, 28)){
            char nm[32];
            trunc_utf8(nm, sizeof nm, e->name, 8);
            ui_text(cx + 8, y + 42, nm, UI_TEXT_DIM, 12);
        }
    }
    if(L->emoji_pending && nc == 0)
        ui_text(x + w - 120, y + 88, "Loading emoji...", UI_TEXT_DIM, 12);
    ui_text(x + 16, y + 84, "CROSS toggle   CIRCLE close", UI_TEXT_DIM, 12);
}

static void draw_gifs(main_ctx *L){
    enum { COLS = 4 };
    int cw = 210, ch = 150, pad = 12;
    int rows = (L->gif_n + COLS - 1) / COLS;
    int w = COLS * cw + pad * 2;
    int h = 60 + rows * ch + 36;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2, i;
    char title[128], t2[64];
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    if(L->gif_q[0])
        snprintf(title, sizeof title, "GIFs: %s", L->gif_q);
    else
        snprintf(title, sizeof title, "Trending GIFs");
    trunc_utf8(t2, sizeof t2, title, 46);
    ui_text(x + pad + 8, y + 16, t2, UI_TEXT, 18);
    for(i = 0; i < L->gif_n; i++){
        const gif_item *g = &L->gif_list[i];
        int cx = x + pad + (i % COLS) * cw;
        int cy = y + 48 + (i / COLS) * ch;
        int tw = cw - 16, th = ch - 16;
        if(i == L->gif_i)
            ui_rrect(cx - 2, cy - 2, cw - 4, ch - 4, 8, UI_BLURPLE);
        if(g->w > 0 && g->h > 0){
            th = tw * g->h / g->w;
            if(th > ch - 16){
                th = ch - 16;
                tw = th * g->w / g->h;
            }
        }
        if(!ui_img(g->preview, cx + (cw - tw) / 2, cy + (ch - th) / 2,
                   tw, th))
            ui_text(cx + 8, cy + 8, "GIF", UI_TEXT_DIM, 14);
    }
    ui_text(x + pad + 8, y + h - 26,
            "CROSS send   R2 new search   CIRCLE close", UI_TEXT_DIM, 12);
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

static void draw_profile(main_ctx *L){
    int w = 560, h = 360;
    int x = (SCR_W - w) / 2, y = (SCR_H - h) / 2;
    uint32_t banner = L->profile_color != UI_TEXT ? L->profile_color
                                                  : UI_BLURPLE;
    char nb[64], nb2[64];
    int nw;
    ui_rect(0, 0, SCR_W, SCR_H, 0x80000000u);
    ui_rrect(x, y, w, h, 10, UI_CHANNEL_BG);
    ui_rrect(x, y, w, 88, 10, banner);
    ui_rect(x, y + 48, w, 40, banner);
    ui_rrect(x + 28, y + 44, 96, 96, 48, UI_CHANNEL_BG);
    ui_img(L->profile_avatar, x + 28, y + 44, 96, 96);
    trunc_utf8(nb, sizeof nb, L->profile_name, 28);
    nw = ui_text_width(nb, 24);
    ui_text(x + 144, y + 58, nb, UI_TEXT, 24);
    if(L->profile_bot){
        ui_rrect(x + 144 + nw + 10, y + 64, 40, 18, 4, UI_BLURPLE);
        ui_text(x + 144 + nw + 17, y + 64, "BOT", UI_TEXT, 11);
    }
    trunc_utf8(nb2, sizeof nb2, L->profile_tag, 34);
    if(nb2[0]) ui_text(x + 144, y + 90, nb2, UI_TEXT_DIM, 16);
    ui_rect(x + 28, y + 164, w - 56, 1, UI_SEP);
    if(L->profile_joined[0]){
        ui_text(x + 28, y + 186, "JOINED", UI_TEXT_DIM, 12);
        ui_text(x + 28, y + 204, L->profile_joined, UI_TEXT, 16);
    } else {
        ui_text(x + 28, y + 186, "DIRECT MESSAGE", UI_TEXT_DIM, 12);
    }
    snprintf(nb, sizeof nb, "User ID: %llu",
             (unsigned long long)L->profile_user);
    ui_text(x + 28, y + 244, nb, UI_TEXT_DIM, 14);
    if(L->profile_pending)
        ui_text(x + 28, y + h - 32, "Loading profile...", UI_TEXT_DIM, 12);
    else
        ui_text(x + 28, y + h - 32, "CROSS close", UI_TEXT_DIM, 12);
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
            if(i > 0){
                const guild_badge *b = NULL;
                int k;
                for(k = 0; k < L->badge_n; k++)
                    if(L->badges[k].guild == v->guilds[i - 1]->id){
                        b = &L->badges[k];
                        break;
                    }
                if(b && b->mentions > 0){
                    char cnt[8];
                    int bw;
                    if(b->mentions > 99)
                        snprintf(cnt, sizeof cnt, "99+");
                    else
                        snprintf(cnt, sizeof cnt, "%d", b->mentions);
                    bw = ui_text_width(cnt, 11) + 10;
                    if(bw < 18) bw = 18;
                    ui_rrect(12 + 48 - bw, y + 34, bw, 16, 8, 0xFFED4245u);
                    ui_text(12 + 48 - bw + (bw - ui_text_width(cnt, 11)) / 2,
                            y + 35, cnt, 0xFFFFFFFFu, 11);
                } else if(b && b->unread > 0){
                    ui_rrect(12 + 48 - 12, y + 34, 10, 10, 5, 0xFFFFFFFFu);
                }
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
        if(v->text[i]->type == 4){
            char cname[80], hbuf[96];
            int collapsed = cat_collapsed(L, v->text[i]->id);
            if(sel)
                ui_rrect(RAIL_W + 8, y, CHAN_W - 16, CHAN_ROW_H - 4, 4,
                         UI_SEP);
            trunc_utf8(cname, sizeof cname, v->text[i]->name, 20);
            snprintf(hbuf, sizeof hbuf, "%s %s", collapsed ? ">" : "v",
                     cname);
            ui_text(RAIL_W + 16, y + 8, hbuf, UI_TEXT_DIM, 13);
            continue;
        }
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
        const char *hint = v->active->type == 15
                               ? "TRIANGLE to browse forum posts"
                           : v->active->type == 2
                               ? "CROSS to join voice"
                               : (L->hist_pending ? "Loading messages..."
                                                  : "No messages yet");
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
        if(v->active && v->active->type == 15)
            snprintf(buf, sizeof buf, "Select a post to reply");
        else if(v->active && v->active->type == 2)
            snprintf(buf, sizeof buf, "Voice channel");
        else if(v->active)
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
    L.vsock = -1;
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
        if(L.logout) break;

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
                if(ev > 0){
                    int snd = gw_take_msg_sound(g);
                    if(snd == 2) sound_mention();
                    else if(snd == 1) sound_message();
                }
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
                    send_presence(&L);
                }
                if(ev & GW_EV_RECONNECT){
                    gw_drop(&L, now, "op7");
                    break;
                }
                if(ev & GW_EV_INVALID_SESSION){
                    gw_drop(&L, now, "invalid session");
                    break;
                }
                if(ev & GW_EV_VOICE_STATE){
                    if(L.voice_state == 1 && !gw_voice_channel(g))
                        voice_fail(&L, "Voice join rejected");
                    else
                        voice_connect(&L);
                }
                if(ev & GW_EV_VOICE_SERVER) voice_connect(&L);
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
        badges_refresh(&L);

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
            if(v.active->type != 15 && v.active->type != 2)
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

        if(L.voice_state == 1 && now - L.voice_started > 10000)
            voice_fail(&L, "Voice join timeout");
        if(L.voice_state >= 2){
            int vn, vop, vfin, vframes = 0;
            while(vframes < 16 &&
                  (vn = ws_recv_frame(&L.vws, L.recv, GW_RECV - 1, &vop,
                                      &vfin)) != 0){
                if(vn < 0){
                    voice_fail(&L, "Voice closed");
                    break;
                }
                vframes++;
                if(vop == 9){ ws_pong(&L.vws); continue; }
                if(vop == 8){
                    voice_fail(&L, "Voice closed");
                    break;
                }
                if(vop != 1) continue;
                L.recv[vn] = 0;
                voice_handle(&L, L.recv);
            }
            if(L.voice_state == 3){
                unsigned char pkt[128];
                char ip[64];
                int port = 0;
                int r = voice_udp_recv(L.vsock, pkt, sizeof pkt, ip,
                                       sizeof ip, &port);
                if(r > 0 && voice_discovery_parse(pkt, (size_t)r, ip,
                                                  sizeof ip, &port)){
                    const char *mode = voice_pick_mode(L.voice_modes);
                    char buf[256];
                    int m = voice_select_json(ip, port, mode, buf,
                                              sizeof buf);
                    if(m > 0){
                        ws_send_text(&L.vws, buf, (size_t)m);
                        L.voice_state = 4;
                        log_msg("voice: selected %s", mode);
                    } else {
                        voice_fail(&L, "Voice select failed");
                    }
                } else if(now > L.voice_discover_at){
                    voice_fail(&L, "Voice discovery timeout");
                }
            }
            if((L.voice_state == 4 || L.voice_state == 5) &&
               L.voice_hb_ms && now >= L.voice_hb_at){
                char buf[64];
                int m = voice_heartbeat_json(now, buf, sizeof buf);
                if(m > 0) ws_send_text(&L.vws, buf, (size_t)m);
                L.voice_hb_at = now + (uint64_t)L.voice_hb_ms;
            }
            if(L.voice_state == 5){
                unsigned char pkt[2048];
                char ip[64];
                int port = 0, k = 0;
                while(k++ < 32 &&
                      voice_udp_recv(L.vsock, pkt, sizeof pkt, ip,
                                     sizeof ip, &port) > 0){}
            }
            if(L.voice_state == 2 && now - L.voice_started > 10000)
                voice_fail(&L, "Voice handshake timeout");
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
                    if(e == IE_VIEW || e == IE_SELECT || e == IE_BACK)
                        L.viewer = 0;
                    else if(e == IE_MENU){
                        if(image_save(L.view_urls[L.view_i], NULL))
                            set_status(&L, "Saved to downloads");
                        else
                            set_status(&L, "Save failed");
                    } else if(e == IE_UP && L.view_i > 0)
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
                            if(item != 4 && item != 5) L.sel = 0;
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
                    int nu = (int)(sizeof QUICK_EMOJI / sizeof QUICK_EMOJI[0]);
                    int nc = L.react_custom_n > EMOJI_PICK ? EMOJI_PICK
                                                           : L.react_custom_n;
                    int n = nu + nc;
                    if(L.react_i >= n) L.react_i = 0;
                    if(e == IE_LEFT || e == IE_UP){
                        L.react_i = (L.react_i + n - 1) % n;
                    } else if(e == IE_RIGHT || e == IE_DOWN){
                        L.react_i = (L.react_i + 1) % n;
                    } else if(e == IE_SELECT){
                        const message *m = store_message(
                            L.app->st, L.chan_loaded, L.sel_id);
                        if(m && L.react_i < nu){
                            react_toggle(&L, m, QUICK_EMOJI[L.react_i]);
                        } else if(m){
                            const emoji_entry *ce =
                                &L.react_custom[L.react_i - nu];
                            char tag[96];
                            snprintf(tag, sizeof tag, "%s:%llu", ce->name,
                                     (unsigned long long)ce->id);
                            react_toggle(&L, m, tag);
                        }
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
                if(L.settings){
                    if(e == IE_UP || e == IE_DOWN){
                        L.settings_i += e == IE_UP ? -1 : 1;
                        if(L.settings_i < 0) L.settings_i = 1;
                        if(L.settings_i > 1) L.settings_i = 0;
                    } else if(e == IE_SELECT){
                        if(L.settings_i == 0){
                            sound_set_enabled(!sound_enabled());
                        } else {
                            L.settings = 0;
                            L.logout = 1;
                            log_msg("settings: logout");
                        }
                    } else if(e == IE_BACK || e == IE_MENU ||
                              e == IE_STATUS){
                        L.settings = 0;
                    }
                    continue;
                }
                if(L.status_menu){
                    int n = (int)(sizeof STATUS_LABELS /
                                  sizeof STATUS_LABELS[0]);
                    if(e == IE_UP || e == IE_DOWN){
                        L.status_i += e == IE_UP ? -1 : 1;
                        if(L.status_i < 0) L.status_i = n - 1;
                        if(L.status_i >= n) L.status_i = 0;
                    } else if(e == IE_SELECT){
                        if(L.status_i == n - 1){
                            L.settings = 1;
                            L.settings_i = 0;
                        } else {
                            snprintf(L.presence, sizeof L.presence, "%s",
                                     STATUS_CHOICES[L.status_i]);
                            send_presence(&L);
                        }
                        L.status_menu = 0;
                    } else if(e == IE_BACK || e == IE_MENU ||
                              e == IE_STATUS){
                        L.status_menu = 0;
                    }
                    continue;
                }
                if(L.friends){
                    if(e == IE_UP && L.friend_i > 0){
                        L.friend_i--;
                    } else if(e == IE_DOWN && L.friend_i + 1 < L.friend_n){
                        L.friend_i++;
                    } else if(e == IE_MENU && L.friend_n > 0 &&
                              !L.friends_add){
                        L.friend_sel[L.friend_i] = !L.friend_sel[L.friend_i];
                    } else if(e == IE_SELECT && L.friend_n > 0){
                        if(L.friends_add){
                            const friend_entry *fr =
                                &L.friends_list[L.friend_i];
                            if(fr->type != 2)
                                gview_add(&L, L.friends_add, fr->id);
                        } else {
                            uint64_t ids[FRIEND_MAX];
                            int k = 0, j;
                            for(j = 0; j < L.friend_n; j++)
                                if(L.friend_sel[j] &&
                                   L.friends_list[j].type != 2)
                                    ids[k++] = L.friends_list[j].id;
                            if(k > 0) group_create(&L, ids, k);
                            else friend_dm(&L, &L.friends_list[L.friend_i]);
                        }
                    } else if(e == IE_BACK || e == IE_FRIENDS){
                        L.friends = 0;
                    }
                    continue;
                }
                if(L.gview){
                    if(e == IE_UP && L.gview_i > 0){
                        L.gview_i--;
                    } else if(e == IE_DOWN && L.gview_i + 1 < L.gview_n){
                        L.gview_i++;
                    } else if(e == IE_SELECT && L.gview_n > 0){
                        gview_remove(&L, L.gview_chan,
                                     L.gview_list[L.gview_i].id);
                    } else if(e == IE_MENU){
                        friends_open(&L);
                        if(L.friends) L.friends_add = L.gview_chan;
                    } else if(e == IE_BACK || e == IE_VIEW){
                        L.gview = 0;
                    }
                    continue;
                }
                if(L.iview){
                    if(e == IE_UP && L.iview_i > 0){
                        L.iview_i--;
                    } else if(e == IE_DOWN && L.iview_i + 1 < L.iview_n){
                        L.iview_i++;
                    } else if(e == IE_SELECT && L.iview_n > 0){
                        iview_revoke(&L, L.iview_chan,
                                     L.iview_list[L.iview_i].code);
                    } else if(e == IE_MENU){
                        const channel *ic =
                            store_channel(L.app->st, L.iview_chan);
                        if(perm_can(chan_perms(&L, ic), PERM_CREATE_INVITE))
                            iview_create(&L, L.iview_chan);
                        else
                            set_status(&L, "No permission to invite");
                    } else if(e == IE_BACK || e == IE_VIEW){
                        L.iview = 0;
                    }
                    continue;
                }
                if(L.tview){
                    if(e == IE_UP && L.tview_i > 0){
                        L.tview_i--;
                    } else if(e == IE_DOWN && L.tview_i + 1 < L.tview_n){
                        L.tview_i++;
                    } else if(e == IE_SELECT && L.tview_n > 0){
                        tview_join(&L);
                    } else if(e == IE_MENU && L.tview_forum){
                        forum_create(&L, L.tview_chan);
                    } else if(e == IE_BACK || e == IE_VIEW){
                        L.tview = 0;
                    }
                    continue;
                }
                if(L.profile){
                    if(e == IE_SELECT || e == IE_BACK || e == IE_MENU){
                        L.profile = 0;
                        L.profile_gen++;
                        L.sel = 0;
                    }
                    continue;
                }
                if(L.upick){
                    if(e == IE_UP || e == IE_DOWN){
                        L.upick_i += e == IE_UP ? -1 : 1;
                        if(L.upick_i < 0) L.upick_i = L.upick_n - 1;
                        if(L.upick_i >= L.upick_n) L.upick_i = 0;
                    } else if(e == IE_SELECT){
                        if(L.upick_n > 0){
                            upick_upload(&L);
                            L.upick = 0;
                        }
                    } else if(e == IE_BACK || e == IE_MENU){
                        L.upick = 0;
                    }
                    continue;
                }
                if(L.gifs){
                    if(e == IE_UP && L.gif_i >= 4){
                        L.gif_i -= 4;
                    } else if(e == IE_DOWN && L.gif_i + 4 < L.gif_n){
                        L.gif_i += 4;
                    } else if(e == IE_LEFT && L.gif_i > 0){
                        L.gif_i--;
                    } else if(e == IE_RIGHT && L.gif_i + 1 < L.gif_n){
                        L.gif_i++;
                    } else if(e == IE_SELECT){
                        if(can_send_chan(&L,
                                         store_channel(L.app->st, L.gif_chan))){
                            send_message(&L, L.gif_chan,
                                         L.gif_list[L.gif_i].url, 0, 0);
                            L.gifs = 0;
                        } else {
                            set_status(&L, "No permission to send");
                        }
                    } else if(e == IE_GIF || e == IE_SEARCH){
                        gif_prompt(&L);
                    } else if(e == IE_BACK || e == IE_MENU){
                        L.gifs = 0;
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
                    if(L.focus == 1 && v.active &&
                       (v.active->type == 0 || v.active->type == 11)){
                        uint64_t perms = chan_perms(&L, v.active);
                        if(!perm_can(perms, PERM_SEND_MESSAGES) ||
                           !perm_can(perms, PERM_ATTACH_FILES))
                            set_status(&L, "No permission to attach");
                        else
                            upick_open(&L, v.active->id);
                    } else if(L.focus == 2 && v.active){
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
                        if(v.active->type == 15){
                            set_status(&L, "Select a post to reply");
                        } else if(v.active->type == 2){
                            set_status(&L, "Voice channel");
                        } else if(!can_send_chan(&L, v.active)){
                            set_status(&L, "No permission to send");
                        } else {
                            char text[1024];
                            char tpath[96];
                            memset(text, 0, sizeof text);
                            snprintf(tpath, sizeof tpath,
                                     "/api/v9/channels/%llu/typing",
                                     (unsigned long long)v.active->id);
                            rest_send(st->rest, "POST", tpath, NULL, NULL, NULL);
                            if(ime_prompt("Message", v.active->name, text,
                                          sizeof text))
                                send_message(&L, v.active->id, text, 0, 0);
                        }
                    } else if(L.focus == 1 && v.nt > 0 &&
                              v.text[v.ci]->type == 4){
                        cat_toggle(&L, v.text[v.ci]->id);
                    } else if(L.focus == 1 && v.nt > 0 &&
                              v.text[v.ci]->type == 15){
                        tview_open(&L, v.text[v.ci]->id,
                                   v.guilds[v.gi]->id);
                    } else if(L.focus == 1 && v.active &&
                              v.active->type == 2){
                        if(!perm_can(chan_perms(&L, v.active),
                                     PERM_CONNECT_VOICE))
                            set_status(&L, "No permission to join voice");
                        else if(L.voice_state &&
                                L.voice_chan == v.active->id)
                            voice_leave(&L);
                        else
                            voice_join(&L, v.guilds[v.gi]->id,
                                       v.active->id);
                    }
                } else if(e == IE_VIEW){
                    if(L.focus == 2 && v.active)
                        open_viewer(&L, v.active->id);
                    else if(L.focus == 1 && v.active && v.active->type == 3)
                        gview_open(&L, v.active->id);
                    else if(L.focus == 1 && v.active &&
                            (v.active->type == 0 || v.active->type == 15))
                        tview_open(&L, v.active->id, v.guilds[v.gi]->id);
                } else if(e == IE_MENU){
                    if(L.focus == 2 && v.active)
                        sel_begin(&L, v.active->id);
                    else if(L.focus == 1 && v.active && v.active->type == 3)
                        group_rename(&L, v.active);
                    else if(L.focus == 1 && v.active && v.active->type == 0)
                        iview_open(&L, v.active->id);
                    else if(L.focus == 1 && v.active && v.active->type == 15)
                        forum_create(&L, v.active->id);
                } else if(e == IE_PINS){
                    if(L.focus == 2 && v.active)
                        pins_open(&L, v.active->id);
                } else if(e == IE_SEARCH){
                    if(L.focus == 2 && v.active)
                        search_prompt(&L, v.active->id, 0);
                } else if(e == IE_FRIENDS){
                    friends_open(&L);
                } else if(e == IE_STATUS){
                    L.status_menu = 1;
                    L.status_i = 0;
                } else if(e == IE_GIF){
                    if(L.focus == 2 && v.active) gif_prompt(&L);
                } else if(e == IE_BACK){
                    if(L.voice_state) voice_leave(&L);
                    else if(L.thread_chan) L.thread_chan = 0;
                    else log_msg("input: back unbound");
                }
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
        if(L.gview) draw_gview(&L);
        if(L.iview) draw_iview(&L);
        if(L.tview) draw_tview(&L);
        if(L.friends) draw_friends(&L);
        if(L.status_menu) draw_status_menu(&L);
        if(L.settings) draw_settings(&L);
        if(L.profile) draw_profile(&L);
        if(L.gifs) draw_gifs(&L);
        if(L.upick) draw_upick(&L);
        ui_frame_end();
        ui_pump();
        usleep(15000);
    }

    free(L.recv);
    ws_close(&L.ws);
    return (auth_failed || L.logout) ? 1 : 0;
}
