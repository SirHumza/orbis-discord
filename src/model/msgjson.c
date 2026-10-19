#include "msgjson.h"
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
    return 1;
}
