/* main.c - orbis-discord entry point.
 * Boot: logging -> core stores -> TLS http -> login (QR or saved token) ->
 * gateway. The channel UI loop replaces the idle loop in Task 9. */
#include "log.h"
#include "store.h"
#include "gateway.h"
#include "rest.h"
#include "http_tls.h"
#include "ui/renderer.h"
#include "ui/screens.h"
#include "platform/ime.h"
#include "platform/token.h"
#include <stdio.h>
#include <string.h>

int main(void){
    store *st;
    gateway *gw;
    app_state app;

    log_init("/data/orbis-discord/orbis-discord.log");
    log_msg("orbis-discord starting");

    st = store_new();
    if(!st){ log_msg("store alloc fail"); return 1; }

    memset(&app, 0, sizeof app);
    app.st = st;
    app.http = http_tls_new("discord.com", 443);
    if(!app.http){ log_msg("http alloc fail"); return 1; }
    app.rest = rest_new(app.http, "");
    if(!app.rest){ log_msg("rest alloc fail"); return 1; }
    if(!ui_init()){ log_msg("ui init fail"); return 1; }
    ime_init();

    if(!token_load(app.token, sizeof app.token)){
        log_msg("no saved token; login required");
        if(screen_login_run(&app) != 1){
            log_msg("login abandoned; exiting");
            return 0;
        }
        if(!token_save(app.token)) log_msg("token save failed");
    }
    log_msg("token ready (%dB)", (int)strlen(app.token));

    rest_free(app.rest);
    app.rest = rest_new(app.http, app.token);
    gw = gw_new(st, app.token);
    if(!gw){ log_msg("gateway alloc fail"); return 1; }
    app.gw = gw;
    log_msg("core ready (hb interval %d ms)", gw_interval_ms(gw));

    screen_main_run(&app);
    return 0;
}
