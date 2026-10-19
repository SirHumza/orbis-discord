#include "store.h"
#include "snowflake.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define GROUP_WINDOW_MS 300000ULL
#define TYPING_MAX 8
#define TYPING_TTL_S 8

typedef struct {
    uint64_t id;
    message *head, *tail;
} chan_msgs;

typedef struct {
    uint64_t chan, user;
    char name[64];
    int64_t expires;
} typing_entry;

struct store {
    chan_msgs *chans;
    size_t nchans, capchans;
    channel *channels;
    size_t nchannels, capchannels;
    guild *guilds;
    size_t nguilds, capguilds;
    member *members;
    size_t nmembers, capmembers;
    uint64_t rev;
    typing_entry typing[TYPING_MAX];
};

store *store_new(void){ return calloc(1, sizeof(store)); }

void store_free(store *st){
    size_t i;
    if(!st) return;
    for(i = 0; i < st->nchans; i++){
        message *m = st->chans[i].head;
        while(m){ message *n = m->next; free(m); m = n; }
    }
    free(st->chans); free(st->channels); free(st->guilds); free(st->members);
    free(st);
}

static chan_msgs *msgs_for(store *st, uint64_t chan, int create){
    size_t i;
    for(i = 0; i < st->nchans; i++)
        if(st->chans[i].id == chan) return &st->chans[i];
    if(!create) return NULL;
    if(st->nchans == st->capchans){
        size_t nc = st->capchans ? st->capchans * 2 : 8;
        chan_msgs *p = realloc(st->chans, nc * sizeof *p);
        if(!p) return NULL;
        st->chans = p; st->capchans = nc;
    }
    st->chans[st->nchans].id = chan;
    st->chans[st->nchans].head = st->chans[st->nchans].tail = NULL;
    return &st->chans[st->nchans++];
}

static int can_group(const message *prev, const message *m){
    uint64_t a, b;
    if(!prev || prev->author_id != m->author_id) return 0;
    a = sn_timestamp_ms(prev->id); b = sn_timestamp_ms(m->id);
    return b >= a && b - a <= GROUP_WINDOW_MS;
}

void store_put_message(store *st, const message *in){
    chan_msgs *c;
    message *m, *prev = NULL, *cur;
    if(!st || !in) return;
    c = msgs_for(st, in->chan, 1);
    if(!c) return;
    cur = c->head;
    while(cur && cur->id < in->id){ prev = cur; cur = cur->next; }
    if(cur && cur->id == in->id){
        cur->author_id = in->author_id;
        memcpy(cur->author_name, in->author_name, sizeof cur->author_name);
        memcpy(cur->content, in->content, sizeof cur->content);
        cur->reply_to = in->reply_to;
        memcpy(cur->reply_name, in->reply_name, sizeof cur->reply_name);
        memcpy(cur->reply_text, in->reply_text, sizeof cur->reply_text);
        cur->edited_at = in->edited_at;
        cur->has_edit = in->has_edit;
        cur->natt = in->natt;
        memcpy(cur->att, in->att, sizeof cur->att);
        cur->nemb = in->nemb;
        memcpy(cur->emb, in->emb, sizeof cur->emb);
        cur->nreact = in->nreact;
        memcpy(cur->react, in->react, sizeof cur->react);
        st->rev++;
        return;
    }
    m = malloc(sizeof *m);
    if(!m) return;
    *m = *in;
    m->next = cur;
    m->author_name[sizeof m->author_name - 1] = 0;
    m->content[sizeof m->content - 1] = 0;
    m->is_grouped = can_group(prev, m);
    if(prev) prev->next = m; else c->head = m;
    if(!cur) c->tail = m;
    if(cur) cur->is_grouped = can_group(m, cur);
    st->rev++;
}

void store_del_message(store *st, uint64_t chan, uint64_t id){
    chan_msgs *c;
    message *prev = NULL, *cur;
    if(!st) return;
    c = msgs_for(st, chan, 0);
    if(!c) return;
    cur = c->head;
    while(cur && cur->id != id){ prev = cur; cur = cur->next; }
    if(!cur) return;
    if(prev) prev->next = cur->next; else c->head = cur->next;
    if(c->tail == cur) c->tail = prev;
    if(cur->next && prev) cur->next->is_grouped = can_group(prev, cur->next);
    else if(cur->next) cur->next->is_grouped = 0;
    free(cur);
    st->rev++;
}

