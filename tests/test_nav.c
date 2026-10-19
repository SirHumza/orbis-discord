#include "tests.h"
#include "../src/ui/nav.c"

TEST(test_nav_init_zero){
    nav n;
    nav_init(&n);
    ASSERT_EQ_INT(n.index, 0);
}

TEST(test_nav_wraps_up_from_first){
    nav n;
    nav_init(&n);
    ASSERT_EQ_INT(nav_step(&n, IE_UP, 5), 4);
    ASSERT_EQ_INT(nav_step(&n, IE_UP, 5), 3);
}

TEST(test_nav_wraps_down_from_last){
    nav n;
    nav_init(&n);
    ASSERT_EQ_INT(nav_step(&n, IE_DOWN, 3), 1);
    ASSERT_EQ_INT(nav_step(&n, IE_DOWN, 3), 2);
    ASSERT_EQ_INT(nav_step(&n, IE_DOWN, 3), 0);
}

TEST(test_nav_single_item_stays){
    nav n;
    nav_init(&n);
    ASSERT_EQ_INT(nav_step(&n, IE_DOWN, 1), 0);
    ASSERT_EQ_INT(nav_step(&n, IE_UP, 1), 0);
}

TEST(test_nav_zero_count_resets){
    nav n;
    nav_init(&n);
    nav_step(&n, IE_DOWN, 4);
    ASSERT_EQ_INT(nav_step(&n, IE_DOWN, 0), 0);
    ASSERT_EQ_INT(nav_step(&n, IE_UP, 0), 0);
}

TEST(test_nav_clamps_when_count_shrinks){
    nav n;
    int i;
    nav_init(&n);
    for(i = 0; i < 9; i++) nav_step(&n, IE_DOWN, 10);
    ASSERT_EQ_INT(n.index, 9);
    ASSERT_EQ_INT(nav_step(&n, IE_NONE, 3), 2);
    ASSERT_EQ_INT(nav_step(&n, IE_DOWN, 3), 0);
}

TEST(test_nav_ignores_other_events){
    nav n;
    nav_init(&n);
    nav_step(&n, IE_DOWN, 4);
    ASSERT_EQ_INT(nav_step(&n, IE_SELECT, 4), 1);
    ASSERT_EQ_INT(nav_step(&n, IE_BACK, 4), 1);
    ASSERT_EQ_INT(nav_step(&n, IE_LEFT, 4), 1);
    ASSERT_EQ_INT(nav_step(&n, IE_RIGHT, 4), 1);
    ASSERT_EQ_INT(nav_step(&n, IE_NONE, 4), 1);
}

void run_nav_tests(void){
    g_tests_run++; test_nav_init_zero();
    g_tests_run++; test_nav_wraps_up_from_first();
    g_tests_run++; test_nav_wraps_down_from_last();
    g_tests_run++; test_nav_single_item_stays();
    g_tests_run++; test_nav_zero_count_resets();
    g_tests_run++; test_nav_clamps_when_count_shrinks();
    g_tests_run++; test_nav_ignores_other_events();
}
