#include "rest.h"
#include "jsonlite.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define REST_QMAX 64

typedef struct {
    char method[8];
    char path[512];
    char *body;
    rest_cb cb;
    void *ud;
} req;

struct rest {
    http *h;
    char token[512];
    req q[REST_QMAX];
    int head, tail;
    uint64_t blocked_until;
};

rest *rest_new(http *h, const char *token){
    rest *r = calloc(1, sizeof *r);
    if(!r) return NULL;
    r->h = h;
    snprintf(r->token, sizeof r->token, "%s", token ? token : "");
    return r;
}

void rest_free(rest *r){
    if(!r) return;
    while(r->head != r->tail){
        free(r->q[r->head].body);
        r->head = (r->head + 1) % REST_QMAX;
    }
    free(r);
}

int rest_send(rest *r, const char *method, const char *path, const char *body,
              rest_cb cb, void *ud){
    req *q;
    int next;
    if(!r || !method || !path) return -1;
    next = (r->tail + 1) % REST_QMAX;
    if(next == r->head) return -2;
    q = &r->q[r->tail];
    snprintf(q->method, sizeof q->method, "%s", method);
    snprintf(q->path, sizeof q->path, "%s", path);
    q->body = body ? strdup(body) : NULL;
    q->cb = cb;
    q->ud = ud;
    r->tail = next;
    return 0;
}

int rest_get_messages(rest *r, uint64_t chan, uint64_t before, rest_cb cb, void *ud){
    char path[160];
    if(before)
        snprintf(path, sizeof path,
                 "/api/v9/channels/%llu/messages?limit=50&before=%llu",
                 (unsigned long long)chan, (unsigned long long)before);
    else
        snprintf(path, sizeof path, "/api/v9/channels/%llu/messages?limit=50",
                 (unsigned long long)chan);
    return rest_send(r, "GET", path, NULL, cb, ud);
}

static uint64_t retry_after_ms(const char *body){
    jl_val_t *v = body ? jl_parse(body, strlen(body)) : NULL;
    uint64_t ms = 1000;
    if(v){
        jl_val_t *ra = jl_obj_get(v, "retry_after");
        if(ra && ra->type == JL_NUMBER) ms = (uint64_t)(ra->num * 1000.0);
        jl_free(v);
    }
    return ms;
}

uint64_t rest_blocked_until(rest *r){ return r ? r->blocked_until : 0; }

int rest_pump(rest *r, uint64_t now_ms){
    int processed = 0;
    if(!r) return 0;
    while(r->head != r->tail){
        req *q;
        http_resp resp;
        int rc;
        if(now_ms < r->blocked_until) break;
        q = &r->q[r->head];
        rc = http_request(r->h, "discord.com", q->method, q->path, q->body, r->token, &resp);
        processed++;
        free(q->body);
        q->body = NULL;
        r->head = (r->head + 1) % REST_QMAX;
        if(rc != 0){
            if(q->cb) q->cb(0, NULL, q->ud);
            continue;
        }
        if(resp.status == 429){
            r->blocked_until = now_ms + retry_after_ms(resp.body);
            http_resp_free(&resp);
            continue;
        }
        if(q->cb) q->cb(resp.status, resp.body, q->ud);
        http_resp_free(&resp);
    }
    return processed;
}
