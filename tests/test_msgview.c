#include "tests.h"
#include "../src/ui/msgview.c"

static int fake_measure(void *ud, const char *s, size_t len){
    (void)ud; (void)s;
    return (int)len * 10;
}

static uint64_t mid(uint64_t ms, uint64_t n){
    return ((ms - 1420070400000ULL) << 22) | (n & 0x3FFFFFu);
}

static message mkmsg(uint64_t ms, uint64_t n, uint64_t author, const char *text, int grouped){
    message m;
    memset(&m, 0, sizeof m);
    m.id = mid(ms, n);
    m.chan = 1;
    m.author_id = author;
    m.is_grouped = grouped;
    snprintf(m.author_name, sizeof m.author_name, "user%llu", (unsigned long long)author);
    snprintf(m.content, sizeof m.content, "%s", text);
    return m;
}

TEST(test_msgview_wraps_words){
    message a = mkmsg(1500000000000ULL, 1, 7, "aaaa bbbb cccc", 0);
    mv_layout L;
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 100, fake_measure, NULL), 4);
    ASSERT_EQ_INT(L.lines[0].is_sep, 1);
    ASSERT_EQ_INT(L.lines[1].author, 1);
    ASSERT_EQ_INT(L.lines[2].len, 9);
    ASSERT_EQ_INT(L.lines[2].off, 0);
    ASSERT_EQ_INT(L.lines[3].off, 10);
    ASSERT_EQ_INT(L.lines[3].len, 4);
    mv_free(&L);
}

TEST(test_msgview_newlines){
    message a = mkmsg(1500000000000ULL, 1, 7, "a\nb", 0);
    mv_layout L;
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 1000, fake_measure, NULL), 4);
    ASSERT_EQ_INT(L.lines[2].len, 1);
    ASSERT_EQ_INT(L.lines[3].len, 1);
    mv_free(&L);
}

TEST(test_msgview_blank_line_preserved){
    message a = mkmsg(1500000000000ULL, 1, 7, "a\n\nb", 0);
    mv_layout L;
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 1000, fake_measure, NULL), 5);
    ASSERT_EQ_INT(L.lines[3].len, 0);
    mv_free(&L);
}

TEST(test_msgview_hard_breaks_long_words){
    message a = mkmsg(1500000000000ULL, 1, 7, "abcdefghij", 0);
    mv_layout L;
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 50, fake_measure, NULL), 4);
    ASSERT_EQ_INT(L.lines[2].len, 5);
    ASSERT_EQ_INT(L.lines[3].len, 5);
    mv_free(&L);
}

TEST(test_msgview_never_splits_utf8){
    message a = mkmsg(1500000000000ULL, 1, 7, "\xC3\xA9\xC3\xA9", 0);
    mv_layout L;
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 30, fake_measure, NULL), 4);
    ASSERT_EQ_INT(L.lines[2].len, 2);
    ASSERT_EQ_INT(L.lines[3].len, 2);
    mv_free(&L);
}

TEST(test_msgview_grouping_skips_author_row){
    message a = mkmsg(1500000000000ULL, 1, 7, "one", 0);
    message b = mkmsg(1500000000000ULL, 2, 7, "two", 1);
    mv_layout L;
    a.next = &b;
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 1000, fake_measure, NULL), 4);
    ASSERT_EQ_INT(L.lines[1].author, 1);
    ASSERT_EQ_INT(L.lines[2].author, 0);
    ASSERT_EQ_INT(L.lines[3].author, 0);
    mv_free(&L);
}

TEST(test_msgview_day_separator){
    message a = mkmsg(1500000000000ULL, 1, 7, "one", 0);
    message b = mkmsg(1500086400000ULL, 2, 8, "two", 0);
    mv_layout L;
    a.next = &b;
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 1000, fake_measure, NULL), 6);
    ASSERT_EQ_INT(L.lines[0].is_sep, 1);
    ASSERT_TRUE(L.lines[0].m == &a);
    ASSERT_EQ_INT(L.lines[3].is_sep, 1);
    ASSERT_TRUE(L.lines[3].m == &b);
    mv_free(&L);
}

TEST(test_msgview_height_accounts_for_separator_and_gap){
    message a = mkmsg(1500000000000ULL, 1, 7, "hi", 0);
    mv_layout L;
    mv_init(&L);
    mv_build(&L, &a, 1000, fake_measure, NULL);
    ASSERT_EQ_INT(L.height, MV_SEP_H + MV_LINE_H + MV_LINE_H + MV_GROUP_GAP);
    mv_free(&L);
}

TEST(test_msgview_attachment_rows){
    message a = mkmsg(1500000000000ULL, 1, 7, "look", 0);
    mv_layout L;
    a.natt = 2;
    snprintf(a.att[0].url, sizeof a.att[0].url, "https://cdn.discordapp.com/x.png");
    a.att[0].image = 1;
    a.att[0].w = 800;
    a.att[0].h = 600;
    snprintf(a.att[1].name, sizeof a.att[1].name, "file.zip");
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 1000, fake_measure, NULL), 5);
    ASSERT_EQ_INT(L.lines[3].att, 0);
    ASSERT_EQ_INT(L.lines[3].att_w, 346);
    ASSERT_EQ_INT(L.lines[3].att_h, 260);
    ASSERT_EQ_INT(L.lines[4].att, 1);
    ASSERT_EQ_INT(L.lines[4].att_h, MV_LINE_H);
    ASSERT_EQ_INT(L.height, 34 + 22 + 22 + 260 + 6 + 22 + 2 + 8);
    mv_free(&L);
}

