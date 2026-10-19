#include "tests.h"
#include "../src/model/snowflake.h"
#include "../src/model/store.h"

TEST(test_snowflake_parse_format){
    const char *s = "175928847299117063";
    uint64_t id = sn_parse(s);
    char out[21];
    sn_str(id, out);
    ASSERT_EQ_STR(out, s);
}

TEST(test_snowflake_timestamp){
    uint64_t id = (1690000000000ULL - 1420070400000ULL) << 22;
    ASSERT_EQ_INT(sn_timestamp_ms(id), 1690000000000ULL);
}

TEST(test_store_messages_sorted_and_deduped){
    store *st = store_new();
    ASSERT_TRUE(st != NULL);
    message a = {.id = 50, .chan = 1, .author_id = 7};
    strcpy(a.author_name, "a"); strcpy(a.content, "five");
    message b = {.id = 10, .chan = 1, .author_id = 7};
    strcpy(b.author_name, "a"); strcpy(b.content, "one");
    message c = {.id = 30, .chan = 1, .author_id = 8};
    strcpy(c.author_name, "b"); strcpy(c.content, "three");
    store_put_message(st, &a);
    store_put_message(st, &b);
    store_put_message(st, &c);
    const message *m = store_messages(st, 1);
    ASSERT_TRUE(m != NULL);
    ASSERT_EQ_INT(m->id, 10);
    ASSERT_EQ_INT(m->next->id, 30);
    ASSERT_EQ_INT(m->next->next->id, 50);
    b.content[0] = 'X'; b.content[1] = 0;
    store_put_message(st, &b);
    ASSERT_EQ_STR(store_messages(st, 1)->content, "X");
    store_free(st);
}

TEST(test_store_delete_missing_is_noop){
    store *st = store_new();
    store_del_message(st, 1, 99);
    ASSERT_TRUE(store_messages(st, 1) == NULL);
    store_free(st);
}

TEST(test_store_delete_existing){
    store *st = store_new();
    message a = {.id = 10, .chan = 1, .author_id = 7};
    strcpy(a.content, "x");
    store_put_message(st, &a);
    store_del_message(st, 1, 10);
    ASSERT_TRUE(store_messages(st, 1) == NULL);
    store_free(st);
}

TEST(test_store_grouping){
    store *st = store_new();
    uint64_t base = 1690000000000ULL;
    message a = {.id = ((base - 1420070400000ULL) << 22) | 1, .chan = 1, .author_id = 7};
    strcpy(a.author_name, "a"); strcpy(a.content, "hi");
    message b = a; b.id += 1; strcpy(b.content, "again");
    message c = a; c.id += 2; c.author_id = 8; strcpy(c.author_name, "b"); strcpy(c.content, "yo");
    message d = a; d.id = ((base + 600000ULL - 1420070400000ULL) << 22) | 3; strcpy(d.content, "later");
    store_put_message(st, &a);
    store_put_message(st, &b);
    store_put_message(st, &c);
    store_put_message(st, &d);
    const message *m = store_messages(st, 1);
    ASSERT_EQ_INT(m->is_grouped, 0);
    ASSERT_EQ_INT(m->next->is_grouped, 1);
    ASSERT_EQ_INT(m->next->next->is_grouped, 0);
    ASSERT_EQ_INT(m->next->next->next->is_grouped, 0);
    store_free(st);
}

TEST(test_store_utf8){
    store *st = store_new();
    message a = {.id = 1, .chan = 2, .author_id = 7};
    const char *emoji = "hello \xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD world";
    strcpy(a.content, emoji);
    store_put_message(st, &a);
    ASSERT_EQ_STR(store_messages(st, 2)->content, emoji);
    ASSERT_EQ_INT(strlen(store_messages(st, 2)->content), strlen(emoji));
    store_free(st);
}

TEST(test_store_channels_for_guild){
    store *st = store_new();
    channel c1 = {.id = 100, .guild = 9, .type = 0, .position = 2};
    channel c2 = {.id = 101, .guild = 9, .type = 0, .position = 1};
    channel c3 = {.id = 102, .guild = 8, .type = 0, .position = 0};
    strcpy(c1.name, "two"); strcpy(c2.name, "one"); strcpy(c3.name, "other");
    store_put_channel(st, &c1);
    store_put_channel(st, &c2);
    store_put_channel(st, &c3);
    uint64_t out[8];
    size_t n = store_channels_for_guild(st, 9, out, 8);
    ASSERT_EQ_INT(n, 2);
    ASSERT_EQ_INT(out[0], 101);
    ASSERT_EQ_INT(out[1], 100);
    n = store_channels_for_guild(st, 9, out, 1);
    ASSERT_EQ_INT(n, 1);
    ASSERT_TRUE(store_channel(st, 101) != NULL);
    ASSERT_EQ_STR(store_channel(st, 101)->name, "one");
    ASSERT_TRUE(store_channel(st, 999) == NULL);
    store_free(st);
}

void run_store_tests(void){
    g_tests_run++; test_snowflake_parse_format();
    g_tests_run++; test_snowflake_timestamp();
    g_tests_run++; test_store_messages_sorted_and_deduped();
    g_tests_run++; test_store_delete_missing_is_noop();
    g_tests_run++; test_store_delete_existing();
    g_tests_run++; test_store_grouping();
    g_tests_run++; test_store_utf8();
    g_tests_run++; test_store_channels_for_guild();
}
