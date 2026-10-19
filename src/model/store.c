#include "store.h"
#include "snowflake.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define GROUP_WINDOW_MS 300000ULL

typedef struct {
    uint64_t id;
    message *head, *tail;
} chan_msgs;

struct store {
    chan_msgs *chans;
    size_t nchans, capchans;
    channel *channels;
    size_t nchannels, capchannels;
    guild *guilds;
    size_t nguilds, capguilds;
    member *members;
    size_t nmembers, capmembers;
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
        cur->edited_at = in->edited_at;
        cur->has_edit = in->has_edit;
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
}

const message *store_messages(store *st, uint64_t chan){
    chan_msgs *c = st ? msgs_for(st, chan, 0) : NULL;
    return c ? c->head : NULL;
}

void store_put_channel(store *st, const channel *in){
    size_t i;
    if(!st || !in) return;
    for(i = 0; i < st->nchannels; i++)
        if(st->channels[i].id == in->id){ st->channels[i] = *in; return; }
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
