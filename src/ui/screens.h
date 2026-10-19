#ifndef ORBIS_SCREENS_H
#define ORBIS_SCREENS_H
#include "../core/gateway.h"
#include "../core/http_tls.h"
#include "../core/rest.h"
#include "../model/store.h"

typedef struct app_state {
    store *st;
    gateway *gw;
    http *http;
    rest *rest;
    char token[512];
    char self_name[64];
    int online;
} app_state;

int screen_login_run(app_state *st);
#endif
