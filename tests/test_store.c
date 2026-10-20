#include "tests.h"
#include "../src/model/snowflake.h"
#include "../src/model/store.h"
#include "../src/model/perm.h"

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

TEST(test_store_dm_channels){
    store *st = store_new();
    channel dm2 = {.id = 20, .guild = 0, .type = 1, .position = 1};
    channel dm1 = {.id = 10, .guild = 0, .type = 1, .position = 0};
    channel grp = {.id = 30, .guild = 0, .type = 3, .position = 2};
    channel txt = {.id = 40, .guild = 7, .type = 0, .position = 0};
    channel voice = {.id = 50, .guild = 0, .type = 2, .position = 3};
    const channel *out[8];
    size_t n;
    strcpy(dm1.name, "alice");
    strcpy(dm2.name, "bob");
    strcpy(grp.name, "friends");
    store_put_channel(st, &dm2);
    store_put_channel(st, &dm1);
    store_put_channel(st, &grp);
    store_put_channel(st, &txt);
    store_put_channel(st, &voice);
    n = store_dm_channels(st, out, 8);
    ASSERT_EQ_INT(n, 3);
    ASSERT_EQ_INT(out[0]->id, 10);
    ASSERT_EQ_INT(out[1]->id, 20);
    ASSERT_EQ_INT(out[2]->id, 30);
    store_free(st);
}

