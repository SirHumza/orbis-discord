#ifndef ORBIS_STORE_H
#define ORBIS_STORE_H
#include <stdint.h>
#include <stddef.h>

#define MSG_MAX_ATT 4

typedef struct {
    char url[320];
    char name[96];
    int w, h, size;
    int image;
} attachment;

typedef struct message {
    uint64_t id, chan, author_id;
    char author_name[64];
    char author_avatar[128];
    char content[2048];
    uint64_t edited_at;
    int has_edit;
    int is_grouped;
    int pending;
    uint64_t nonce;
    attachment att[MSG_MAX_ATT];
    int natt;
    struct message *next;
} message;

typedef struct channel {
    uint64_t id, guild;
    int type;
    char name[100];
    char avatar_url[128];
    int nsfw, position;
} channel;

typedef struct guild {
    uint64_t id;
    char name[100];
    char icon_url[256];
} guild;

typedef struct member {
    uint64_t user, guild;
    char name[64];
    char avatar_url[128];
    int online;
} member;

typedef struct store store;

store *store_new(void);
void store_free(store *);
void store_put_message(store *, const message *);
void store_del_message(store *, uint64_t chan, uint64_t id);
void store_del_pending(store *, uint64_t chan, uint64_t nonce);
const message *store_messages(store *, uint64_t chan);
uint64_t store_revision(store *);
void store_put_channel(store *, const channel *);
const channel *store_channel(store *, uint64_t id);
void store_put_guild(store *, const guild *);
void store_put_member(store *, const member *);
size_t store_channels_for_guild(store *, uint64_t guild_id, uint64_t *out, size_t cap);
size_t store_dm_channels(store *, const channel **out, size_t cap);
size_t store_guilds(store *, const guild **out, size_t cap);
size_t store_members(store *, uint64_t guild_id, const member **out, size_t cap);
void store_set_member_online(store *, uint64_t guild_id, uint64_t user_id, int online);
void store_set_member_name(store *, uint64_t guild_id, uint64_t user_id, const char *name);
#endif
