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
#define GW_EV_VOICE_STATE 64
#define GW_EV_VOICE_SERVER 128

typedef struct gateway gateway;

gateway *gw_new(store *, const char *token);
void gw_free(gateway *);
int gw_feed(gateway *, const char *json, size_t len);
const char *gw_self_name(gateway *);
uint64_t gw_self_id(gateway *);
int gw_identify(gateway *, char *out, size_t cap);
int gw_presence_payload(const char *status, char *out, size_t cap);
int gw_resume(gateway *, char *out, size_t cap);
const char *gw_ack(gateway *, size_t *len);
int gw_expired_heartbeat(gateway *);
int gw_heartbeat_due(gateway *, uint64_t now_ms);
void gw_mark_beat(gateway *, uint64_t now_ms);
int gw_interval_ms(gateway *);
int gw_seq(gateway *);
int gw_state(gateway *);
void gw_force_resume(gateway *);
uint64_t gw_backoff_ms(int attempt);
void gw_schedule_reconnect(gateway *, uint64_t now_ms);
int gw_reconnect_due(gateway *, uint64_t now_ms);
void gw_on_reconnect_failed(gateway *, uint64_t now_ms);
void gw_on_reconnected(gateway *);
int gw_ack_expired(gateway *, uint64_t now_ms);
void gw_on_ready(gateway *, const char *session_id, const char *name, uint64_t id);
store *gw_store(gateway *);
const char *gw_voice_session(gateway *);
uint64_t gw_voice_channel(gateway *);
uint64_t gw_voice_guild(gateway *);
const char *gw_voice_token(gateway *);
const char *gw_voice_endpoint(gateway *);
void gw_set_voice_state(gateway *, const char *session, uint64_t guild,
                        uint64_t chan);
void gw_set_voice_server(gateway *, const char *token, const char *endpoint,
                         uint64_t guild);
/* Notification sound state: 2 = mention, 1 = message, 0 = none. */
void gw_note_msg_sound(gateway *, int mentions);
int gw_take_msg_sound(gateway *);
#endif
