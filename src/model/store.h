#ifndef ORBIS_STORE_H
#define ORBIS_STORE_H
#include <stdint.h>
#include <stddef.h>

#define MSG_MAX_ATT 4
#define MSG_MAX_EMB 2
#define GUILD_MAX_ROLES 64

typedef struct {
    uint64_t id;
    uint32_t color;
    int position;
} role;

typedef struct {
    char url[320];
    char name[96];
    int w, h, size;
    int image;
} attachment;

typedef struct {
    char title[128];
    char desc[512];
    char url[256];
    char image_url[320];
    int image_w, image_h;
    uint32_t color;
    int has_color;
} embed;

#define MSG_MAX_REACT 6

typedef struct {
    char emoji[32];
    int count;
    int me;
} reaction;

typedef struct message {
    uint64_t id, chan, author_id;
    char author_name[64];
    char author_avatar[128];
    char content[2048];
    uint64_t reply_to;
    char reply_name[64];
    char reply_text[96];
    int mentions_me;
    uint64_t edited_at;
    int has_edit;
    int is_grouped;
    int pending;
    uint64_t nonce;
    attachment att[MSG_MAX_ATT];
    int natt;
    embed emb[MSG_MAX_EMB];
    int nemb;
    reaction react[MSG_MAX_REACT];
    int nreact;
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
    role roles[GUILD_MAX_ROLES];
    int nroles;
} guild;

typedef struct member {
    uint64_t user, guild;
    char name[64];
    char avatar_url[128];
    uint32_t color;
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
const guild *store_guild(store *, uint64_t id);
void store_put_guild(store *, const guild *);
void store_put_member(store *, const member *);
size_t store_channels_for_guild(store *, uint64_t guild_id, uint64_t *out, size_t cap);
size_t store_dm_channels(store *, const channel **out, size_t cap);
size_t store_guilds(store *, const guild **out, size_t cap);
size_t store_members(store *, uint64_t guild_id, const member **out, size_t cap);
void store_set_member_online(store *, uint64_t guild_id, uint64_t user_id, int online);
void store_set_member_name(store *, uint64_t guild_id, uint64_t user_id, const char *name);
void store_set_member_color(store *, uint64_t guild_id, uint64_t user_id, uint32_t color);
void store_react_add(store *, uint64_t chan, uint64_t msg, const char *emoji, int me);
void store_react_remove(store *, uint64_t chan, uint64_t msg, const char *emoji, int me);
void store_react_clear(store *, uint64_t chan, uint64_t msg);
void store_react_clear_emoji(store *, uint64_t chan, uint64_t msg, const char *emoji);
void store_typing(store *, uint64_t chan, uint64_t user, const char *name, int64_t now_s);
int store_typing_count(store *, uint64_t chan, int64_t now_s);
void store_typing_str(store *, uint64_t chan, int64_t now_s, char *out, size_t cap);
#endif
