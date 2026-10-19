#include "tests.h"

int g_tests_run = 0, g_tests_failed = 0;

int main(void){
    run_ws_streaming_tests();
    run_json_b64_tests();
    run_store_tests();
    run_gateway_tests();
    run_rest_tests();
    run_remoteauth_tests();
    run_gateway_loop_tests();
    run_ui_tests();
    run_image_tests();
    run_nav_tests();
    run_msgview_tests();
    printf("%d checks, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed ? 1 : 0;
}