void store_del_pending(store *st, uint64_t chan, uint64_t nonce){
    chan_msgs *c;
    message *prev = NULL, *cur;
    if(!st) return;
    c = msgs_for(st, chan, 0);
    if(!c) return;
    cur = c->head;
    while(cur && !(cur->pending && cur->nonce == nonce)){ prev = cur; cur = cur->next; }
    if(!cur) return;
    if(prev) prev->next = cur->next; else c->head = cur->next;
    if(c->tail == cur) c->tail = prev;
    if(cur->next && prev) cur->next->is_grouped = can_group(prev, cur->next);
    else if(cur->next) cur->next->is_grouped = 0;
    free(cur);
    st->rev++;
}

uint64_t store_revision(store *st){ return st ? st->rev : 0; }

static message *msgs_find(store *st, uint64_t chan, uint64_t id){
    chan_msgs *c = msgs_for(st, chan, 0);
    message *m;
    if(!c) return NULL;
    for(m = c->head; m && m->id != id; m = m->next){}
    return m;
}

void store_react_add(store *st, uint64_t chan, uint64_t msg, const char *emoji,
                     int me){
    message *m;
    int i;
    if(!st || !emoji || !*emoji) return;
    m = msgs_find(st, chan, msg);
    if(!m) return;
    for(i = 0; i < m->nreact; i++){
        if(strcmp(m->react[i].emoji, emoji) == 0){
            m->react[i].count++;
            if(me) m->react[i].me = 1;
            st->rev++;
            return;
        }
    }
    if(m->nreact >= MSG_MAX_REACT) return;
    snprintf(m->react[m->nreact].emoji, sizeof m->react[0].emoji, "%s", emoji);
    m->react[m->nreact].count = 1;
    m->react[m->nreact].me = me;
    m->nreact++;
    st->rev++;
}

void store_react_remove(store *st, uint64_t chan, uint64_t msg,
                        const char *emoji, int me){
    message *m;
    int i;
    if(!st || !emoji || !*emoji) return;
    m = msgs_find(st, chan, msg);
    if(!m) return;
    for(i = 0; i < m->nreact; i++){
        if(strcmp(m->react[i].emoji, emoji) == 0){
            m->react[i].count--;
            if(me) m->react[i].me = 0;
            if(m->react[i].count <= 0){
                m->nreact--;
                m->react[i] = m->react[m->nreact];
            }
            st->rev++;
            return;
        }
    }
}

void store_react_clear(store *st, uint64_t chan, uint64_t msg){
    message *m;
    if(!st) return;
    m = msgs_find(st, chan, msg);
    if(!m || m->nreact == 0) return;
    m->nreact = 0;
    st->rev++;
}

void store_react_clear_emoji(store *st, uint64_t chan, uint64_t msg,
                             const char *emoji){
    message *m;
    int i;
    if(!st || !emoji || !*emoji) return;
    m = msgs_find(st, chan, msg);
    if(!m) return;
    for(i = 0; i < m->nreact; i++){
        if(strcmp(m->react[i].emoji, emoji) == 0){
            m->nreact--;
            m->react[i] = m->react[m->nreact];
            st->rev++;
            return;
        }
    }
}

static void typing_prune(store *st, int64_t now_s){
    int i;
    for(i = 0; i < TYPING_MAX; i++)
        if(st->typing[i].user && st->typing[i].expires <= now_s)
            st->typing[i].user = 0;
}

void store_typing(store *st, uint64_t chan, uint64_t user, const char *name,
                  int64_t now_s){
    int i, victim = 0;
    if(!st || !chan || !user) return;
    typing_prune(st, now_s);
    for(i = 0; i < TYPING_MAX; i++){
        if(st->typing[i].user == user && st->typing[i].chan == chan){
            if(name && name[0])
                snprintf(st->typing[i].name, sizeof st->typing[i].name, "%s",
                         name);
            st->typing[i].expires = now_s + TYPING_TTL_S;
            return;
        }
        if(!st->typing[i].user){ victim = i; break; }
        if(st->typing[i].expires < st->typing[victim].expires) victim = i;
    }
    st->typing[victim].chan = chan;
    st->typing[victim].user = user;
    snprintf(st->typing[victim].name, sizeof st->typing[victim].name, "%s",
             name && name[0] ? name : "Someone");
    st->typing[victim].expires = now_s + TYPING_TTL_S;
}

