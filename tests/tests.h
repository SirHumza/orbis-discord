#ifndef TESTS_H
#define TESTS_H
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int g_tests_run, g_tests_failed;

#define TEST(name) static void name(void)
#define ASSERT_TRUE(c) do { if(!(c)){ printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_tests_failed++; return; } } while(0)
#define ASSERT_EQ_INT(a,b) do { long long _a=(long long)(a), _b=(long long)(b); if(_a!=_b){ printf("  FAIL %s:%d: %lld != %lld\n", __FILE__, __LINE__, _a, _b); g_tests_failed++; return; } } while(0)
#define ASSERT_EQ_STR(a,b) do { if(strcmp((a),(b))!=0){ printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); g_tests_failed++; return; } } while(0)
#define ASSERT_EQ_MEM(a,b,n) do { if(memcmp((a),(b),(n))!=0){ printf("  FAIL %s:%d: memcmp(%s, %s, %zu)\n", __FILE__, __LINE__, #a, #b, (size_t)(n)); g_tests_failed++; return; } } while(0)

void run_ws_streaming_tests(void);
void run_json_b64_tests(void);
void run_store_tests(void);
void run_gateway_tests(void);

#endif
