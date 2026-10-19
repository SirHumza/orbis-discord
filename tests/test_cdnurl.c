#include "tests.h"
#include "../src/model/cdnurl.h"

TEST(test_cdnurl_avatar_hash_and_default){
    char buf[256];
    cdn_avatar_url(buf, sizeof buf, 42, "h1");
    ASSERT_EQ_STR(buf, "https://cdn.discordapp.com/avatars/42/h1.png?size=64");
    cdn_avatar_url(buf, sizeof buf, 42, "");
    ASSERT_EQ_STR(buf, "https://cdn.discordapp.com/embed/avatars/0.png");
    cdn_avatar_url(buf, sizeof buf, 1ULL << 22, "");
    ASSERT_EQ_STR(buf, "https://cdn.discordapp.com/embed/avatars/1.png");
}

TEST(test_cdnurl_icons){
    char buf[256];
    cdn_guild_icon_url(buf, sizeof buf, 7, "xyz");
    ASSERT_EQ_STR(buf, "https://cdn.discordapp.com/icons/7/xyz.png?size=64");
    cdn_guild_icon_url(buf, sizeof buf, 7, "");
    ASSERT_EQ_STR(buf, "");
    cdn_channel_icon_url(buf, sizeof buf, 9, "q");
    ASSERT_EQ_STR(buf, "https://cdn.discordapp.com/channel-icons/9/q.png?size=64");
    cdn_channel_icon_url(buf, sizeof buf, 9, NULL);
    ASSERT_EQ_STR(buf, "");
}

void run_cdnurl_tests(void){
    g_tests_run++; test_cdnurl_avatar_hash_and_default();
    g_tests_run++; test_cdnurl_icons();
}
