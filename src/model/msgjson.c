#include "msgjson.h"
#include "cdnurl.h"
#include "snowflake.h"
#include <stdio.h>
#include <string.h>

static jl_val_t *field(jl_val_t *o, const char *k){
    return o && o->type == JL_OBJECT ? jl_obj_get(o, k) : NULL;
}

static const char *jstr(jl_val_t *v){
    return v && v->type == JL_STRING ? v->str : "";
}

static uint64_t jid(jl_val_t *v){
    return v && v->type == JL_STRING ? sn_parse(v->str) : 0;
}

static int jint(jl_val_t *v){
    return v && v->type == JL_NUMBER ? (int)v->num : 0;
}

static int jbool(jl_val_t *v){
    return v && v->type == JL_BOOL ? (int)v->num : 0;
}

static size_t copyb(char *dst, size_t o, size_t cap, const char *src, size_t n){
    if(o >= cap - 1) return o;
    if(n > cap - 1 - o) n = cap - 1 - o;
    memcpy(dst + o, src, n);
    return o + n;
}

static const char *member_name(store *st, uint64_t chan, uint64_t user){
    const channel *ch = store_channel(st, chan);
    const member *ms[200];
    size_t n, i;
    if(!ch || !ch->guild) return NULL;
    n = store_members(st, ch->guild, ms, 200);
    for(i = 0; i < n; i++)
        if(ms[i]->user == user) return ms[i]->name;
    return NULL;
}

void msg_resolve_mentions(store *st, message *m, uint64_t self_id){
    char out[sizeof m->content];
    size_t o = 0;
    const char *s;
    if(!m) return;
    m->mentions_me = 0;
    s = m->content;
    while(*s){
        if(s[0] == '<' && s[1] == '@'){
            int role = s[2] == '&';
            const char *p = s + (s[2] == '!' || role ? 3 : 2);
            const char *q = p;
            uint64_t id = 0;
            while(*q >= '0' && *q <= '9'){
                id = id * 10 + (uint64_t)(*q - '0');
                q++;
            }
            if(q > p && *q == '>'){
                o = copyb(out, o, sizeof out, "@", 1);
                if(role){
                    o = copyb(out, o, sizeof out, "role", 4);
                } else {
                    const char *nm = member_name(st, m->chan, id);
                    if(nm) o = copyb(out, o, sizeof out, nm, strlen(nm));
                    else o = copyb(out, o, sizeof out, p, (size_t)(q - p));
                    if(id == self_id) m->mentions_me = 1;
                }
                s = q + 1;
                continue;
            }
        }
        if(s[0] == '<' && s[1] == '#'){
            const char *q = s + 2;
            const channel *ch;
            uint64_t id = 0;
            while(*q >= '0' && *q <= '9'){
                id = id * 10 + (uint64_t)(*q - '0');
                q++;
            }
            if(q > s + 2 && *q == '>'){
                ch = store_channel(st, id);
                o = copyb(out, o, sizeof out, "#", 1);
                if(ch && ch->name[0])
                    o = copyb(out, o, sizeof out, ch->name, strlen(ch->name));
                else
                    o = copyb(out, o, sizeof out, s + 2, (size_t)(q - (s + 2)));
                s = q + 1;
                continue;
            }
        }
        if(s[0] == '<' && (s[1] == ':' || (s[1] == 'a' && s[2] == ':'))){
            const char *p = s + (s[1] == 'a' ? 3 : 2);
            const char *e = strchr(p, ':');
            if(e && e[1] >= '0' && e[1] <= '9'){
                const char *q = e + 1;
                while(*q >= '0' && *q <= '9') q++;
                if(*q == '>'){
                    o = copyb(out, o, sizeof out, ":", 1);
                    o = copyb(out, o, sizeof out, p, (size_t)(e - p));
                    o = copyb(out, o, sizeof out, ":", 1);
                    s = q + 1;
                    continue;
                }
            }
        }
        if(o >= sizeof out - 1) break;
        out[o++] = *s++;
    }
    out[o] = 0;
    memcpy(m->content, out, o + 1);
}

