#include "tests.h"
#include "../src/core/gateway.h"
#include "../src/core/clock.h"
#include "../src/model/store.h"

static store *s;
static gateway *g;

static void setup(void){
    s = store_new();
    g = gw_new(s, "tok123");
}

static int feed(const char *json){
    return gw_feed(g, json, strlen(json));
}

TEST(test_events_ready){
    setup();
    int ev = feed("{\"op\":0,\"s\":1,\"t\":\"READY\",\"d\":{\"session_id\":\"sess-abc\",\"user\":{\"id\":\"5\",\"username\":\"me\"},\"guilds\":[{\"id\":\"9\",\"name\":\"Guild9\",\"channels\":[{\"id\":\"100\",\"name\":\"general\",\"type\":0,\"position\":1,\"guild_id\":\"9\"}]}],\"private_channels\":[]}}");
    ASSERT_TRUE(ev & GW_EV_READY);
    const guild *gs[4];
    ASSERT_EQ_INT(store_guilds(s, gs, 4), 1);
    ASSERT_EQ_INT(gs[0]->id, 9);
    uint64_t ch[4];
    ASSERT_EQ_INT(store_channels_for_guild(s, 9, ch, 4), 1);
    ASSERT_EQ_INT(ch[0], 100);
    ASSERT_EQ_STR(gw_self_name(g), "me");
    gw_free(g); store_free(s);
}

TEST(test_events_ready_resume_payload){
    setup();
    feed("{\"op\":0,\"s\":1,\"t\":\"READY\",\"d\":{\"session_id\":\"sess-abc\",\"user\":{\"id\":\"5\",\"username\":\"me\"},\"guilds\":[],\"private_channels\":[]}}");
    char out[512];
    ASSERT_TRUE(gw_resume(g, out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"op\":6") != NULL);
    ASSERT_TRUE(strstr(out, "sess-abc") != NULL);
    ASSERT_TRUE(strstr(out, "\"seq\":1") != NULL);
    gw_free(g); store_free(s);
}

TEST(test_events_identify_has_token_and_op){
    setup();
    char out[1024];
    ASSERT_TRUE(gw_identify(g, out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"op\":2") != NULL);
    ASSERT_TRUE(strstr(out, "tok123") != NULL);
    gw_free(g); store_free(s);
}

TEST(test_events_message_create){
    setup();
    feed("{\"op\":0,\"s\":2,\"t\":\"MESSAGE_CREATE\",\"d\":{\"id\":\"20\",\"channel_id\":\"100\",\"content\":\"hello world\",\"author\":{\"id\":\"7\",\"username\":\"bob\"}}}");
    const message *m = store_messages(s, 100);
    ASSERT_TRUE(m != NULL);
    ASSERT_EQ_INT(m->id, 20);
    ASSERT_EQ_STR(m->content, "hello world");
    ASSERT_EQ_STR(m->author_name, "bob");
    ASSERT_EQ_INT(m->author_id, 7);
    gw_free(g); store_free(s);
}

TEST(test_events_message_update_sets_edit_flag){
    setup();
    feed("{\"op\":0,\"s\":2,\"t\":\"MESSAGE_CREATE\",\"d\":{\"id\":\"20\",\"channel_id\":\"100\",\"content\":\"hello\",\"author\":{\"id\":\"7\",\"username\":\"bob\"}}}");
    feed("{\"op\":0,\"s\":3,\"t\":\"MESSAGE_UPDATE\",\"d\":{\"id\":\"20\",\"channel_id\":\"100\",\"content\":\"hello edited\",\"edited_timestamp\":\"2026-10-19T00:00:00.000000+00:00\",\"author\":{\"id\":\"7\",\"username\":\"bob\"}}}");
    const message *m = store_messages(s, 100);
    ASSERT_EQ_STR(m->content, "hello edited");
    ASSERT_EQ_INT(m->has_edit, 1);
    gw_free(g); store_free(s);
}