int store_typing_count(store *st, uint64_t chan, int64_t now_s){
    int i, n = 0;
    if(!st) return 0;
    typing_prune(st, now_s);
    for(i = 0; i < TYPING_MAX; i++)
        if(st->typing[i].user && st->typing[i].chan == chan) n++;
    return n;
}

void store_typing_str(store *st, uint64_t chan, int64_t now_s, char *out,
                      size_t cap){
    int i, n = 0, total;
    char names[2][64];
    if(!out || cap == 0) return;
    out[0] = 0;
    if(!st) return;
    total = store_typing_count(st, chan, now_s);
    if(total == 0) return;
    for(i = 0; i < TYPING_MAX && n < 2; i++){
        if(!st->typing[i].user || st->typing[i].chan != chan) continue;
        snprintf(names[n], sizeof names[n], "%s", st->typing[i].name);
        n++;
    }
    if(total == 1)
        snprintf(out, cap, "%s is typing...", names[0]);
    else if(total == 2)
        snprintf(out, cap, "%s and %s are typing...", names[0], names[1]);
    else
        snprintf(out, cap, "several people are typing...");
}

const message *store_messages(store *st, uint64_t chan){
    chan_msgs *c = st ? msgs_for(st, chan, 0) : NULL;
    return c ? c->head : NULL;
}

const message *store_message(store *st, uint64_t chan, uint64_t id){
    return st ? msgs_find(st, chan, id) : NULL;
}

const message *store_newest_message(store *st, uint64_t chan){
    chan_msgs *c = st ? msgs_for(st, chan, 0) : NULL;
    return c ? c->tail : NULL;
}

const message *store_message_step(store *st, uint64_t chan, uint64_t id, int dir){
    chan_msgs *c = st ? msgs_for(st, chan, 0) : NULL;
    message *prev = NULL, *cur;
    if(!c) return NULL;
    if(dir > 0){
        cur = msgs_find(st, chan, id);
        return cur ? cur->next : NULL;
    }
    for(cur = c->head; cur && cur->id != id; cur = cur->next) prev = cur;
    return cur ? prev : NULL;
}

void store_put_channel(store *st, const channel *in){
    size_t i;
    if(!st || !in) return;
    for(i = 0; i < st->nchannels; i++)
        if(st->channels[i].id == in->id){
            uint64_t read = st->channels[i].last_read;
            st->channels[i] = *in;
            st->channels[i].last_read = read;
            return;
        }
    if(st->nchannels == st->capchannels){
        size_t nc = st->capchannels ? st->capchannels * 2 : 16;
        channel *p = realloc(st->channels, nc * sizeof *p);
        if(!p) return;
        st->channels = p; st->capchannels = nc;
    }
    st->channels[st->nchannels++] = *in;
}

const channel *store_channel(store *st, uint64_t id){
    size_t i;
    if(!st) return NULL;
    for(i = 0; i < st->nchannels; i++)
        if(st->channels[i].id == id) return &st->channels[i];
    return NULL;
}

const guild *store_guild(store *st, uint64_t id){
    size_t i;
    if(!st) return NULL;
    for(i = 0; i < st->nguilds; i++)
        if(st->guilds[i].id == id) return &st->guilds[i];
    return NULL;
}

void store_put_guild(store *st, const guild *in){
    size_t i;
    if(!st || !in) return;
    for(i = 0; i < st->nguilds; i++)
        if(st->guilds[i].id == in->id){ st->guilds[i] = *in; return; }
    if(st->nguilds == st->capguilds){
        size_t nc = st->capguilds ? st->capguilds * 2 : 16;
        guild *p = realloc(st->guilds, nc * sizeof *p);
        if(!p) return;
        st->guilds = p; st->capguilds = nc;
    }
    st->guilds[st->nguilds++] = *in;
}

void store_put_member(store *st, const member *in){
    size_t i;
    if(!st || !in) return;
    for(i = 0; i < st->nmembers; i++)
        if(st->members[i].user == in->user && st->members[i].guild == in->guild){
            st->members[i] = *in; return;
        }
    if(st->nmembers == st->capmembers){
        size_t nc = st->capmembers ? st->capmembers * 2 : 32;
        member *p = realloc(st->members, nc * sizeof *p);
        if(!p) return;
        st->members = p; st->capmembers = nc;
    }
    st->members[st->nmembers++] = *in;
}