int msg_from_json(message *m, jl_val_t *o){
    jl_val_t *a, *atts, *e;
    int i;
    if(!m || !o || o->type != JL_OBJECT) return 0;
    memset(m, 0, sizeof *m);
    m->id = jid(field(o, "id"));
    m->chan = jid(field(o, "channel_id"));
    if(!m->id || !m->chan) return 0;
    a = field(o, "author");
    m->author_id = jid(field(a, "id"));
    snprintf(m->author_name, sizeof m->author_name, "%s",
             jstr(field(a, "username")));
    {
        jl_val_t *av = field(a, "avatar");
        cdn_avatar_url(m->author_avatar, sizeof m->author_avatar, m->author_id,
                       av && av->type == JL_STRING ? av->str : "");
    }
    snprintf(m->content, sizeof m->content, "%s", jstr(field(o, "content")));
    {
        jl_val_t *ref = field(o, "message_reference");
        if(ref && ref->type == JL_OBJECT){
            jl_val_t *refm = field(o, "referenced_message");
            m->reply_to = jid(field(ref, "message_id"));
            if(m->reply_to && refm && refm->type == JL_OBJECT){
                const char *c = jstr(field(field(refm, "author"), "username"));
                size_t k = 0;
                snprintf(m->reply_name, sizeof m->reply_name, "%s", c);
                c = jstr(field(refm, "content"));
                for(; *c && k < sizeof m->reply_text - 1; c++)
                    m->reply_text[k++] = *c == '\n' ? ' ' : *c;
                m->reply_text[k] = 0;
            }
        }
    }
    e = field(o, "edited_timestamp");
    if(e && e->type == JL_STRING && e->str[0]) m->has_edit = 1;
    atts = field(o, "attachments");
    if(atts && atts->type == JL_ARRAY){
        for(i = 0; i < atts->count && m->natt < MSG_MAX_ATT; i++){
            jl_val_t *aj = jl_arr_at(atts, i);
            attachment *at = &m->att[m->natt];
            const char *ct = jstr(field(aj, "content_type"));
            snprintf(at->url, sizeof at->url, "%s", jstr(field(aj, "url")));
            snprintf(at->name, sizeof at->name, "%s", jstr(field(aj, "filename")));
            at->size = jint(field(aj, "size"));
            at->w = jint(field(aj, "width"));
            at->h = jint(field(aj, "height"));
            if(!at->url[0]) continue;
            at->image = strncmp(ct, "image/", 6) == 0 &&
                        strncmp(ct, "image/svg", 9) != 0 &&
                        at->w > 0 && at->h > 0;
            m->natt++;
        }
    }
    {
        jl_val_t *embs = field(o, "embeds");
        if(embs && embs->type == JL_ARRAY){
            for(i = 0; i < embs->count && m->nemb < MSG_MAX_EMB; i++){
                jl_val_t *ej = jl_arr_at(embs, i);
                embed *e = &m->emb[m->nemb];
                jl_val_t *img = field(ej, "image");
                jl_val_t *th = field(ej, "thumbnail");
                jl_val_t *col = field(ej, "color");
                snprintf(e->title, sizeof e->title, "%s", jstr(field(ej, "title")));
                snprintf(e->desc, sizeof e->desc, "%s", jstr(field(ej, "description")));
                snprintf(e->url, sizeof e->url, "%s", jstr(field(ej, "url")));
                snprintf(e->site, sizeof e->site, "%s",
                         jstr(field(field(ej, "provider"), "name")));
                snprintf(e->author, sizeof e->author, "%s",
                         jstr(field(field(ej, "author"), "name")));
                snprintf(e->footer, sizeof e->footer, "%s",
                         jstr(field(field(ej, "footer"), "text")));
                if(img && jstr(field(img, "url"))[0]){
                    snprintf(e->image_url, sizeof e->image_url, "%s",
                             jstr(field(img, "url")));
                    e->image_w = jint(field(img, "width"));
                    e->image_h = jint(field(img, "height"));
                } else if(th && jstr(field(th, "url"))[0]){
                    snprintf(e->image_url, sizeof e->image_url, "%s",
                             jstr(field(th, "url")));
                    e->image_w = jint(field(th, "width"));
                    e->image_h = jint(field(th, "height"));
                }
                if(col && col->type == JL_NUMBER){
                    e->has_color = 1;
                    e->color = 0xFF000000u | ((uint32_t)col->num & 0xFFFFFFu);
                }
                {
                    jl_val_t *flds = field(ej, "fields");
                    if(flds && flds->type == JL_ARRAY){
                        int k;
                        for(k = 0; k < flds->count &&
                                  e->nfields < MSG_EMB_FIELDS; k++){
                            jl_val_t *fj = jl_arr_at(flds, k);
                            embed_field *ff = &e->field[e->nfields];
                            snprintf(ff->name, sizeof ff->name, "%s",
                                     jstr(field(fj, "name")));
                            snprintf(ff->value, sizeof ff->value, "%s",
                                     jstr(field(fj, "value")));
                            if(ff->name[0] || ff->value[0]) e->nfields++;
                        }
                    }
                }
                if(e->title[0] || e->desc[0] || e->image_url[0] ||
                   e->site[0] || e->author[0] || e->nfields > 0) m->nemb++;
            }
        }
    }
    {
        jl_val_t *sts = field(o, "sticker_items");
        if(sts && sts->type == JL_ARRAY){
            for(i = 0; i < sts->count && m->natt < MSG_MAX_ATT; i++){
                jl_val_t *sj = jl_arr_at(sts, i);
                attachment *at = &m->att[m->natt];
                int fmt = jint(field(sj, "format_type"));
                uint64_t id = jid(field(sj, "id"));
                if(!id) continue;
                snprintf(at->name, sizeof at->name, "%s",
                         jstr(field(sj, "name")));
                if(fmt == 3){
                    at->image = 0;
                } else {
                    snprintf(at->url, sizeof at->url,
                             "https://cdn.discordapp.com/stickers/%llu.%s",
                             (unsigned long long)id, fmt == 4 ? "gif" : "png");
                    at->w = at->h = 160;
                    at->image = 1;
                }
                m->natt++;
            }
        }
    }
    {
        jl_val_t *rs = field(o, "reactions");
        if(rs && rs->type == JL_ARRAY){
            for(i = 0; i < rs->count && m->nreact < MSG_MAX_REACT; i++){
                jl_val_t *rj = jl_arr_at(rs, i);
                reaction *r = &m->react[m->nreact];
                jl_val_t *em = field(rj, "emoji");
                int c = jint(field(rj, "count"));
                snprintf(r->emoji, sizeof r->emoji, "%s", jstr(field(em, "name")));
                if(!r->emoji[0]) continue;
                r->count = c > 0 ? c : 1;
                r->me = jbool(field(rj, "me"));
                m->nreact++;
            }
        }
    }
    return 1;
}

