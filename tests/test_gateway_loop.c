#include "tests.h"
#include "../src/core/gateway.h"
#include "../src/model/store.h"

static store *s;
static gateway *g;
static void setup(void){ s = store_new(); g = gw_new(s, "tok"); }

TEST(test_gw_backoff_sequence){
    ASSERT_EQ_INT(gw_backoff_ms(0), 0);
    ASSERT_EQ_INT(gw_backoff_ms(1), 1000);
    ASSERT_EQ_INT(gw_backoff_ms(2), 2000);
    ASSERT_EQ_INT(gw_backoff_ms(3), 4000);
    ASSERT_EQ_INT(gw_backoff_ms(4), 8000);
    ASSERT_EQ_INT(gw_backoff_ms(5), 16000);
    ASSERT_EQ_INT(gw_backoff_ms(6), 30000);
    ASSERT_EQ_INT(gw_backoff_ms(10), 30000);
}

TEST(test_gw_reconnect_schedule){
    setup();
    gw_schedule_reconnect(g, 0);
    ASSERT_EQ_INT(gw_reconnect_due(g, 0), 1);
    gw_on_reconnected(g);
    gw_schedule_reconnect(g, 0);
    gw_on_reconnect_failed(g, 0);
    ASSERT_EQ_INT(gw_reconnect_due(g, 999), 0);
    ASSERT_EQ_INT(gw_reconnect_due(g, 1000), 1);
    gw_on_reconnect_failed(g, 1000);
    ASSERT_EQ_INT(gw_reconnect_due(g, 2999), 0);
    ASSERT_EQ_INT(gw_reconnect_due(g, 3000), 1);
    gw_on_reconnected(g);
    gw_schedule_reconnect(g, 5000);
    ASSERT_EQ_INT(gw_reconnect_due(g, 5000), 1);
    gw_free(g); store_free(s);
}

TEST(test_gw_heartbeat_cadence){
    const char *hello = "{\"op\":10,\"d\":{\"heartbeat_interval\":10000}}";
    setup();
    gw_feed(g, hello, strlen(hello));
    ASSERT_EQ_INT(gw_interval_ms(g), 10000);
    gw_mark_beat(g, 0);
    ASSERT_EQ_INT(gw_heartbeat_due(g, 9999), 0);
    ASSERT_EQ_INT(gw_heartbeat_due(g, 10000), 1);
    gw_mark_beat(g, 10000);
    ASSERT_EQ_INT(gw_ack_expired(g, 10000), 0);
    ASSERT_EQ_INT(gw_ack_expired(g, 19000), 0);
    ASSERT_EQ_INT(gw_ack_expired(g, 20000), 1);
    gw_feed(g, "{\"op\":11}", 9);
    ASSERT_EQ_INT(gw_ack_expired(g, 25000), 0);
    gw_free(g); store_free(s);
}

TEST(test_gw_invalid_session_resets){
    const char *ready = "{\"op\":0,\"s\":9,\"t\":\"READY\",\"d\":{\"session_id\":\"s1\",\"user\":{\"id\":\"5\",\"username\":\"me\"},\"guilds\":[],\"private_channels\":[]}}";
    setup();
    gw_feed(g, ready, strlen(ready));
    char out[512];
    ASSERT_TRUE(gw_resume(g, out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"seq\":9") != NULL);
    gw_feed(g, "{\"op\":9,\"d\":true}", strlen("{\"op\":9,\"d\":true}"));
    ASSERT_TRUE(gw_resume(g, out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"seq\":0") != NULL);
    gw_free(g); store_free(s);
}

void run_gateway_loop_tests(void){
    g_tests_run++; test_gw_backoff_sequence();
    g_tests_run++; test_gw_reconnect_schedule();
    g_tests_run++; test_gw_heartbeat_cadence();
    g_tests_run++; test_gw_invalid_session_resets();
}
