#include "http_tls.h"
#include "ws.h"
#include "tls.h"
#include "log.h"
#include <orbis/Net.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *host;
    int port;
    tls_ctx_t *tls;
    int fd;
} io_ctx;

static void io_reset(io_ctx *c){
    if(c->tls) tls_free(c->tls);
    if(c->fd >= 0) sceNetSocketClose(c->fd);
    c->tls = NULL;
    c->fd = -1;
}

static int io_write(void *p, const char *buf, size_t len){
    io_ctx *c = p;
    if(!c->tls){
        int fd = orbis_net_dial(c->host, c->port);
        if(fd < 0) return -1;
        c->fd = fd;
        c->tls = tls_start(fd, c->host);
        if(!c->tls){
            io_reset(c);
            return -1;
        }
    }
    return tls_write(c->tls, buf, len);
}

static int io_read(void *p, char *buf, size_t cap){
    io_ctx *c = p;
    int stalls = 0;
    if(!c->tls) return -1;
    for(;;){
        int r = tls_read(c->tls, buf, cap);
        if(r > 0) return r;
        if(r < 0){
            io_reset(c);
            return r;
        }
        if(++stalls > 6000){
            io_reset(c);
            return -1;
        }
        usleep(5000);
    }
}

http *http_tls_new(const char *host, int port){
    io_ctx *c = calloc(1, sizeof *c);
    http_io io;
    if(!c) return NULL;
    c->host = host;
    c->port = port;
    c->fd = -1;
    io.read = io_read;
    io.write = io_write;
    io.ctx = c;
    return http_new(io);
}
