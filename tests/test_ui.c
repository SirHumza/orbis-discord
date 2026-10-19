#include "tests.h"

static void test_ui_placeholder(void){
    ASSERT_TRUE(1);
}

void run_ui_tests(void){
    g_tests_run++; test_ui_placeholder();
}
