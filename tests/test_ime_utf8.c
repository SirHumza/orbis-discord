#include "tests.h"
#include "../src/platform/ime.h"
#include "../src/ui/composer.h"

TEST(test_utf16_ascii){
    uint16_t in[] = { 'A', 'B', 0 };
    char out[16];
    size_t n = ime_utf16_to_utf8(in, 2, out, sizeof out);
    ASSERT_EQ_INT(n, 2);
    ASSERT_EQ_STR(out, "AB");
}

TEST(test_utf16_two_byte){
    uint16_t in[] = { 0x00E9, 0 }; /* é */
    char out[16];
    size_t n = ime_utf16_to_utf8(in, 1, out, sizeof out);
    ASSERT_EQ_INT(n, 2);
    ASSERT_EQ_MEM(out, "\xC3\xA9", 3);
}

TEST(test_utf16_three_byte){
    uint16_t in[] = { 0x4E2D, 0 }; /* 中 */
    char out[16];
    size_t n = ime_utf16_to_utf8(in, 1, out, sizeof out);
    ASSERT_EQ_INT(n, 3);
    ASSERT_EQ_MEM(out, "\xE4\xB8\xAD", 4);
}

TEST(test_utf16_surrogate_pair_emoji){
    uint16_t in[] = { 0xD83D, 0xDE00, 0 }; /* 😀 U+1F600 */
    char out[16];
    size_t n = ime_utf16_to_utf8(in, 2, out, sizeof out);
    ASSERT_EQ_INT(n, 4);
    ASSERT_EQ_MEM(out, "\xF0\x9F\x98\x80", 5);
}

TEST(test_utf16_lone_surrogates){
    uint16_t hi[] = { 0xD83D, 'A', 0 };
    uint16_t lo[] = { 0xDE00, 0 };
    char out[16];
    size_t n = ime_utf16_to_utf8(hi, 2, out, sizeof out);
    ASSERT_EQ_INT(n, 4);
    ASSERT_EQ_MEM(out, "\xEF\xBF\xBD" "A", 5);
    n = ime_utf16_to_utf8(lo, 1, out, sizeof out);
    ASSERT_EQ_INT(n, 3);
    ASSERT_EQ_MEM(out, "\xEF\xBF\xBD", 4);
}

TEST(test_utf16_truncation){
    uint16_t in[] = { 0x00E9, 'B', 0 };
    char out[2];
    size_t n = ime_utf16_to_utf8(in, 2, out, sizeof out);
    ASSERT_EQ_INT(n, 0);
    ASSERT_EQ_STR(out, "");
    n = ime_utf16_to_utf8(in, 1, out, 3);
    ASSERT_EQ_INT(n, 2);
    ASSERT_EQ_MEM(out, "\xC3\xA9", 3);
}

TEST(test_composer_payload_plain){
    char out[128];
    int n = composer_payload("hi", 123, out, sizeof out);
    ASSERT_EQ_STR(out, "{\"content\":\"hi\",\"nonce\":\"123\"}");
    ASSERT_EQ_INT(n, (int)strlen(out));
}

TEST(test_composer_payload_escapes){
    char out[128];
    composer_payload("a\"b\\c\nd\te\x01" "f", 7, out, sizeof out);
    ASSERT_EQ_STR(out, "{\"content\":\"a\\\"b\\\\c\\nd\\te\\u0001f\",\"nonce\":\"7\"}");
}

TEST(test_composer_payload_utf8_bytes){
    char out[256];
    const char *s = "ok \xF0\x9F\x98\x80";
    int n = composer_payload(s, 5, out, sizeof out);
    ASSERT_EQ_INT(n, (int)strlen(out));
    ASSERT_EQ_INT(n, (int)strlen(s) + (int)strlen("{\"content\":\"\",\"nonce\":\"5\"}"));
    ASSERT_TRUE(strstr(out, s) != NULL);
}

TEST(test_composer_payload_overflow){
    char out[8];
    ASSERT_EQ_INT(composer_payload("hello", 1, out, sizeof out), -1);
    ASSERT_EQ_INT(composer_payload("", 1, out, 4), -1);
}

TEST(test_composer_payload_reply){
    char out[256];
    int n = composer_payload_reply("hi", 123, 44, 9, out, sizeof out);
    ASSERT_EQ_STR(out, "{\"content\":\"hi\",\"nonce\":\"123\","
                       "\"message_reference\":{\"message_id\":\"44\","
                       "\"channel_id\":\"9\",\"fail_if_not_exists\":false}}");
    ASSERT_EQ_INT(n, (int)strlen(out));
}

TEST(test_composer_payload_edit){
    char out[128];
    int n = composer_payload_edit("new", out, sizeof out);
    ASSERT_EQ_STR(out, "{\"content\":\"new\"}");
    ASSERT_EQ_INT(n, (int)strlen(out));
}

TEST(test_composer_group_dm){
    char out[128];
    uint64_t ids[2] = { 111, 222 };
    int n = composer_group_dm(ids, 2, out, sizeof out);
    ASSERT_EQ_STR(out, "{\"recipients\":[\"111\",\"222\"]}");
    ASSERT_EQ_INT(n, (int)strlen(out));
    n = composer_group_dm(ids, 1, out, sizeof out);
    ASSERT_EQ_STR(out, "{\"recipients\":[\"111\"]}");
    ASSERT_EQ_INT(n, (int)strlen(out));
    ASSERT_EQ_INT(composer_group_dm(NULL, 2, out, sizeof out), -1);
    ASSERT_EQ_INT(composer_group_dm(ids, 0, out, sizeof out), -1);
    ASSERT_EQ_INT(composer_group_dm(ids, 1, out, 5), -1);
    ASSERT_EQ_INT(composer_group_dm(ids, 2, out, 27), -1);
}

TEST(test_composer_name_payload){
    char out[128];
    int n = composer_name_payload("My \"crew\"", out, sizeof out);
    ASSERT_EQ_STR(out, "{\"name\":\"My \\\"crew\\\"\"}");
    ASSERT_EQ_INT(n, (int)strlen(out));
    n = composer_name_payload("", out, sizeof out);
    ASSERT_EQ_STR(out, "{\"name\":\"\"}");
    ASSERT_EQ_INT(n, 11);
    n = composer_name_payload(NULL, out, sizeof out);
    ASSERT_EQ_STR(out, "{\"name\":\"\"}");
    ASSERT_EQ_INT(n, 11);
    ASSERT_EQ_INT(composer_name_payload("x", out, 4), -1);
    ASSERT_EQ_INT(composer_name_payload("hello", out, 15), -1);
}

void run_ime_utf8_tests(void){
    g_tests_run++; test_utf16_ascii();
    g_tests_run++; test_utf16_two_byte();
    g_tests_run++; test_utf16_three_byte();
    g_tests_run++; test_utf16_surrogate_pair_emoji();
    g_tests_run++; test_utf16_lone_surrogates();
    g_tests_run++; test_utf16_truncation();
    g_tests_run++; test_composer_payload_plain();
    g_tests_run++; test_composer_payload_escapes();
    g_tests_run++; test_composer_payload_utf8_bytes();
    g_tests_run++; test_composer_payload_overflow();
    g_tests_run++; test_composer_payload_reply();
    g_tests_run++; test_composer_payload_edit();
    g_tests_run++; test_composer_group_dm();
    g_tests_run++; test_composer_name_payload();
}
