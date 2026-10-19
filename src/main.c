/* main.c - orbis-discord entry point (phase 1 scaffold).
 * Boots logging + core stores. UI loop lands with the SDL2 screen work. */
#include "log.h"
#include "store.h"
#include "gateway.h"
#include "clock.h"
#include <unistd.h>

int main(void){
    store *st;
    gateway *gw;
    log_init("/data/orbis-discord/orbis-discord.log");
    log_msg("orbis-discord starting");
    st = store_new();
    if(!st){ log_msg("store alloc fail"); return 1; }
    gw = gw_new(st, "");
    if(!gw){ log_msg("gateway alloc fail"); store_free(st); return 1; }
    log_msg("core ready (hb interval %d ms)", gw_interval_ms(gw));
    for(;;) sleep(1);
    return 0;
}
