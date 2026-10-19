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

TEST(test_msgjson_requires_id_and_channel){
    const char *json = "{\"id\":\"5\",\"content\":\"x\"}";
    jl_val_t *v = jl_parse(json, strlen(json));
    message m;
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_INT(msg_from_json(&m, v), 0);
    jl_free(v);
}

void run_msgjson_tests(void){
    g_tests_run++; test_msgjson_attachments_and_author();
    g_tests_run++; test_msgjson_edit_flag_and_svg();
    g_tests_run++; test_msgjson_requires_id_and_channel();
}
