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
                if(e->title[0] || e->desc[0] || e->image_url[0]) m->nemb++;
            }
        }
    }
    return 1;
}
