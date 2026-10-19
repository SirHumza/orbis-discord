#ifndef ORBIS_CDNURL_H
#define ORBIS_CDNURL_H
#include <stddef.h>
#include <stdint.h>

void cdn_avatar_url(char *dst, size_t cap, uint64_t user, const char *hash);
void cdn_guild_icon_url(char *dst, size_t cap, uint64_t guild, const char *hash);
void cdn_channel_icon_url(char *dst, size_t cap, uint64_t chan, const char *hash);

#endif
