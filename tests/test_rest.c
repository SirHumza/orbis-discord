#include "tests.h"
#include "../src/core/http.h"
#include "../src/core/rest.h"
#include <stdlib.h>

typedef struct {
    const char *responses[8];
    int nresp, idx;
    char capture[4096];
    size_t captured;
    size_t off;
} memio;

static int mread(void *ctx, char *buf, size_t cap){
    memio *m = ctx;
    const char *r = m->responses[m->idx];
    size_t len;
    if(!r) return 0;
    len = strlen(r);
    if(m->off >= len) return 0;
    size_t n = len - m->off < cap ? len - m->off : cap;
    memcpy(buf, r + m->off, n);
    m->off += n;
    return (int)n;
}
static int mwrite(void *ctx, const char *buf, size_t len){
    memio *m = ctx;
    if(m->captured + len >= sizeof m->capture) return -1;
    memcpy(m->capture + m->captured, buf, len);
    m->captured += len;
    m->capture[m->captured] = 0;
    return (int)len;
}
static void mnext(memio *m){ m->idx++; m->off = 0; m->captured = 0; m->capture[0] = 0; }

static void cb_count(int status, const char *body, void *ud){
    int *calls = ud;
    (void)status; (void)body;
    (*calls)++;
}

TEST(test_http_get_headers_and_parse){
    memio m = {0};
    m.responses[0] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
    http_io io = {mread, mwrite, &m};
    http *h = http_new(io);
    http_resp r;
    ASSERT_EQ_INT(http_request(h, "discord.com", "GET", "/users/@me", NULL, "tok123", &r), 0);
    ASSERT_EQ_INT(r.status, 200);
    ASSERT_EQ_STR(r.body, "{}");
    ASSERT_TRUE(strstr(m.capture, "GET /users/@me HTTP/1.1") == m.capture);
    ASSERT_TRUE(strstr(m.capture, "Host: discord.com") != NULL);
    ASSERT_TRUE(strstr(m.capture, "Authorization: tok123") != NULL);
    ASSERT_TRUE(strstr(m.capture, "Content-Length: 0") != NULL);
    http_resp_free(&r);
    http_free(h);
}

TEST(test_http_post_body){
    memio m = {0};
    m.responses[0] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    http_io io = {mread, mwrite, &m};
    http *h = http_new(io);
    http_resp r;
    ASSERT_EQ_INT(http_request(h, "discord.com", "POST", "/channels/1/messages", "{\"content\":\"hi\"}", "t", &r), 0);
    ASSERT_TRUE(strstr(m.capture, "Content-Length: 16") != NULL);
    ASSERT_TRUE(strstr(m.capture, "{\"content\":\"hi\"}") != NULL);
    http_resp_free(&r);
    http_free(h);
}

TEST(test_rest_ratelimit){
    memio m = {0};
    m.responses[0] = "HTTP/1.1 429 Too Many Requests\r\nContent-Type: application/json\r\nX-RateLimit-Reset-After: 0.100\r\nContent-Length: 26\r\n\r\n{\"retry_after\": 0.1}";
    m.responses[1] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    m.responses[2] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    http_io io = {mread, mwrite, &m};
    http *h = http_new(io);
    rest *r = rest_new(h, "tok123");
    int calls = 0;
    ASSERT_TRUE(rest_send(r, "POST", "/channels/1/messages", "{\"content\":\"a\"}", cb_count, &calls) == 0);
    ASSERT_TRUE(rest_send(r, "POST", "/channels/1/messages", "{\"content\":\"b\"}", cb_count, &calls) == 0);
    ASSERT_TRUE(rest_send(r, "POST", "/channels/1/messages", "{\"content\":\"c\"}", cb_count, &calls) == 0);
    ASSERT_EQ_INT(rest_pump(r, 0), 1);
    ASSERT_EQ_INT(calls, 0);
    ASSERT_EQ_INT(rest_pump(r, 50), 0);
    ASSERT_EQ_INT(calls, 0);
    mnext(&m);
    ASSERT_EQ_INT(rest_pump(r, 100), 2);
    ASSERT_EQ_INT(calls, 2);
    ASSERT_TRUE(strstr(m.capture, "Authorization: tok123") != NULL);
    rest_free(r);
    http_free(h);
}

TEST(test_http_omits_auth_when_token_empty){
    memio m = {0};
    m.responses[0] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
    http_io io = {mread, mwrite, &m};
    http *h = http_new(io);
    http_resp r;
    ASSERT_EQ_INT(http_request(h, "discord.com", "POST", "/api/v9/users/@me/remote-auth/login",
                               "{\"ticket\":\"x\"}", "", &r), 0);
    ASSERT_TRUE(strstr(m.capture, "Authorization") == NULL);
    ASSERT_TRUE(strstr(m.capture, "Content-Type: application/json") != NULL);
    http_resp_free(&r);
    http_free(h);
}

TEST(test_rest_retry_after_from_json_body){
    memio m = {0};
    m.responses[0] = "HTTP/1.1 429 Too Many Requests\r\nContent-Type: application/json\r\nContent-Length: 19\r\n\r\n{\"retry_after\":0.5}";
    http_io io = {mread, mwrite, &m};
    http *h = http_new(io);
    rest *r = rest_new(h, "t");
    int calls = 0;
    rest_send(r, "GET", "/x", NULL, cb_count, &calls);
    ASSERT_EQ_INT(rest_pump(r, 0), 1);
    ASSERT_EQ_INT(rest_blocked_until(r), 500);
    rest_free(r);
    http_free(h);
}

void run_rest_tests(void){
    g_tests_run++; test_http_get_headers_and_parse();
    g_tests_run++; test_http_post_body();
    g_tests_run++; test_rest_ratelimit();
    g_tests_run++; test_http_omits_auth_when_token_empty();
    g_tests_run++; test_rest_retry_after_from_json_body();
}