static int chan_cmp(const void *a, const void *b){
    const channel *x = a, *y = b;
    if(x->position != y->position) return x->position - y->position;
    return x->id < y->id ? -1 : (x->id > y->id);
}

size_t store_channels_for_guild(store *st, uint64_t guild_id, uint64_t *out, size_t cap){
    size_t i, n = 0;
    channel *tmp;
    if(!st || !out || !cap) return 0;
    tmp = malloc(st->nchannels * sizeof *tmp);
    if(!tmp && st->nchannels) return 0;
    for(i = 0; i < st->nchannels; i++)
        if(st->channels[i].guild == guild_id) tmp[n++] = st->channels[i];
    qsort(tmp, n, sizeof *tmp, chan_cmp);
    if(n > cap) n = cap;
    for(i = 0; i < n; i++) out[i] = tmp[i].id;
    free(tmp);
    return n;
}

static int chan_ptr_cmp(const void *a, const void *b){
    const channel *x = *(const channel *const *)a, *y = *(const channel *const *)b;
    if(x->position != y->position) return x->position - y->position;
    return x->id < y->id ? -1 : (x->id > y->id);
}

size_t store_dm_channels(store *st, const channel **out, size_t cap){
    const channel **tmp;
    size_t i, n = 0;
    if(!st || !out || !cap) return 0;
    tmp = malloc(st->nchannels * sizeof *tmp);
    if(!tmp && st->nchannels) return 0;
    for(i = 0; i < st->nchannels; i++)
        if(st->channels[i].guild == 0 &&
           (st->channels[i].type == 1 || st->channels[i].type == 3))
            tmp[n++] = &st->channels[i];
    qsort(tmp, n, sizeof *tmp, chan_ptr_cmp);
    if(n > cap) n = cap;
    for(i = 0; i < n; i++) out[i] = tmp[i];
    free(tmp);
    return n;
}

size_t store_guilds(store *st, const guild **out, size_t cap){
    size_t n;
    if(!st || !out) return 0;
    n = st->nguilds < cap ? st->nguilds : cap;
    for(size_t i = 0; i < n; i++) out[i] = &st->guilds[i];
    return n;
}

size_t store_members(store *st, uint64_t guild_id, const member **out, size_t cap){
    size_t i, n = 0;
    if(!st || !out) return 0;
    for(i = 0; i < st->nmembers && n < cap; i++)
        if(st->members[i].guild == guild_id) out[n++] = &st->members[i];
    return n;
}

void store_set_member_online(store *st, uint64_t guild_id, uint64_t user_id, int online){
    size_t i;
    if(!st) return;
    for(i = 0; i < st->nmembers; i++)
        if(st->members[i].guild == guild_id && st->members[i].user == user_id){
            st->members[i].online = online; return;
        }
}

void store_set_member_name(store *st, uint64_t guild_id, uint64_t user_id, const char *name){
    size_t i;
    if(!st || !name) return;
    for(i = 0; i < st->nmembers; i++)
        if(st->members[i].guild == guild_id && st->members[i].user == user_id){
            snprintf(st->members[i].name, sizeof st->members[i].name, "%s", name); return;
        }
}

void store_mark_read(store *st, uint64_t chan){
    chan_msgs *c;
    size_t i;
    if(!st) return;
    c = msgs_for(st, chan, 0);
    for(i = 0; i < st->nchannels; i++)
        if(st->channels[i].id == chan){
            st->channels[i].last_read = c && c->tail ? c->tail->id : 0;
            return;
        }
}

int store_unread(store *st, uint64_t chan){
    chan_msgs *c;
    size_t i;
    if(!st) return 0;
    for(i = 0; i < st->nchannels; i++)
        if(st->channels[i].id == chan){
            c = msgs_for(st, chan, 0);
            return c && c->tail && c->tail->id > st->channels[i].last_read;
        }
    return 0;
}

void store_set_member_color(store *st, uint64_t guild_id, uint64_t user_id, uint32_t color){
    size_t i;
    if(!st) return;
    for(i = 0; i < st->nmembers; i++)
        if(st->members[i].guild == guild_id && st->members[i].user == user_id){
            st->members[i].color = color; return;
        }
}