TEST(test_events_message_delete){
    setup();
    feed("{\"op\":0,\"s\":2,\"t\":\"MESSAGE_CREATE\",\"d\":{\"id\":\"20\",\"channel_id\":\"100\",\"content\":\"x\",\"author\":{\"id\":\"7\",\"username\":\"bob\"}}}");
    feed("{\"op\":0,\"s\":3,\"t\":\"MESSAGE_DELETE\",\"d\":{\"id\":\"20\",\"channel_id\":\"100\"}}");
    ASSERT_TRUE(store_messages(s, 100) == NULL);
    gw_free(g); store_free(s);
}

TEST(test_events_malformed){
    setup();
    ASSERT_TRUE(feed("{") < 0);
    ASSERT_TRUE(feed("{\"op\":0,\"t\":\"MESSAGE_CREATE\",\"d\":null}") <= 0);
    ASSERT_TRUE(feed("{\"op\":\"x\",\"d\":{}}") < 0);
    ASSERT_TRUE(feed("{\"d\":{}}") < 0);
    ASSERT_TRUE(store_messages(s, 100) == NULL);
    gw_free(g); store_free(s);
}

TEST(test_events_seq_tracks_dispatch){
    setup();
    feed("{\"op\":0,\"s\":5,\"t\":\"READY\",\"d\":{\"session_id\":\"x\",\"user\":{\"id\":\"5\",\"username\":\"me\"},\"guilds\":[],\"private_channels\":[]}}");
    feed("{\"op\":0,\"s\":9,\"t\":\"MESSAGE_CREATE\",\"d\":{\"id\":\"20\",\"channel_id\":\"100\",\"content\":\"a\",\"author\":{\"id\":\"7\",\"username\":\"b\"}}}");
    char out[512];
    gw_resume(g, out, sizeof out);
    ASSERT_TRUE(strstr(out, "\"seq\":9") != NULL);
    gw_free(g); store_free(s);
}

TEST(test_events_presence_updates_member){
    setup();
    feed("{\"op\":0,\"s\":2,\"t\":\"GUILD_CREATE\",\"d\":{\"id\":\"9\",\"name\":\"G\",\"channels\":[],\"members\":[{\"user\":{\"id\":\"7\",\"username\":\"bob\"},\"status\":\"online\"}]}}");
    const member *ms[4];
    ASSERT_EQ_INT(store_members(s, 9, ms, 4), 1);
    ASSERT_EQ_INT(ms[0]->online, 1);
    feed("{\"op\":0,\"s\":3,\"t\":\"PRESENCE_UPDATE\",\"d\":{\"guild_id\":\"9\",\"status\":\"offline\",\"user\":{\"id\":\"7\"}}}");
    store_members(s, 9, ms, 4);
    ASSERT_EQ_INT(ms[0]->online, 0);
    gw_free(g); store_free(s);
}

