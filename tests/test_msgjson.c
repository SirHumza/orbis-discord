#include "tests.h"
#include "../src/core/jsonlite.h"
#include "../src/model/msgjson.h"

TEST(test_msgjson_attachments_and_author){
    const char *json =
        "{\"id\":\"123456789012345678\",\"channel_id\":\"700\","
        "\"content\":\"look\",\"author\":{\"id\":\"42\",\"username\":\"bob\",\"avatar\":\"h1\"},"
        "\"attachments\":["
        "{\"filename\":\"cat.png\",\"content_type\":\"image/png\",\"size\":1234,"
        "\"url\":\"https://cdn.discordapp.com/attachments/1/2/cat.png\","
        "\"width\":400,\"height\":300},"
        "{\"filename\":\"notes.txt\",\"content_type\":\"text/plain\",\"size\":10,"
        "\"url\":\"https://cdn.discordapp.com/attachments/1/2/notes.txt\"}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 1);
    ASSERT_EQ_INT(m.chan, 700);
    ASSERT_EQ_STR(m.content, "look");
    ASSERT_EQ_INT(m.author_id, 42);
    ASSERT_EQ_STR(m.author_name, "bob");
    ASSERT_EQ_STR(m.author_avatar, "https://cdn.discordapp.com/avatars/42/h1.png?size=64");
    ASSERT_EQ_INT(m.natt, 2);
    ASSERT_EQ_INT(m.att[0].image, 1);
    ASSERT_EQ_INT(m.att[0].w, 400);
    ASSERT_EQ_INT(m.att[0].h, 300);
    ASSERT_EQ_INT(m.att[0].size, 1234);
    ASSERT_EQ_STR(m.att[0].name, "cat.png");
    ASSERT_TRUE(strstr(m.att[0].url, "cat.png") != NULL);
    ASSERT_EQ_INT(m.att[1].image, 0);
    ASSERT_EQ_STR(m.att[1].name, "notes.txt");
    jl_free(v);
}

TEST(test_msgjson_edit_flag_and_svg){
    const char *json =
        "{\"id\":\"5\",\"channel_id\":\"7\",\"content\":\"x\","
        "\"edited_timestamp\":\"2026-10-19T00:00:00.000000+00:00\","
        "\"attachments\":[{\"filename\":\"v.svg\","
        "\"content_type\":\"image/svg+xml\",\"url\":\"https://cdn.discordapp.com/v.svg\","
        "\"width\":10,\"height\":10}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 1);
    ASSERT_EQ_INT(m.has_edit, 1);
    ASSERT_EQ_INT(m.natt, 1);
    ASSERT_EQ_INT(m.att[0].image, 0);
    jl_free(v);
}

TEST(test_msgjson_embed_with_image){
    const char *json =
        "{\"id\":\"1\",\"channel_id\":\"2\",\"content\":\"\",\"embeds\":[{"
        "\"title\":\"Hello\",\"description\":\"World desc\",\"url\":\"https://x\","
        "\"color\":5814783,\"image\":{\"url\":\"https://cdn.discordapp.com/a.png\","
        "\"width\":800,\"height\":400}}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 1);
    ASSERT_EQ_INT(m.nemb, 1);
    ASSERT_EQ_STR(m.emb[0].title, "Hello");
    ASSERT_EQ_STR(m.emb[0].desc, "World desc");
    ASSERT_EQ_INT(m.emb[0].has_color, 1);
    ASSERT_EQ_INT(m.emb[0].color, 0xFF58B9FF);
    ASSERT_EQ_INT(m.emb[0].image_w, 800);
    ASSERT_EQ_INT(m.emb[0].image_h, 400);
    jl_free(v);
}

TEST(test_msgjson_embed_thumbnail_fallback){
    const char *json =
        "{\"id\":\"1\",\"channel_id\":\"2\",\"content\":\"\",\"embeds\":[{"
        "\"title\":\"T\",\"thumbnail\":{\"url\":\"https://cdn.discordapp.com/t.png\","
        "\"width\":80,\"height\":80}}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 1);
    ASSERT_EQ_INT(m.nemb, 1);
    ASSERT_TRUE(strstr(m.emb[0].image_url, "t.png") != NULL);
    ASSERT_EQ_INT(m.emb[0].has_color, 0);
    jl_free(v);
}