TEST(test_msgview_embed_rows){
    message a = mkmsg(1500000000000ULL, 1, 7, "hi", 0);
    mv_layout L;
    a.nemb = 1;
    snprintf(a.emb[0].title, sizeof a.emb[0].title, "Hello");
    snprintf(a.emb[0].image_url, sizeof a.emb[0].image_url,
             "https://cdn.discordapp.com/a.png");
    a.emb[0].image_w = 800;
    a.emb[0].image_h = 400;
    a.emb[0].has_color = 1;
    a.emb[0].color = 0xFF5865F2u;
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 1000, fake_measure, NULL), 5);
    ASSERT_EQ_INT(L.lines[3].emb, 0);
    ASSERT_EQ_INT(L.lines[3].emb_kind, 1);
    ASSERT_EQ_INT(L.lines[4].emb_kind, 3);
    ASSERT_EQ_INT(L.lines[4].att_w, 400);
    ASSERT_EQ_INT(L.lines[4].att_h, 200);
    mv_free(&L);
}

TEST(test_msgview_reaction_row){
    message a = mkmsg(1500000000000ULL, 1, 7, "hi", 0);
    mv_layout L;
    a.nreact = 2;
    snprintf(a.react[0].emoji, sizeof a.react[0].emoji, "fire");
    a.react[0].count = 2;
    a.react[0].me = 1;
    snprintf(a.react[1].emoji, sizeof a.react[1].emoji, "cat");
    a.react[1].count = 1;
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 1000, fake_measure, NULL), 4);
    ASSERT_EQ_INT(L.lines[2].react, -1);
    ASSERT_EQ_INT(L.lines[3].react, 1);
    ASSERT_EQ_INT(L.lines[3].att_h, MV_REACT_H);
    ASSERT_EQ_INT(L.height,
                  MV_SEP_H + MV_LINE_H + MV_LINE_H + MV_REACT_H + 4 + MV_GROUP_GAP);
    mv_free(&L);
}

TEST(test_msgview_reply_row){
    message a = mkmsg(1500000000000ULL, 1, 7, "yes", 0);
    mv_layout L;
    a.reply_to = 99;
    snprintf(a.reply_name, sizeof a.reply_name, "alice");
    snprintf(a.reply_text, sizeof a.reply_text, "the original");
    mv_init(&L);
    ASSERT_EQ_INT(mv_build(&L, &a, 1000, fake_measure, NULL), 4);
    ASSERT_EQ_INT(L.lines[1].author, 1);
    ASSERT_EQ_INT(L.lines[1].reply, -1);
    ASSERT_EQ_INT(L.lines[2].reply, 1);
    ASSERT_EQ_INT(L.lines[3].len, 3);
    ASSERT_EQ_INT(L.height,
                  MV_SEP_H + MV_LINE_H + MV_REPLY_H + MV_LINE_H + MV_GROUP_GAP);
    mv_free(&L);
}

TEST(test_msgview_image_list){
    message a = mkmsg(1500000000000ULL, 1, 7, "", 0);
    message b = mkmsg(1500000000000ULL, 2, 7, "", 1);
    const char *urls[4];
    a.natt = 2;
    snprintf(a.att[0].url, sizeof a.att[0].url, "https://cdn/a.png");
    a.att[0].image = 1;
    snprintf(a.att[1].url, sizeof a.att[1].url, "https://cdn/file.zip");
    a.nemb = 1;
    snprintf(a.emb[0].image_url, sizeof a.emb[0].image_url, "https://cdn/e.png");
    b.natt = 1;
    snprintf(b.att[0].url, sizeof b.att[0].url, "https://cdn/b.gif");
    b.att[0].image = 1;
    a.next = &b;
    ASSERT_EQ_INT(mv_images(&a, urls, 4), 3);
    ASSERT_EQ_STR(urls[0], "https://cdn/a.png");
    ASSERT_EQ_STR(urls[1], "https://cdn/e.png");
    ASSERT_EQ_STR(urls[2], "https://cdn/b.gif");
}

TEST(test_msgview_time_and_day_strings){
    char buf[64];
    mv_time_str(1420070400000ULL, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "00:00");
    mv_day_str(1420070400000ULL, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "January 1, 2015");
    mv_time_str(((14ULL * 3600ULL) + (32ULL * 60ULL)) * 1000ULL, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "14:32");
    mv_day_str(0, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "January 1, 1970");
}

void run_msgview_tests(void){
    g_tests_run++; test_msgview_wraps_words();
    g_tests_run++; test_msgview_newlines();
    g_tests_run++; test_msgview_blank_line_preserved();
    g_tests_run++; test_msgview_hard_breaks_long_words();
    g_tests_run++; test_msgview_never_splits_utf8();
    g_tests_run++; test_msgview_grouping_skips_author_row();
    g_tests_run++; test_msgview_day_separator();
    g_tests_run++; test_msgview_height_accounts_for_separator_and_gap();
    g_tests_run++; test_msgview_attachment_rows();
    g_tests_run++; test_msgview_embed_rows();
    g_tests_run++; test_msgview_reaction_row();
    g_tests_run++; test_msgview_reply_row();
    g_tests_run++; test_msgview_image_list();
    g_tests_run++; test_msgview_time_and_day_strings();
}
