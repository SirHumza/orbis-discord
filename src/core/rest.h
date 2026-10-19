#ifndef ORBIS_REST_H
#define ORBIS_REST_H
#include <stdint.h>
#include <stddef.h>
#include "http.h"

typedef void (*rest_cb)(int status, const char *body, void *ud);
typedef struct rest rest;

rest *rest_new(http *, const char *token);
void rest_free(rest *);
int rest_send(rest *, const char *method, const char *path, const char *body,
              rest_cb cb, void *ud);
/* GET /channels/{chan}/messages?limit=50[&before={before}] */
int rest_get_messages(rest *, uint64_t chan, uint64_t before, rest_cb cb, void *ud);
/* GET /guilds/{guild}/members?limit=100 */
int rest_get_guild_members(rest *, uint64_t guild, rest_cb cb, void *ud);
/* GET /users/@me/channels */
int rest_get_dm_channels(rest *, rest_cb cb, void *ud);
int rest_pump(rest *, uint64_t now_ms);
uint64_t rest_blocked_until(rest *);
#endif