TEST(test_msgjson_reactions){
    const char *json =
        "{\"id\":\"1\",\"channel_id\":\"2\",\"content\":\"\",\"reactions\":["
        "{\"count\":3,\"me\":true,\"emoji\":{\"id\":\"\",\"name\":\"fire\"}},"
        "{\"count\":1,\"me\":false,\"emoji\":{\"id\":\"999\",\"name\":\"pepe\"}},"
        "{\"count\":0,\"me\":false,\"emoji\":{\"id\":\"\",\"name\":\"zero\"}},"
        "{\"count\":5,\"me\":true,\"emoji\":{\"id\":\"\",\"name\":\"\"}}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 1);
    ASSERT_EQ_INT(m.nreact, 3);
    ASSERT_EQ_STR(m.react[0].emoji, "fire");
    ASSERT_EQ_INT(m.react[0].count, 3);
    ASSERT_EQ_INT(m.react[0].me, 1);
    ASSERT_EQ_STR(m.react[1].emoji, "pepe");
    ASSERT_EQ_INT(m.react[1].me, 0);
    ASSERT_EQ_STR(m.react[2].emoji, "zero");
    ASSERT_EQ_INT(m.react[2].count, 1);
    jl_free(v);
}

TEST(test_msgjson_reply_reference){
    const char *json =
        "{\"id\":\"1\",\"channel_id\":\"2\",\"content\":\"yes\","
        "\"message_reference\":{\"message_id\":\"99\",\"channel_id\":\"2\"},"
        "\"referenced_message\":{\"id\":\"99\",\"content\":\"line one\\nline two\","
        "\"author\":{\"id\":\"5\",\"username\":\"alice\"}}}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 1);
    ASSERT_EQ_INT(m.reply_to, 99);
    ASSERT_EQ_STR(m.reply_name, "alice");
    ASSERT_EQ_STR(m.reply_text, "line one line two");
    jl_free(v);
}

TEST(test_msgjson_reply_deleted_reference){
    const char *json =
        "{\"id\":\"1\",\"channel_id\":\"2\",\"content\":\"yes\","
        "\"message_reference\":{\"message_id\":\"99\",\"channel_id\":\"2\"}}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 1);
    ASSERT_EQ_INT(m.reply_to, 99);
    ASSERT_EQ_STR(m.reply_name, "");
    ASSERT_EQ_STR(m.reply_text, "");
    jl_free(v);
}

TEST(test_msgjson_resolve_mentions){
    store *st = store_new();
    guild g = {.id = 9};
    channel c = {.id = 100, .guild = 9, .type = 0};
    member mb = {.user = 7, .guild = 9};
    message m;
    strcpy(g.name, "G");
    strcpy(c.name, "general");
    strcpy(mb.name, "bob");
    store_put_guild(st, &g);
    store_put_channel(st, &c);
    store_put_member(st, &mb);
    memset(&m, 0, sizeof m);
    m.chan = 100;
    strcpy(m.content, "<@7> see <#100> <:tada:55> <a:party:66> <@&44> <@!5> plain");
    msg_resolve_mentions(st, &m, 5);
    ASSERT_EQ_STR(m.content, "@bob see #general :tada: :party: @role @5 plain");
    ASSERT_EQ_INT(m.mentions_me, 1);
    strcpy(m.content, "<@7> hi <@123>");
    msg_resolve_mentions(st, &m, 5);
    ASSERT_EQ_STR(m.content, "@bob hi @123");
    ASSERT_EQ_INT(m.mentions_me, 0);
    store_free(st);
}

TEST(test_msgjson_stickers){
    const char *json =
        "{\"id\":\"1\",\"channel_id\":\"2\",\"content\":\"\",\"sticker_items\":["
        "{\"id\":\"11\",\"name\":\"wave\",\"format_type\":1},"
        "{\"id\":\"22\",\"name\":\"party\",\"format_type\":4},"
        "{\"id\":\"33\",\"name\":\"lottie\",\"format_type\":3}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 1);
    ASSERT_EQ_INT(m.natt, 3);
    ASSERT_EQ_STR(m.att[0].url, "https://cdn.discordapp.com/stickers/11.png");
    ASSERT_EQ_INT(m.att[0].w, 160);
    ASSERT_EQ_INT(m.att[0].h, 160);
    ASSERT_EQ_INT(m.att[0].image, 1);
    ASSERT_TRUE(strstr(m.att[1].url, "/stickers/22.gif") != NULL);
    ASSERT_EQ_STR(m.att[2].name, "lottie");
    ASSERT_EQ_INT(m.att[2].image, 0);
    ASSERT_EQ_INT(m.att[2].url[0], 0);
    jl_free(v);
}

