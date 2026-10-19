#include "events.h"
#include "gateway.h"
#include "snowflake.h"
#include "store.h"
#include "../model/cdnurl.h"
#include "../model/msgjson.h"
#include <string.h>
#include <stdio.h>

extern const char *gw_string(jl_val_t *, const char *);
extern uint64_t gw_u64(jl_val_t *, const char *);
extern int gw_num(jl_val_t *, const char *);
extern jl_val_t *gw_obj(jl_val_t *, const char *);

static void cp(char *dst, size_t cap, const char *src){
    snprintf(dst, cap, "%s", src ? src : "");
}

static jl_val_t *at(jl_val_t *arr, size_t i){
    return arr && arr->type == JL_ARRAY ? jl_arr_at(arr, i) : NULL;
}
static size_t count(jl_val_t *arr){
    return arr && arr->type == JL_ARRAY ? (size_t)arr->count : 0;
}

static void put_guild(store *st, jl_val_t *gobj){
    guild gu;
    jl_val_t *chans, *members;
    size_t i;
    uint64_t gid;
    if(!gobj) return;
    gid = gw_u64(gobj, "id");
    if(!gid) return;
    memset(&gu, 0, sizeof gu);
    gu.id = gid;
    cp(gu.name, sizeof gu.name, gw_string(gobj, "name"));
    cdn_guild_icon_url(gu.icon_url, sizeof gu.icon_url, gid, gw_string(gobj, "icon"));
    store_put_guild(st, &gu);

    chans = gw_obj(gobj, "channels");
    for(i = 0; i < count(chans); i++){
        jl_val_t *c = at(chans, i);
        channel ch;
        memset(&ch, 0, sizeof ch);
        ch.id = gw_u64(c, "id");
        if(!ch.id) continue;
        ch.guild = gw_u64(c, "guild_id");
        if(!ch.guild) ch.guild = gid;
        ch.type = gw_num(c, "type");
        ch.position = gw_num(c, "position");
        ch.nsfw = gw_obj(c, "nsfw") && gw_obj(c, "nsfw")->type == JL_BOOL ? (int)gw_obj(c, "nsfw")->num : 0;
        cp(ch.name, sizeof ch.name, gw_string(c, "name"));
        store_put_channel(st, &ch);
    }

    members = gw_obj(gobj, "members");
    for(i = 0; i < count(members); i++){
        jl_val_t *m = at(members, i);
        jl_val_t *u = gw_obj(m, "user");
        member mem;
        memset(&mem, 0, sizeof mem);
        mem.user = gw_u64(u, "id");
        if(!mem.user) continue;
        mem.guild = gid;
        cp(mem.name, sizeof mem.name, gw_string(u, "username"));
        cdn_avatar_url(mem.avatar_url, sizeof mem.avatar_url, mem.user,
                       gw_string(u, "avatar"));
        mem.online = gw_string(m, "status")[0] != 0 && strcmp(gw_string(m, "status"), "offline") != 0;
        store_put_member(st, &mem);
    }
}

static int on_ready(gateway *g, jl_val_t *d){
    store *st = gw_store(g);
    jl_val_t *user, *guilds, *pcs;
    size_t i;
    user = gw_obj(d, "user");
    gw_on_ready(g, gw_string(d, "session_id"), gw_string(user, "username"), gw_u64(user, "id"));
    guilds = gw_obj(d, "guilds");
    for(i = 0; i < count(guilds); i++) put_guild(st, at(guilds, i));
    pcs = gw_obj(d, "private_channels");
    for(i = 0; i < count(pcs); i++){
        jl_val_t *c = at(pcs, i);
        channel ch;
        memset(&ch, 0, sizeof ch);
        ch.id = gw_u64(c, "id");
        if(!ch.id) continue;
        ch.guild = 0;
        ch.type = gw_num(c, "type");
        ch.position = (int)i;
        cp(ch.name, sizeof ch.name, gw_string(c, "name"));
        store_put_channel(st, &ch);
    }
    return GW_EV_READY;
}

