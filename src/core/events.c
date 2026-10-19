#include "events.h"
#include "gateway.h"
#include "clock.h"
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

static uint64_t top_hoisted_role(const guild *gu, jl_val_t *ids){
    uint64_t top = 0;
    int best = -1;
    size_t k;
    int i;
    if(!gu) return 0;
    for(k = 0; k < count(ids); k++){
        jl_val_t *rv = at(ids, k);
        uint64_t rid = rv && rv->type == JL_STRING ? sn_parse(rv->str) : 0;
        for(i = 0; i < gu->nroles; i++){
            if(gu->roles[i].id == rid && gu->roles[i].hoist &&
               gu->roles[i].position > best){
                best = gu->roles[i].position;
                top = gu->roles[i].id;
            }
        }
    }
    return top;
}

static uint32_t role_color(const guild *gu, jl_val_t *ids){
    uint32_t color = 0;
    int best = -1;
    size_t k;
    int i;
    if(!gu) return 0;
    for(k = 0; k < count(ids); k++){
        jl_val_t *rv = at(ids, k);
        uint64_t rid = rv && rv->type == JL_STRING ? sn_parse(rv->str) : 0;
        for(i = 0; i < gu->nroles; i++){
            if(gu->roles[i].id == rid && gu->roles[i].color &&
               gu->roles[i].position > best){
                best = gu->roles[i].position;
                color = gu->roles[i].color;
            }
        }
    }
    return color ? (0xFF000000u | color) : 0;
}

static void guild_self_perms(gateway *g, guild *gu, jl_val_t *gobj){
    uint64_t self = gw_self_id(g);
    jl_val_t *members = gw_obj(gobj, "members");
    size_t i, k;
    int j;
    if(!self) return;
    for(j = 0; j < gu->nroles; j++)
        if(gu->roles[j].id == gu->id) gu->my_perms |= gu->roles[j].perms;
    for(i = 0; i < count(members); i++){
        jl_val_t *m = at(members, i);
        jl_val_t *u = gw_obj(m, "user");
        jl_val_t *roles;
        if(gw_u64(u, "id") != self) continue;
        roles = gw_obj(m, "roles");
        for(k = 0; k < count(roles) && gu->my_nroles < 16; k++){
            jl_val_t *rv = at(roles, k);
            uint64_t rid = rv && rv->type == JL_STRING ? sn_parse(rv->str) : 0;
            if(!rid) continue;
            gu->my_roles[gu->my_nroles++] = rid;
            for(j = 0; j < gu->nroles; j++)
                if(gu->roles[j].id == rid) gu->my_perms |= gu->roles[j].perms;
        }
        break;
    }
}

static void put_guild(gateway *g, jl_val_t *gobj){
    store *st = gw_store(g);
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
    {
        jl_val_t *roles = gw_obj(gobj, "roles");
        for(i = 0; i < count(roles) && gu.nroles < GUILD_MAX_ROLES; i++){
            jl_val_t *rj = at(roles, i);
            role *r = &gu.roles[gu.nroles];
            r->id = gw_u64(rj, "id");
            if(!r->id) continue;
            r->color = (uint32_t)gw_num(rj, "color");
            r->position = gw_num(rj, "position");
            r->perms = gw_u64(rj, "permissions");
            cp(r->name, sizeof r->name, gw_string(rj, "name"));
            {
                jl_val_t *h = gw_obj(rj, "hoist");
                r->hoist = h && h->type == JL_BOOL && h->num;
            }
            gu.nroles++;
        }
    }
    guild_self_perms(g, &gu, gobj);
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
        ch.parent = gw_u64(c, "parent_id");
        ch.nsfw = gw_obj(c, "nsfw") && gw_obj(c, "nsfw")->type == JL_BOOL ? (int)gw_obj(c, "nsfw")->num : 0;
        cp(ch.name, sizeof ch.name, gw_string(c, "name"));
        overwrites_from_json(gw_obj(c, "permission_overwrites"), &ch);
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
        mem.color = role_color(&gu, gw_obj(m, "roles"));
        mem.top_role = top_hoisted_role(&gu, gw_obj(m, "roles"));
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
    for(i = 0; i < count(guilds); i++) put_guild(g, at(guilds, i));
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
    msg_resolve_mentions(gw_store(g), &m, gw_self_id(g));
    store_put_message(gw_store(g), &m);
    if(m.author_id != gw_self_id(g)) gw_note_msg_sound(g, m.mentions_me);
    return GW_EV_MESSAGE;
}