TEST(test_events_reactions){
    setup();
    feed("{\"op\":0,\"s\":1,\"t\":\"READY\",\"d\":{\"session_id\":\"x\",\"user\":{\"id\":\"5\",\"username\":\"me\"},\"guilds\":[],\"private_channels\":[]}}");
    feed("{\"op\":0,\"s\":2,\"t\":\"MESSAGE_CREATE\",\"d\":{\"id\":\"20\",\"channel_id\":\"100\",\"content\":\"x\",\"author\":{\"id\":\"7\",\"username\":\"bob\"}}}");
    ASSERT_TRUE(feed("{\"op\":0,\"s\":3,\"t\":\"MESSAGE_REACTION_ADD\",\"d\":{\"channel_id\":\"100\",\"message_id\":\"20\",\"user_id\":\"7\",\"emoji\":{\"id\":\"\",\"name\":\"fire\"}}}") & GW_EV_MESSAGE);
    ASSERT_EQ_INT(store_messages(s, 100)->nreact, 1);
    ASSERT_EQ_INT(store_messages(s, 100)->react[0].count, 1);
    ASSERT_EQ_INT(store_messages(s, 100)->react[0].me, 0);
    feed("{\"op\":0,\"s\":4,\"t\":\"MESSAGE_REACTION_ADD\",\"d\":{\"channel_id\":\"100\",\"message_id\":\"20\",\"user_id\":\"5\",\"emoji\":{\"id\":\"\",\"name\":\"fire\"}}}");
    ASSERT_EQ_INT(store_messages(s, 100)->react[0].count, 2);
    ASSERT_EQ_INT(store_messages(s, 100)->react[0].me, 1);
    feed("{\"op\":0,\"s\":5,\"t\":\"MESSAGE_UPDATE\",\"d\":{\"id\":\"20\",\"channel_id\":\"100\",\"content\":\"edited\",\"edited_timestamp\":\"2026-10-19T00:00:00.000000+00:00\",\"author\":{\"id\":\"7\",\"username\":\"bob\"}}}");
    ASSERT_EQ_STR(store_messages(s, 100)->content, "edited");
    ASSERT_EQ_INT(store_messages(s, 100)->nreact, 1);
    ASSERT_EQ_INT(store_messages(s, 100)->react[0].count, 2);
    feed("{\"op\":0,\"s\":6,\"t\":\"MESSAGE_REACTION_REMOVE\",\"d\":{\"channel_id\":\"100\",\"message_id\":\"20\",\"user_id\":\"7\",\"emoji\":{\"id\":\"\",\"name\":\"fire\"}}}");
    ASSERT_EQ_INT(store_messages(s, 100)->react[0].count, 1);
    feed("{\"op\":0,\"s\":7,\"t\":\"MESSAGE_REACTION_REMOVE_EMOJI\",\"d\":{\"channel_id\":\"100\",\"message_id\":\"20\",\"emoji\":{\"id\":\"\",\"name\":\"fire\"}}}");
    ASSERT_EQ_INT(store_messages(s, 100)->nreact, 0);
    feed("{\"op\":0,\"s\":8,\"t\":\"MESSAGE_REACTION_ADD\",\"d\":{\"channel_id\":\"100\",\"message_id\":\"20\",\"user_id\":\"7\",\"emoji\":{\"id\":\"\",\"name\":\"cat\"}}}");
    ASSERT_EQ_INT(store_messages(s, 100)->nreact, 1);
    feed("{\"op\":0,\"s\":9,\"t\":\"MESSAGE_REACTION_REMOVE_ALL\",\"d\":{\"channel_id\":\"100\",\"message_id\":\"20\"}}");
    ASSERT_EQ_INT(store_messages(s, 100)->nreact, 0);
    gw_free(g); store_free(s);
}

TEST(test_events_typing){
    setup();
    char out[160];
    feed("{\"op\":0,\"s\":1,\"t\":\"READY\",\"d\":{\"session_id\":\"x\",\"user\":{\"id\":\"5\",\"username\":\"me\"},\"guilds\":[],\"private_channels\":[]}}");
    feed("{\"op\":0,\"s\":2,\"t\":\"TYPING_START\",\"d\":{\"channel_id\":\"100\",\"user_id\":\"7\",\"member\":{\"user\":{\"id\":\"7\",\"username\":\"bob\"}}}}");
    store_typing_str(s, 100, orbis_mono_s(), out, sizeof out);
    ASSERT_EQ_STR(out, "bob is typing...");
    feed("{\"op\":0,\"s\":3,\"t\":\"TYPING_START\",\"d\":{\"channel_id\":\"100\",\"user_id\":\"9\",\"user\":{\"id\":\"9\",\"username\":\"carl\"}}}");
    store_typing_str(s, 100, orbis_mono_s(), out, sizeof out);
    ASSERT_EQ_STR(out, "bob and carl are typing...");
    feed("{\"op\":0,\"s\":4,\"t\":\"TYPING_START\",\"d\":{\"channel_id\":\"200\",\"user_id\":\"11\"}}");
    store_typing_str(s, 200, orbis_mono_s(), out, sizeof out);
    ASSERT_EQ_STR(out, "Someone is typing...");
    gw_free(g); store_free(s);
}

void run_gateway_tests(void){
    g_tests_run++; test_events_ready();
    g_tests_run++; test_events_ready_resume_payload();
    g_tests_run++; test_events_identify_has_token_and_op();
    g_tests_run++; test_events_message_create();
    g_tests_run++; test_events_message_update_sets_edit_flag();
    g_tests_run++; test_events_message_delete();
    g_tests_run++; test_events_malformed();
    g_tests_run++; test_events_seq_tracks_dispatch();
    g_tests_run++; test_events_presence_updates_member();
    g_tests_run++; test_events_reactions();
    g_tests_run++; test_events_typing();
}