static int on_message_create(gateway *g, jl_val_t *d){
    message m;
    if(!msg_from_json(&m, d)) return GW_EV_NONE;
    store_put_message(gw_store(g), &m);
    return GW_EV_MESSAGE;
}

static int on_message_update(gateway *g, jl_val_t *d){
    message m;
    const message *old;
    if(!msg_from_json(&m, d)) return GW_EV_NONE;
    old = store_messages(gw_store(g), m.chan);
    while(old && old->id != m.id) old = old->next;
    if(old){
        if(!m.author_id) m.author_id = old->author_id;
        if(!m.author_name[0]) memcpy(m.author_name, old->author_name, sizeof m.author_name);
        if(m.natt == 0){
            m.natt = old->natt;
            memcpy(m.att, old->att, sizeof m.att);
        }
    }
    m.has_edit = 1;
    store_put_message(gw_store(g), &m);
    return GW_EV_MESSAGE;
}

static int on_message_delete(gateway *g, jl_val_t *d){
    uint64_t id = gw_u64(d, "id"), chan = gw_u64(d, "channel_id");
    if(!id || !chan) return GW_EV_NONE;
    store_del_message(gw_store(g), chan, id);
    return GW_EV_MESSAGE;
}

static int on_guild_create(gateway *g, jl_val_t *d){
    put_guild(gw_store(g), d);
    return GW_EV_READY;
}

static int on_presence(gateway *g, jl_val_t *d){
    uint64_t guild = gw_u64(d, "guild_id");
    uint64_t user = gw_u64(gw_obj(d, "user"), "id");
    const char *status = gw_string(d, "status");
    if(!guild || !user) return GW_EV_NONE;
    store_set_member_online(gw_store(g), guild, user, status[0] && strcmp(status, "offline") != 0);
    return GW_EV_PRESENCE;
}

static int on_channel_put(gateway *g, jl_val_t *d){
    channel ch;
    memset(&ch, 0, sizeof ch);
    ch.id = gw_u64(d, "id");
    if(!ch.id) return GW_EV_NONE;
    ch.guild = gw_u64(d, "guild_id");
    ch.type = gw_num(d, "type");
    ch.position = gw_num(d, "position");
    cp(ch.name, sizeof ch.name, gw_string(d, "name"));
    store_put_channel(gw_store(g), &ch);
    return GW_EV_NONE;
}

static int on_member_update(gateway *g, jl_val_t *d){
    uint64_t guild = gw_u64(d, "guild_id");
    jl_val_t *u = gw_obj(d, "user");
    uint64_t user = gw_u64(u, "id");
    if(!guild || !user) return GW_EV_NONE;
    if(gw_string(u, "username")[0]) store_set_member_name(gw_store(g), guild, user, gw_string(u, "username"));
    return GW_EV_PRESENCE;
}

int events_dispatch(gateway *g, const char *t, jl_val_t *d){
    if(!g || !t || !d) return GW_EV_NONE;
    if(!strcmp(t, "READY")) return on_ready(g, d);
    if(!strcmp(t, "MESSAGE_CREATE")) return on_message_create(g, d);
    if(!strcmp(t, "MESSAGE_UPDATE")) return on_message_update(g, d);
    if(!strcmp(t, "MESSAGE_DELETE")) return on_message_delete(g, d);
    if(!strcmp(t, "GUILD_CREATE")) return on_guild_create(g, d);
    if(!strcmp(t, "PRESENCE_UPDATE")) return on_presence(g, d);
    if(!strcmp(t, "CHANNEL_CREATE") || !strcmp(t, "CHANNEL_UPDATE")) return on_channel_put(g, d);
    if(!strcmp(t, "GUILD_MEMBER_UPDATE")) return on_member_update(g, d);
    return GW_EV_NONE;
}