TEST(test_msgjson_requires_id_and_channel){
    const char *json = "{\"id\":\"5\",\"content\":\"x\"}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 0);
    jl_free(v);
}

TEST(test_msgjson_emoji_item){
    const char *json = "{\"id\":\"41771983429993937\",\"name\":\"LUL\","
                       "\"animated\":true,\"available\":true}";
    jl_val_t *v = jl_parse(json, strlen(json));
    emoji_item e;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(emoji_from_json(&e, v), 1);
    ASSERT_EQ_INT(e.id, 41771983429993937LL);
    ASSERT_EQ_STR(e.name, "LUL");
    ASSERT_EQ_INT(e.animated, 1);
    jl_free(v);
}

TEST(test_msgjson_emoji_item_invalid){
    emoji_item e;
    const char *json = "\"nope\"";
    jl_val_t *v = jl_parse(json, strlen(json));
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(emoji_from_json(&e, NULL), 0);
    ASSERT_EQ_INT(emoji_from_json(&e, v), 0);
    jl_free(v);
}

TEST(test_msgjson_recipients){
    const char *json =
        "{\"id\":\"900\",\"type\":3,\"recipients\":["
        "{\"id\":\"111\",\"username\":\"bob\",\"global_name\":\"Bobby\","
        "\"avatar\":\"h1\"},"
        "{\"id\":\"222\",\"username\":\"eve\",\"avatar\":null},"
        "{\"username\":\"noid\"}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    recip_item r[8];
    int n;
    ASSERT_TRUE(v != NULL);
    n = recipients_from_json(jl_obj_get(v, "recipients"), r, 8);
    ASSERT_EQ_INT(n, 2);
    ASSERT_EQ_INT(r[0].id, 111);
    ASSERT_EQ_STR(r[0].name, "Bobby");
    ASSERT_TRUE(strstr(r[0].avatar, "avatars/111/h1.png") != NULL);
    ASSERT_EQ_INT(r[1].id, 222);
    ASSERT_EQ_STR(r[1].name, "eve");
    ASSERT_TRUE(strstr(r[1].avatar, "embed/avatars/") != NULL);
    jl_free(v);
}

TEST(test_msgjson_recipients_invalid){
    const char *json = "\"nope\"";
    recip_item r[4];
    jl_val_t *v = jl_parse(json, strlen(json));
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(recipients_from_json(NULL, r, 4), 0);
    ASSERT_EQ_INT(recipients_from_json(v, r, 4), 0);
    ASSERT_EQ_INT(recipients_from_json(v, NULL, 4), 0);
    ASSERT_EQ_INT(recipients_from_json(v, r, 0), 0);
    jl_free(v);
}

TEST(test_msgjson_overwrites){
    const char *json =
        "{\"permission_overwrites\":["
        "{\"id\":\"10\",\"type\":0,\"allow\":\"1024\",\"deny\":\"0\"},"
        "{\"id\":\"77\",\"type\":1,\"allow\":\"0\",\"deny\":\"2048\"},"
        "{\"type\":0}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    channel ch;
    memset(&ch, 0, sizeof ch);
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(overwrites_from_json(jl_obj_get(v, "permission_overwrites"),
                                       &ch), 2);
    ASSERT_EQ_INT(ch.now, 2);
    ASSERT_EQ_INT(ch.ow[0].id, 10);
    ASSERT_EQ_INT(ch.ow[0].type, 0);
    ASSERT_EQ_INT(ch.ow[0].allow, 1024);
    ASSERT_EQ_INT(ch.ow[0].deny, 0);
    ASSERT_EQ_INT(ch.ow[1].id, 77);
    ASSERT_EQ_INT(ch.ow[1].type, 1);
    ASSERT_EQ_INT(ch.ow[1].deny, 2048);
    ASSERT_EQ_INT(overwrites_from_json(NULL, &ch), 0);
    ASSERT_EQ_INT(ch.now, 0);
    ASSERT_EQ_INT(overwrites_from_json(NULL, NULL), 0);
    jl_free(v);
}

