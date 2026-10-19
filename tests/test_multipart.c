#include "tests.h"
#include "../src/model/multipart.h"
#include <stdlib.h>

TEST(test_multipart_file){
    const char *data = "PNG\0DATA";
    char *out = NULL;
    size_t len = 0;
    ASSERT_EQ_INT(multipart_build("BND", NULL, "a\"b.png", "image/png",
                                  data, 8, &out, &len), 0);
    ASSERT_TRUE(out != NULL);
    ASSERT_TRUE(strstr(out, "--BND\r\n") != NULL);
    ASSERT_TRUE(strstr(out, "name=\"files[0]\"; filename=\"a\\\"b.png\"") != NULL);
    ASSERT_TRUE(strstr(out, "Content-Type: image/png\r\n\r\n") != NULL);
    {
        size_t i;
        int found = 0;
        for(i = 0; i + 8 <= len; i++)
            if(!memcmp(out + i, "PNG\0DATA", 8)){ found = 1; break; }
        ASSERT_TRUE(found);
    }
    ASSERT_TRUE(strstr(out + len - 9, "--BND--\r\n") != NULL);
    free(out);
}

TEST(test_multipart_payload){
    char *out = NULL;
    size_t len = 0;
    ASSERT_EQ_INT(multipart_build("B", "{\"content\":\"hi\"}", "f.bin",
                                  NULL, "x", 1, &out, &len), 0);
    ASSERT_TRUE(strstr(out, "name=\"payload_json\"") != NULL);
    ASSERT_TRUE(strstr(out, "{\"content\":\"hi\"}") != NULL);
    ASSERT_TRUE(strstr(out, "application/octet-stream") != NULL);
    ASSERT_EQ_INT(multipart_build(NULL, NULL, "f", NULL, "x", 1, &out,
                                  &len), -1);
    free(out);
}

void run_multipart_tests(void){
    g_tests_run++; test_multipart_file();
    g_tests_run++; test_multipart_payload();
}
