#include "tests.h"
#include "../src/model/tone.h"

TEST(test_tone_render_shapes){
    int16_t buf[16000];
    int n = tone_render(TONE_MSG, buf, 16000);
    int i, peak = 0;
    ASSERT_EQ_INT(n, 44100 * 140 / 1000);
    for(i = 0; i < n; i++)
        if(buf[i] > peak) peak = buf[i];
    ASSERT_TRUE(peak > 1000);
    ASSERT_EQ_INT(tone_frames(TONE_MENTION), 44100 * 240 / 1000);
    ASSERT_EQ_INT(tone_render(TONE_MENTION, buf, 100), 100);
}

TEST(test_tone_render_edges){
    int16_t buf[16000];
    ASSERT_EQ_INT(tone_render(TONE_MSG, NULL, 100), 0);
    ASSERT_EQ_INT(tone_render(TONE_MSG, buf, 0), 0);
    ASSERT_EQ_INT(tone_render(7, buf, 100), 0);
}

void run_tone_tests(void){
    g_tests_run++; test_tone_render_shapes();
    g_tests_run++; test_tone_render_edges();
}
