#include "tests.h"
#include "../src/model/tenor.h"

static const char *FIXTURE =
    "{\"results\":["
    "{\"id\":\"1\",\"content_description\":\"cat\",\"media_formats\":{"
    "\"gif\":{\"url\":\"https://media.tenor.com/abc/one.gif\","
    "\"dims\":[498,278],\"size\":123},"
    "\"tinygif\":{\"url\":\"https://media.tenor.com/abc/one-tiny.gif\","
    "\"dims\":[220,122]}}},"
    "{\"id\":\"2\",\"content_description\":\"dog\",\"media_formats\":{"
    "\"gif\":{\"url\":\"https://media.tenor.com/def/two.gif\","
    "\"dims\":[300,200]}}},"
    "{\"id\":\"3\",\"media_formats\":{}},"
    "{\"id\":\"4\",\"media_formats\":{\"gif\":{\"url\":\"\"}}}"
    "]}";

TEST(test_tenor_parse_results){
    gif_item items[4];
    int n = gif_from_tenor(FIXTURE, items, 4);
    ASSERT_EQ_INT(n, 2);
    ASSERT_EQ_STR(items[0].url, "https://media.tenor.com/abc/one.gif");
    ASSERT_EQ_STR(items[0].preview,
                  "https://media.tenor.com/abc/one-tiny.gif");
    ASSERT_EQ_INT(items[0].w, 498);
    ASSERT_EQ_INT(items[0].h, 278);
    ASSERT_EQ_STR(items[1].url, "https://media.tenor.com/def/two.gif");
    ASSERT_EQ_STR(items[1].preview, "https://media.tenor.com/def/two.gif");
    ASSERT_EQ_INT(items[1].w, 300);
}

TEST(test_tenor_parse_edges){
    gif_item items[4];
    ASSERT_EQ_INT(gif_from_tenor(NULL, items, 4), 0);
    ASSERT_EQ_INT(gif_from_tenor("not json", items, 4), 0);
    ASSERT_EQ_INT(gif_from_tenor("{\"results\":[]}", items, 4), 0);
    ASSERT_EQ_INT(gif_from_tenor(FIXTURE, items, 1), 1);
}

void run_tenor_tests(void){
    g_tests_run++; test_tenor_parse_results();
    g_tests_run++; test_tenor_parse_edges();
}