static int on_message_update(gateway *g, jl_val_t *d){
    message m;
    const message *old;
    if(!msg_from_json(&m, d)) return GW_EV_NONE;
    msg_resolve_mentions(gw_store(g), &m, gw_self_id(g));
    old = store_messages(gw_store(g), m.chan);
    while(old && old->id != m.id) old = old->next;
    if(old){
        if(!m.author_id) m.author_id = old->author_id;
        if(!m.author_name[0]) memcpy(m.author_name, old->author_name, sizeof m.author_name);
        if(m.natt == 0){
            m.natt = old->natt;
            memcpy(m.att, old->att, sizeof m.att);
        }
        if(m.nemb == 0){
            m.nemb = old->nemb;
            memcpy(m.emb, old->emb, sizeof m.emb);
        }
        if(m.nreact == 0){
            m.nreact = old->nreact;
            memcpy(m.react, old->react, sizeof m.react);
        }
        if(!m.reply_to){
            m.reply_to = old->reply_to;
            memcpy(m.reply_name, old->reply_name, sizeof m.reply_name);
            memcpy(m.reply_text, old->reply_text, sizeof m.reply_text);
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
    put_guild(g, d);
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
    const channel *old;
    memset(&ch, 0, sizeof ch);
    ch.id = gw_u64(d, "id");
    if(!ch.id) return GW_EV_NONE;
    ch.guild = gw_u64(d, "guild_id");
    ch.type = gw_num(d, "type");
    ch.position = gw_num(d, "position");
    ch.parent = gw_u64(d, "parent_id");
    cp(ch.name, sizeof ch.name, gw_string(d, "name"));
    {
        jl_val_t *ows = gw_obj(d, "permission_overwrites");
        if(ows && ows->type == JL_ARRAY){
            overwrites_from_json(ows, &ch);
        } else {
            old = store_channel(gw_store(g), ch.id);
            if(old){
                memcpy(ch.ow, old->ow, sizeof ch.ow);
                ch.now = old->now;
            }
        }
    }
    if(!ch.name[0] || !ch.guild){
        old = store_channel(gw_store(g), ch.id);
        if(old){
            if(!ch.name[0]) cp(ch.name, sizeof ch.name, old->name);
            cp(ch.avatar_url, sizeof ch.avatar_url, old->avatar_url);
            if(!ch.guild) ch.guild = old->guild;
            if(!ch.type) ch.type = old->type;
        }
    }
    store_put_channel(gw_store(g), &ch);
    return GW_EV_NONE;
}

static int on_member_update(gateway *g, jl_val_t *d){
    uint64_t guild = gw_u64(d, "guild_id");
    jl_val_t *u = gw_obj(d, "user");
    jl_val_t *roles = gw_obj(d, "roles");
    uint64_t user = gw_u64(u, "id");
    if(!guild || !user) return GW_EV_NONE;
    if(gw_string(u, "username")[0]) store_set_member_name(gw_store(g), guild, user, gw_string(u, "username"));
    if(roles && roles->type == JL_ARRAY){
        const struct guild *gu = store_guild(gw_store(g), guild);
        store_set_member_color(gw_store(g), guild, user,
                               role_color(gu, roles));
        store_set_member_top_role(gw_store(g), guild, user,
                                  top_hoisted_role(gu, roles));
        if(gu && user == gw_self_id(g)){
            uint64_t p = 0, ids[16];
            int n = 0, j;
            size_t k;
            for(j = 0; j < gu->nroles; j++)
                if(gu->roles[j].id == guild) p |= gu->roles[j].perms;
            for(k = 0; k < count(roles) && n < 16; k++){
                jl_val_t *rv = at(roles, k);
                uint64_t rid = rv && rv->type == JL_STRING
                                   ? sn_parse(rv->str) : 0;
                if(!rid) continue;
                ids[n++] = rid;
                for(j = 0; j < gu->nroles; j++)
                    if(gu->roles[j].id == rid) p |= gu->roles[j].perms;
            }
            store_set_guild_perms(gw_store(g), guild, p, ids, n);
        }
    }
    return GW_EV_PRESENCE;
}

static int on_react(gateway *g, jl_val_t *d, int add){
    uint64_t chan = gw_u64(d, "channel_id");
    uint64_t msg = gw_u64(d, "message_id");
    uint64_t user = gw_u64(d, "user_id");
    const char *emoji = gw_string(gw_obj(d, "emoji"), "name");
    int me;
    if(!chan || !msg || !emoji[0]) return GW_EV_NONE;
    me = user && user == gw_self_id(g);
    if(add) store_react_add(gw_store(g), chan, msg, emoji, me);
    else store_react_remove(gw_store(g), chan, msg, emoji, me);
    return GW_EV_MESSAGE;
}

static int on_react_clear(gateway *g, jl_val_t *d){
    uint64_t chan = gw_u64(d, "channel_id");
    uint64_t msg = gw_u64(d, "message_id");
    if(!chan || !msg) return GW_EV_NONE;
    store_react_clear(gw_store(g), chan, msg);
    return GW_EV_MESSAGE;
}

static int on_react_clear_emoji(gateway *g, jl_val_t *d){
    uint64_t chan = gw_u64(d, "channel_id");
    uint64_t msg = gw_u64(d, "message_id");
    const char *emoji = gw_string(gw_obj(d, "emoji"), "name");
    if(!chan || !msg || !emoji[0]) return GW_EV_NONE;
    store_react_clear_emoji(gw_store(g), chan, msg, emoji);
    return GW_EV_MESSAGE;
}

static int on_typing(gateway *g, jl_val_t *d){
    uint64_t chan = gw_u64(d, "channel_id");
    uint64_t user = gw_u64(d, "user_id");
    const char *name = gw_string(gw_obj(gw_obj(d, "member"), "user"), "username");
    if(!name[0]) name = gw_string(gw_obj(d, "user"), "username");
    if(!chan || !user) return GW_EV_NONE;
    store_typing(gw_store(g), chan, user, name, orbis_mono_s());
    return GW_EV_NONE;
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
    if(!strcmp(t, "MESSAGE_REACTION_ADD")) return on_react(g, d, 1);
    if(!strcmp(t, "MESSAGE_REACTION_REMOVE")) return on_react(g, d, 0);
    if(!strcmp(t, "MESSAGE_REACTION_REMOVE_ALL")) return on_react_clear(g, d);
    if(!strcmp(t, "MESSAGE_REACTION_REMOVE_EMOJI")) return on_react_clear_emoji(g, d);
    if(!strcmp(t, "TYPING_START")) return on_typing(g, d);
    return GW_EV_NONE;
}
