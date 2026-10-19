#include "tenor.h"
#include "../core/jsonlite.h"
#include <string.h>
#include <stdio.h>

int gif_from_tenor(const char *body, gif_item *out, int max){
    jl_val_t *v, *results;
    int i, n = 0;
    if(!body || !out || max <= 0) return 0;
    v = jl_parse(body, strlen(body));
    results = v && v->type == JL_OBJECT ? jl_obj_get(v, "results") : NULL;
    if(results && results->type == JL_ARRAY){
        for(i = 0; i < results->count && n < max; i++){
            jl_val_t *r = jl_arr_at(results, i);
            jl_val_t *fm = r ? jl_obj_get(r, "media_formats") : NULL;
            jl_val_t *gif = fm ? jl_obj_get(fm, "gif") : NULL;
            jl_val_t *tiny = fm ? jl_obj_get(fm, "tinygif") : NULL;
            jl_val_t *url = gif ? jl_obj_get(gif, "url") : NULL;
            jl_val_t *turl = tiny ? jl_obj_get(tiny, "url") : NULL;
            jl_val_t *dims = gif ? jl_obj_get(gif, "dims") : NULL;
            gif_item *g = &out[n];
            if(!url || url->type != JL_STRING || !url->str[0]) continue;
            memset(g, 0, sizeof *g);
            snprintf(g->url, sizeof g->url, "%s", url->str);
            snprintf(g->preview, sizeof g->preview, "%s",
                     turl && turl->type == JL_STRING && turl->str[0]
                         ? turl->str : url->str);
            if(dims && dims->type == JL_ARRAY && dims->count >= 2){
                jl_val_t *dw = jl_arr_at(dims, 0);
                jl_val_t *dh = jl_arr_at(dims, 1);
                if(dw && dw->type == JL_NUMBER) g->w = (int)dw->num;
                if(dh && dh->type == JL_NUMBER) g->h = (int)dh->num;
            }
            n++;
        }
    }
    jl_free(v);
    return n;
}