int emoji_from_json(emoji_item *e, jl_val_t *o){
    uint64_t id;
    if(!e || !o || o->type != JL_OBJECT) return 0;
    id = jid(field(o, "id"));
    if(!id) return 0;
    memset(e, 0, sizeof *e);
    e->id = id;
    snprintf(e->name, sizeof e->name, "%s", jstr(field(o, "name")));
    e->animated = jbool(field(o, "animated"));
    return 1;
}

int recipients_from_json(jl_val_t *arr, recip_item *out, int cap){
    int n = 0, i;
    if(!arr || arr->type != JL_ARRAY || !out || cap <= 0) return 0;
    for(i = 0; i < arr->count && n < cap; i++){
        jl_val_t *o = jl_arr_at(arr, i);
        uint64_t id;
        const char *gname, *uname;
        if(!o || o->type != JL_OBJECT) continue;
        id = jid(field(o, "id"));
        if(!id) continue;
        memset(&out[n], 0, sizeof out[n]);
        out[n].id = id;
        gname = jstr(field(o, "global_name"));
        uname = jstr(field(o, "username"));
        snprintf(out[n].name, sizeof out[n].name, "%s",
                 gname[0] ? gname : (uname[0] ? uname : "user"));
        cdn_avatar_url(out[n].avatar, sizeof out[n].avatar, id,
                       jstr(field(o, "avatar")));
        n++;
    }
    return n;
}

int overwrites_from_json(jl_val_t *arr, channel *ch){
    int n = 0, i;
    if(!ch) return 0;
    ch->now = 0;
    if(!arr || arr->type != JL_ARRAY) return 0;
    for(i = 0; i < arr->count && n < CHAN_MAX_OW; i++){
        jl_val_t *o = jl_arr_at(arr, i);
        uint64_t id;
        if(!o || o->type != JL_OBJECT) continue;
        id = jid(field(o, "id"));
        if(!id) continue;
        ch->ow[n].id = id;
        ch->ow[n].type = jint(field(o, "type"));
        ch->ow[n].allow = jid(field(o, "allow"));
        ch->ow[n].deny = jid(field(o, "deny"));
        n++;
    }
    ch->now = n;
    return n;
}

int invites_from_json(jl_val_t *arr, invite_item *out, int cap){
    int n = 0, i;
    if(!arr || arr->type != JL_ARRAY || !out || cap <= 0) return 0;
    for(i = 0; i < arr->count && n < cap; i++){
        jl_val_t *o = jl_arr_at(arr, i);
        const char *code;
        if(!o || o->type != JL_OBJECT) continue;
        code = jstr(field(o, "code"));
        if(!code[0]) continue;
        memset(&out[n], 0, sizeof out[n]);
        snprintf(out[n].code, sizeof out[n].code, "%s", code);
        out[n].uses = jint(field(o, "uses"));
        out[n].max_uses = jint(field(o, "max_uses"));
        out[n].max_age = jint(field(o, "max_age"));
        n++;
    }
    return n;
}

int threads_from_json(jl_val_t *obj, uint64_t parent, thread_item *out,
                      int cap){
    jl_val_t *arr;
    int n = 0, i;
    if(!obj || obj->type != JL_OBJECT || !out || cap <= 0) return 0;
    arr = jl_obj_get(obj, "threads");
    if(!arr || arr->type != JL_ARRAY) return 0;
    for(i = 0; i < arr->count && n < cap; i++){
        jl_val_t *o = jl_arr_at(arr, i);
        uint64_t id, par;
        const char *name;
        if(!o || o->type != JL_OBJECT) continue;
        id = jid(field(o, "id"));
        par = jid(field(o, "parent_id"));
        if(!id || par != parent) continue;
        name = jstr(field(o, "name"));
        memset(&out[n], 0, sizeof out[n]);
        out[n].id = id;
        snprintf(out[n].name, sizeof out[n].name, "%s", name);
        out[n].messages = jint(field(o, "message_count"));
        n++;
    }
    return n;
}
