#include "tests.h"
#include "../src/core/b64.h"
#include "../src/core/jsonlite.h"

TEST(test_b64_roundtrip){
    const unsigned char raw[] = {0x00, 0x01, 0xfe, 0xff, 'h', 'i'};
    char enc[32];
    size_t el = b64_encode(raw, sizeof raw, enc);
    ASSERT_EQ_STR(enc, "AAH+/2hp");
    ASSERT_EQ_INT(el, 8);
    unsigned char dec[32];
    size_t dl = b64_decode(enc, dec, sizeof dec);
    ASSERT_EQ_INT(dl, sizeof raw);
    ASSERT_EQ_MEM(dec, raw, sizeof raw);
}

TEST(test_b64_decode_rejects_invalid){
    unsigned char dec[8];
    ASSERT_EQ_INT(b64_decode("!!!!", dec, sizeof dec), (size_t)-1);
}

TEST(test_jsonlite_reads_nested_object){
    const char *json = "{\"op\":0,\"d\":{\"id\":\"123\",\"n\":7,\"v\":null}}";
    jl_val_t *v = jl_parse(json, strlen(json));
    ASSERT_TRUE(v != NULL);
    jl_val_t *d = jl_obj_get(v, "d");
    ASSERT_TRUE(d != NULL);
    ASSERT_EQ_STR(jl_obj_get(d, "id")->str, "123");
    ASSERT_EQ_INT(jl_obj_get(d, "n")->num, 7);
    ASSERT_EQ_INT(jl_obj_get(d, "v")->type, JL_NULL);
    jl_free(v);
}

TEST(test_jsonlite_rejects_truncated){
    const char *json = "{\"op\":0,\"d\":{";
    ASSERT_TRUE(jl_parse(json, strlen(json)) == NULL);
}

void run_json_b64_tests(void){
    g_tests_run++;
    test_b64_roundtrip();
    g_tests_run++;
    test_b64_decode_rejects_invalid();
    g_tests_run++;
    test_jsonlite_reads_nested_object();
    g_tests_run++;
    test_jsonlite_rejects_truncated();
}
