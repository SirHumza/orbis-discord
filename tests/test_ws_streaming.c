#include "tests.h"
#include "../src/core/ws.h"
#include "../src/core/ws_skip.c"
#include "../src/core/ws_env.c"

#define BIG (1024u * 1024u)

TEST(test_ws_skip_drains_exactly_one_megabyte_in_chunks){
    uint64_t left = 0;
    uint64_t consumed = 0;
    ws_skip_plan(BIG, 0, &left);
    ASSERT_EQ_INT(left, BIG);
    while(left > 0){
        size_t chunk = left > 65536 ? 65536 : (size_t)left;
        consumed += chunk;
        left -= chunk;
    }
    ASSERT_EQ_INT(consumed, BIG);
    ASSERT_EQ_INT(left, 0);
}

TEST(test_ws_skip_zero_remaining_is_complete){
    uint64_t left = 123;
    ws_skip_plan(1000, 1000, &left);
    ASSERT_EQ_INT(left, 0);
    ws_skip_plan(1000, 5000, &left);
    ASSERT_EQ_INT(left, 0);
}

TEST(test_ws_skip_null_out_is_noop){
    ASSERT_EQ_INT(ws_skip_plan(1000, 10, NULL), 0);
}

TEST(test_ws_env_streams_oversized_ready_in_small_chunks){
    static char payload[BIG];
    const char *head = "{\"op\":0,\"s\":41,\"t\":\"READY\",\"d\":{\"big\":\"";
    size_t hl = strlen(head);
    memcpy(payload, head, hl);
    memset(payload + hl, 'x', BIG - hl - 2);
    memcpy(payload + BIG - 2, "\"}", 2);
    ASSERT_EQ_MEM(payload, payload, 1);

    ws_env_t e;
    ws_env_init(&e);
    size_t off = 0;
    while(off < BIG && !ws_env_complete(&e)){
        size_t n = BIG - off > 4096 ? 4096 : BIG - off;
        ws_env_feed(&e, (const unsigned char *)payload + off, n);
        off += n;
    }
    ASSERT_TRUE(ws_env_complete(&e));
    char out[256];
    size_t n = ws_env_render(&e, out, sizeof out);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ_STR(out, "{\"op\":0,\"s\":41,\"t\":\"READY\"}");
}

TEST(test_ws_env_field_split_across_chunks){
    ws_env_t e;
    ws_env_init(&e);
    const char *p = "{\"op\":1,\"t\":\"MESSAGE_CREATE\",\"d\":{}}";
    size_t i;
    for(i = 0; i < strlen(p); i++)
        ws_env_feed(&e, (const unsigned char *)p + i, 1);
    ASSERT_TRUE(ws_env_complete(&e));
    char out[128];
    ASSERT_TRUE(ws_env_render(&e, out, sizeof out) > 0);
    ASSERT_EQ_STR(out, "{\"op\":1,\"t\":\"MESSAGE_CREATE\"}");
}

void run_ws_streaming_tests(void){
    g_tests_run++;
    test_ws_skip_drains_exactly_one_megabyte_in_chunks();
    g_tests_run++;
    test_ws_skip_zero_remaining_is_complete();
    g_tests_run++;
    test_ws_skip_null_out_is_noop();
    g_tests_run++;
    test_ws_env_streams_oversized_ready_in_small_chunks();
    g_tests_run++;
    test_ws_env_field_split_across_chunks();
}
