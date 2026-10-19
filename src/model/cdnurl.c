#include "cdnurl.h"
#include <stdio.h>

void cdn_avatar_url(char *dst, size_t cap, uint64_t user, const char *hash){
    if(!dst || cap == 0) return;
    if(hash && *hash)
        snprintf(dst, cap,
                 "https://cdn.discordapp.com/avatars/%llu/%s.png?size=64",
                 (unsigned long long)user, hash);
    else
        snprintf(dst, cap,
                 "https://cdn.discordapp.com/embed/avatars/%llu.png",
                 (unsigned long long)((user >> 22) % 6));
}

void cdn_guild_icon_url(char *dst, size_t cap, uint64_t guild, const char *hash){
    if(!dst || cap == 0) return;
    if(hash && *hash)
        snprintf(dst, cap,
                 "https://cdn.discordapp.com/icons/%llu/%s.png?size=64",
                 (unsigned long long)guild, hash);
    else
        dst[0] = 0;
}

void cdn_channel_icon_url(char *dst, size_t cap, uint64_t chan, const char *hash){
    if(!dst || cap == 0) return;
    if(hash && *hash)
        snprintf(dst, cap,
                 "https://cdn.discordapp.com/channel-icons/%llu/%s.png?size=64",
                 (unsigned long long)chan, hash);
    else
        dst[0] = 0;
}