TEST(test_store_del_pending_by_nonce){
    store *st = store_new();
    message real = {.id = 10, .chan = 1, .author_id = 7};
    strcpy(real.author_name, "a"); strcpy(real.content, "one");
    message pend = {.id = 99, .chan = 1, .author_id = 7, .pending = 1, .nonce = 42};
    strcpy(pend.author_name, "a"); strcpy(pend.content, "two");
    message other = {.id = 100, .chan = 1, .author_id = 7, .pending = 1, .nonce = 43};
    strcpy(other.author_name, "a"); strcpy(other.content, "three");
    store_put_message(st, &real);
    store_put_message(st, &pend);
    store_put_message(st, &other);
    store_del_pending(st, 1, 42);
    ASSERT_EQ_INT(store_messages(st, 1)->id, 10);
    ASSERT_EQ_INT(store_messages(st, 1)->next->id, 100);
    store_del_pending(st, 1, 999);
    ASSERT_EQ_INT(store_messages(st, 1)->next->id, 100);
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

TEST(test_store_revision_tracks_mutations){
    store *st = store_new();
    message a = {.id = 10, .chan = 1, .author_id = 7};
    guild g = {.id = 5, .my_perms = PERM_VIEW_CHANNEL};
    uint64_t r0 = store_revision(st);
    strcpy(a.content, "one");
    store_put_message(st, &a);
    ASSERT_TRUE(store_revision(st) != r0);
    r0 = store_revision(st);
    strcpy(a.content, "two");
    store_put_message(st, &a);
    ASSERT_TRUE(store_revision(st) != r0);
    r0 = store_revision(st);
    store_del_message(st, 1, 10);
    ASSERT_TRUE(store_revision(st) != r0);
    r0 = store_revision(st);
    store_put_guild(st, &g);
    ASSERT_TRUE(store_revision(st) != r0);
    r0 = store_revision(st);
    store_set_guild_perms(st, 5, PERM_VIEW_CHANNEL | PERM_SEND_MESSAGES,
                          NULL, 0);
    ASSERT_TRUE(store_revision(st) != r0);
    store_free(st);
}

TEST(test_store_reactions){
    store *st = store_new();
    message a = {.id = 10, .chan = 1, .author_id = 7};
    uint64_t r0;
    strcpy(a.content, "react to me");
    store_put_message(st, &a);
    r0 = store_revision(st);
    store_react_add(st, 1, 10, "\xF0\x9F\x91\x8D", 0);
    store_react_add(st, 1, 10, "\xF0\x9F\x91\x8D", 1);
    ASSERT_TRUE(store_revision(st) != r0);
    ASSERT_EQ_INT(store_messages(st, 1)->nreact, 1);
    ASSERT_EQ_INT(store_messages(st, 1)->react[0].count, 2);
    ASSERT_EQ_INT(store_messages(st, 1)->react[0].me, 1);
    store_react_add(st, 1, 10, "fire", 0);
    ASSERT_EQ_INT(store_messages(st, 1)->nreact, 2);
    store_react_remove(st, 1, 10, "\xF0\x9F\x91\x8D", 0);
    ASSERT_EQ_INT(store_messages(st, 1)->react[0].count, 1);
    store_react_remove(st, 1, 10, "\xF0\x9F\x91\x8D", 1);
    ASSERT_EQ_INT(store_messages(st, 1)->nreact, 1);
    ASSERT_EQ_STR(store_messages(st, 1)->react[0].emoji, "fire");
    store_react_clear_emoji(st, 1, 10, "fire");
    ASSERT_EQ_INT(store_messages(st, 1)->nreact, 0);
    store_react_add(st, 1, 10, "cat", 0);
    store_react_clear(st, 1, 10);
    ASSERT_EQ_INT(store_messages(st, 1)->nreact, 0);
    store_react_add(st, 1, 999, "ghost", 0);
    store_react_remove(st, 1, 999, "ghost", 0);
    store_react_clear(st, 1, 999);
    store_react_add(st, 777, 10, "nope", 0);
    ASSERT_EQ_INT(store_messages(st, 1)->nreact, 0);
    store_free(st);
}

TEST(test_store_typing){
    store *st = store_new();
    char out[160];
    store_typing(st, 1, 7, "bob", 100);
    ASSERT_EQ_INT(store_typing_count(st, 1, 100), 1);
    store_typing_str(st, 1, 100, out, sizeof out);
    ASSERT_EQ_STR(out, "bob is typing...");
    ASSERT_EQ_INT(store_typing_count(st, 2, 100), 0);
    store_typing_str(st, 2, 100, out, sizeof out);
    ASSERT_EQ_STR(out, "");
    store_typing(st, 1, 8, "alice", 100);
    store_typing_str(st, 1, 100, out, sizeof out);
    ASSERT_EQ_STR(out, "bob and alice are typing...");
    store_typing(st, 1, 9, "carl", 100);
    store_typing_str(st, 1, 100, out, sizeof out);
    ASSERT_EQ_STR(out, "several people are typing...");
    ASSERT_EQ_INT(store_typing_count(st, 1, 107), 3);
    ASSERT_EQ_INT(store_typing_count(st, 1, 108), 0);
    store_typing_str(st, 1, 108, out, sizeof out);
    ASSERT_EQ_STR(out, "");
    store_typing(st, 1, 7, "bob", 200);
    store_typing(st, 1, 7, "bobby", 200);
    ASSERT_EQ_INT(store_typing_count(st, 1, 200), 1);
    store_typing_str(st, 1, 200, out, sizeof out);
    ASSERT_EQ_STR(out, "bobby is typing...");
    store_free(st);
}

TEST(test_store_unread){
    store *st = store_new();
    channel c = {.id = 100, .guild = 9, .type = 0};
    message a = {.id = 10, .chan = 100, .author_id = 7};
    strcpy(c.name, "general");
    strcpy(a.content, "x");
    store_put_channel(st, &c);
    ASSERT_EQ_INT(store_unread(st, 100), 0);
    store_put_message(st, &a);
    ASSERT_EQ_INT(store_unread(st, 100), 1);
    store_mark_read(st, 100);
    ASSERT_EQ_INT(store_unread(st, 100), 0);
    a.id = 22;
    store_put_message(st, &a);
    ASSERT_EQ_INT(store_unread(st, 100), 1);
    store_put_channel(st, &c);
    ASSERT_EQ_INT(store_unread(st, 100), 1);
    store_mark_read(st, 100);
    store_put_channel(st, &c);
    ASSERT_EQ_INT(store_unread(st, 100), 0);
    ASSERT_EQ_INT(store_unread(st, 999), 0);
    store_mark_read(st, 999);
    store_free(st);
}

TEST(test_store_message_lookup){
    store *st = store_new();
    message a = {.id = 10, .chan = 1, .author_id = 7};
    message b = {.id = 30, .chan = 1, .author_id = 7};
    message c = {.id = 50, .chan = 1, .author_id = 7};
    ASSERT_TRUE(st != NULL);
    store_put_message(st, &a);
    store_put_message(st, &b);
    store_put_message(st, &c);
    ASSERT_EQ_INT(store_message(st, 1, 30)->id, 30);
    ASSERT_TRUE(store_message(st, 1, 99) == NULL);
    ASSERT_EQ_INT(store_newest_message(st, 1)->id, 50);
    ASSERT_TRUE(store_newest_message(st, 2) == NULL);
    ASSERT_EQ_INT(store_message_step(st, 1, 30, -1)->id, 10);
    ASSERT_EQ_INT(store_message_step(st, 1, 30, 1)->id, 50);
    ASSERT_TRUE(store_message_step(st, 1, 10, -1) == NULL);
    ASSERT_TRUE(store_message_step(st, 1, 50, 1) == NULL);
    store_free(st);
}

TEST(test_store_guild_unread_counts){
    store *st = store_new();
    channel c1 = {.id = 100, .guild = 5, .type = 0};
    channel c2 = {.id = 101, .guild = 5, .type = 0};
    channel dm = {.id = 102, .guild = 0, .type = 1};
    channel other = {.id = 103, .guild = 6, .type = 0};
    guild g5 = {.id = 5, .my_perms = PERM_VIEW_CHANNEL};
    guild g6 = {.id = 6, .my_perms = PERM_VIEW_CHANNEL};
    message a = {.id = 10, .chan = 100};
    message b = {.id = 20, .chan = 100};
    message c = {.id = 30, .chan = 100, .mentions_me = 1};
    message d = {.id = 40, .chan = 101};
    message e = {.id = 50, .chan = 102};
    message f = {.id = 60, .chan = 103};
    int unread = -1, mentions = -1;
    ASSERT_TRUE(st != NULL);
    store_put_guild(st, &g5);
    store_put_guild(st, &g6);
    store_put_channel(st, &c1);
    store_put_channel(st, &c2);
    store_put_channel(st, &dm);
    store_put_channel(st, &other);
    store_put_message(st, &a);
    store_mark_read(st, 100);
    store_put_message(st, &b);
    store_put_message(st, &c);
    store_put_message(st, &d);
    store_put_message(st, &e);
    store_put_message(st, &f);
    store_guild_unread(st, 5, 77, &unread, &mentions);
    ASSERT_EQ_INT(unread, 3);
    ASSERT_EQ_INT(mentions, 1);
    store_guild_unread(st, 6, 77, &unread, &mentions);
    ASSERT_EQ_INT(unread, 1);
    ASSERT_EQ_INT(mentions, 0);
    store_guild_unread(st, 99, 77, &unread, &mentions);
    ASSERT_EQ_INT(unread, 0);
    ASSERT_EQ_INT(mentions, 0);
    store_mark_read(st, 100);
    store_guild_unread(st, 5, 77, &unread, &mentions);
    ASSERT_EQ_INT(unread, 1);
    ASSERT_EQ_INT(mentions, 0);
    store_free(st);
}

TEST(test_store_guild_unread_ignores_hidden_channels){
    store *st = store_new();
    guild gu = {.id = 5, .my_perms = PERM_VIEW_CHANNEL};
    channel open = {.id = 100, .guild = 5, .type = 0};
    channel secret = {.id = 101, .guild = 5, .type = 0, .now = 2};
    message a = {.id = 10, .chan = 100, .mentions_me = 1};
    message b = {.id = 20, .chan = 101, .mentions_me = 1};
    int unread = -1, mentions = -1;
    secret.ow[0].id = 5;
    secret.ow[0].type = 0;
    secret.ow[0].deny = PERM_VIEW_CHANNEL;
    secret.ow[1].id = 77;
    secret.ow[1].type = 1;
    secret.ow[1].allow = PERM_VIEW_CHANNEL;
    ASSERT_TRUE(st != NULL);
    store_put_guild(st, &gu);
    store_put_channel(st, &open);
    store_put_channel(st, &secret);
    store_put_message(st, &a);
    store_put_message(st, &b);
    store_guild_unread(st, 5, 77, &unread, &mentions);
    ASSERT_EQ_INT(unread, 2);
    ASSERT_EQ_INT(mentions, 2);
    store_guild_unread(st, 5, 88, &unread, &mentions);
    ASSERT_EQ_INT(unread, 1);
    ASSERT_EQ_INT(mentions, 1);
    store_free(st);
}

TEST(test_store_hoisted_roles){
    store *st = store_new();
    guild gu;
    const role *out[8];
    ASSERT_TRUE(st != NULL);
    memset(&gu, 0, sizeof gu);
    gu.id = 9;
    gu.roles[0].id = 1; gu.roles[0].position = 5; gu.roles[0].hoist = 1;
    snprintf(gu.roles[0].name, sizeof gu.roles[0].name, "Admin");
    gu.roles[1].id = 2; gu.roles[1].position = 3; gu.roles[1].hoist = 1;
    snprintf(gu.roles[1].name, sizeof gu.roles[1].name, "Mod");
    gu.roles[2].id = 3; gu.roles[2].position = 9; gu.roles[2].hoist = 0;
    snprintf(gu.roles[2].name, sizeof gu.roles[2].name, "Secret");
    gu.roles[3].id = 4; gu.roles[3].position = 7; gu.roles[3].hoist = 1;
    gu.nroles = 4;
    store_put_guild(st, &gu);
    ASSERT_EQ_INT(store_guild_hoisted_roles(st, 9, out, 8), 2);
    ASSERT_EQ_INT(out[0]->id, 1);
    ASSERT_EQ_INT(out[1]->id, 2);
    ASSERT_EQ_INT(store_guild_hoisted_roles(st, 9, out, 1), 1);
    ASSERT_EQ_INT(store_guild_hoisted_roles(st, 99, out, 8), 0);
    store_free(st);
}

void run_store_tests(void){
    g_tests_run++; test_snowflake_parse_format();
    g_tests_run++; test_snowflake_timestamp();
    g_tests_run++; test_store_messages_sorted_and_deduped();
    g_tests_run++; test_store_dm_channels();
    g_tests_run++; test_store_del_pending_by_nonce();
    g_tests_run++; test_store_delete_missing_is_noop();
    g_tests_run++; test_store_delete_existing();
    g_tests_run++; test_store_grouping();
    g_tests_run++; test_store_utf8();
    g_tests_run++; test_store_channels_for_guild();
    g_tests_run++; test_store_revision_tracks_mutations();
    g_tests_run++; test_store_reactions();
    g_tests_run++; test_store_typing();
    g_tests_run++; test_store_unread();
    g_tests_run++; test_store_message_lookup();
    g_tests_run++; test_store_guild_unread_counts();
    g_tests_run++; test_store_guild_unread_ignores_hidden_channels();
    g_tests_run++; test_store_hoisted_roles();
}
