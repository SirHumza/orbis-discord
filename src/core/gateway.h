#ifndef ORBIS_GATEWAY_H
#define ORBIS_GATEWAY_H
#include <stdint.h>
#include <stddef.h>
#include "store.h"

#define GW_EV_NONE 0
#define GW_EV_READY 1
#define GW_EV_MESSAGE 2
#define GW_EV_PRESENCE 4
#define GW_EV_RECONNECT 8
#define GW_EV_INVALID_SESSION 16
#define GW_EV_HELLO 32

typedef struct gateway gateway;

gateway *gw_new(store *, const char *token);
void gw_free(gateway *);
int gw_feed(gateway *, const char *json, size_t len);
const char *gw_self_name(gateway *);
uint64_t gw_self_id(gateway *);
int gw_identify(gateway *, char *out, size_t cap);
int gw_resume(gateway *, char *out, size_t cap);
const char *gw_ack(gateway *, size_t *len);
int gw_expired_heartbeat(gateway *);
int gw_heartbeat_due(gateway *, uint64_t now_ms);
void gw_mark_beat(gateway *, uint64_t now_ms);
int gw_interval_ms(gateway *);
int gw_seq(gateway *);
int gw_state(gateway *);
void gw_force_resume(gateway *);
void gw_on_ready(gateway *, const char *session_id, const char *name, uint64_t id);
store *gw_store(gateway *);
#endif