TEST(test_msgjson_invites){
    const char *json =
        "[{\"code\":\"abc123\",\"uses\":2,\"max_uses\":10,\"max_age\":86400},"
        "{\"code\":\"xyz\",\"uses\":0,\"max_uses\":0},"
        "{\"uses\":1}]";
    jl_val_t *v = jl_parse(json, strlen(json));
    invite_item iv[4];
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(invites_from_json(v, iv, 4), 2);
    ASSERT_EQ_STR(iv[0].code, "abc123");
    ASSERT_EQ_INT(iv[0].uses, 2);
    ASSERT_EQ_INT(iv[0].max_uses, 10);
    ASSERT_EQ_INT(iv[0].max_age, 86400);
    ASSERT_EQ_STR(iv[1].code, "xyz");
    ASSERT_EQ_INT(iv[1].max_uses, 0);
    ASSERT_EQ_INT(invites_from_json(NULL, iv, 4), 0);
    ASSERT_EQ_INT(invites_from_json(v, NULL, 4), 0);
    jl_free(v);
}

TEST(test_msgjson_threads){
    const char *json =
        "{\"threads\":["
        "{\"id\":\"11\",\"parent_id\":\"500\",\"name\":\"one\","
        "\"message_count\":4},"
        "{\"id\":\"22\",\"parent_id\":\"600\",\"name\":\"other\","
        "\"message_count\":9},"
        "{\"id\":\"33\",\"parent_id\":\"500\",\"name\":\"two\","
        "\"message_count\":0},"
        "{\"parent_id\":\"500\",\"name\":\"noid\"}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    thread_item t[4];
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(threads_from_json(v, 500, t, 4), 2);
    ASSERT_EQ_INT(t[0].id, 11);
    ASSERT_EQ_STR(t[0].name, "one");
    ASSERT_EQ_INT(t[0].messages, 4);
    ASSERT_EQ_INT(t[1].id, 33);
    ASSERT_EQ_INT(threads_from_json(NULL, 500, t, 4), 0);
    ASSERT_EQ_INT(threads_from_json(v, 0, t, 4), 0);
    ASSERT_EQ_INT(threads_from_json(v, 500, NULL, 4), 0);
    jl_free(v);
}

TEST(test_msgjson_embed_metadata){
    const char *json =
        "{\"id\":\"1\",\"channel_id\":\"2\",\"content\":\"\",\"embeds\":[{"
        "\"title\":\"Video\",\"description\":\"d\","
        "\"provider\":{\"name\":\"YouTube\"},\"author\":{\"name\":\"Chan\"},"
        "\"footer\":{\"text\":\"12 comments\"}}]}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 1);
    ASSERT_EQ_INT(m.nemb, 1);
    ASSERT_EQ_STR(m.emb[0].site, "YouTube");
    ASSERT_EQ_STR(m.emb[0].author, "Chan");
    ASSERT_EQ_STR(m.emb[0].footer, "12 comments");
    jl_free(v);
}

void run_msgjson_tests(void){
    g_tests_run++; test_msgjson_attachments_and_author();
    g_tests_run++; test_msgjson_edit_flag_and_svg();
    g_tests_run++; test_msgjson_embed_with_image();
    g_tests_run++; test_msgjson_embed_thumbnail_fallback();
    g_tests_run++; test_msgjson_embed_metadata();
    g_tests_run++; test_msgjson_reactions();
    g_tests_run++; test_msgjson_reply_reference();
    g_tests_run++; test_msgjson_reply_deleted_reference();
    g_tests_run++; test_msgjson_resolve_mentions();
    g_tests_run++; test_msgjson_stickers();
    g_tests_run++; test_msgjson_requires_id_and_channel();
    g_tests_run++; test_msgjson_emoji_item();
    g_tests_run++; test_msgjson_emoji_item_invalid();
    g_tests_run++; test_msgjson_recipients();
    g_tests_run++; test_msgjson_recipients_invalid();
    g_tests_run++; test_msgjson_overwrites();
    g_tests_run++; test_msgjson_invites();
    g_tests_run++; test_msgjson_threads();
}
