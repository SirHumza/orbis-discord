#include "tests.h"
#include "../src/model/voice.h"

TEST(test_voice_payloads){
    char out[512];
    ASSERT_TRUE(voice_state_json(10, 20, out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"op\":4") != NULL);
    ASSERT_TRUE(strstr(out, "\"guild_id\":\"10\"") != NULL);
    ASSERT_TRUE(strstr(out, "\"channel_id\":\"20\"") != NULL);
    ASSERT_TRUE(voice_state_json(10, 0, out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"channel_id\":null") != NULL);
    ASSERT_TRUE(voice_identify_json(10, 77, "sess", "tok", out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"server_id\":\"10\"") != NULL);
    ASSERT_TRUE(strstr(out, "\"user_id\":\"77\"") != NULL);
    ASSERT_TRUE(strstr(out, "\"session_id\":\"sess\"") != NULL);
    ASSERT_TRUE(voice_select_json("1.2.3.4", 50000, "xsalsa20_poly1305",
                                  out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"address\":\"1.2.3.4\"") != NULL);
    ASSERT_TRUE(strstr(out, "\"port\":50000") != NULL);
    ASSERT_TRUE(voice_speaking_json(99, 1, out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"ssrc\":99") != NULL);
    ASSERT_TRUE(voice_heartbeat_json(123456, out, sizeof out) > 0);
    ASSERT_EQ_STR(out, "{\"op\":3,\"d\":123456}");
}

TEST(test_voice_ready){
    const char *json =
        "{\"op\":2,\"d\":{\"ssrc\":1234,\"ip\":\"5.6.7.8\",\"port\":50001,"
        "\"modes\":[\"aead_xchacha20_poly1305_rtpsize\","
        "\"xsalsa20_poly1305\"]}}";
    voice_ready r;
    ASSERT_EQ_INT(voice_ready_parse(json, &r), 1);
    ASSERT_EQ_INT(r.ssrc, 1234);
    ASSERT_EQ_STR(r.ip, "5.6.7.8");
    ASSERT_EQ_INT(r.port, 50001);
    ASSERT_EQ_INT(r.modes, VOICE_MODE_XCHACHA | VOICE_MODE_XSALSA);
    ASSERT_EQ_INT(voice_ready_parse("{\"op\":2,\"d\":{}}", &r), 0);
    ASSERT_EQ_INT(voice_ready_parse(NULL, &r), 0);
}

TEST(test_voice_mode_pick){
    ASSERT_EQ_STR(voice_pick_mode(VOICE_MODE_XSALSA | VOICE_MODE_XCHACHA),
                  "aead_xchacha20_poly1305_rtpsize");
    ASSERT_EQ_STR(voice_pick_mode(VOICE_MODE_XSALSA), "xsalsa20_poly1305");
    ASSERT_EQ_STR(voice_pick_mode(0), "");
}

TEST(test_voice_session){
    const char *json =
        "{\"op\":4,\"d\":{\"mode\":\"xsalsa20_poly1305\","
        "\"secret_key\":[1,2,255]}}";
    char mode[48];
    unsigned char key[32];
    size_t len = 0;
    ASSERT_EQ_INT(voice_session_parse(json, mode, sizeof mode, key, sizeof key,
                                      &len), 1);
    ASSERT_EQ_STR(mode, "xsalsa20_poly1305");
    ASSERT_EQ_INT(len, 3);
    ASSERT_EQ_INT(key[0], 1);
    ASSERT_EQ_INT(key[1], 2);
    ASSERT_EQ_INT(key[2], 255);
    ASSERT_EQ_INT(voice_session_parse("{\"op\":4,\"d\":{}}", mode,
                                      sizeof mode, key, sizeof key, &len), 0);
}

TEST(test_voice_discovery){
    unsigned char pkt[80], resp[74];
    char ip[64];
    int port;
    ASSERT_EQ_INT(voice_discovery_packet(0x01020304u, pkt, sizeof pkt), 74);
    ASSERT_EQ_INT(pkt[1], 1);
    ASSERT_EQ_INT(pkt[3], 70);
    ASSERT_EQ_INT(pkt[4], 1);
    ASSERT_EQ_INT(pkt[7], 4);
    memset(resp, 0, sizeof resp);
    resp[0] = 0; resp[1] = 2;
    memcpy(resp + 8, "9.8.7.6", 7);
    resp[72] = (unsigned char)(50002 >> 8);
    resp[73] = (unsigned char)(50002 & 0xff);
    ASSERT_EQ_INT(voice_discovery_parse(resp, sizeof resp, ip, sizeof ip,
                                        &port), 1);
    ASSERT_EQ_STR(ip, "9.8.7.6");
    ASSERT_EQ_INT(port, 50002);
    resp[1] = 1;
    ASSERT_EQ_INT(voice_discovery_parse(resp, sizeof resp, ip, sizeof ip,
                                        &port), 0);
}

TEST(test_voice_endpoint){
    char host[64];
    int port = 0;
    ASSERT_EQ_INT(voice_endpoint_split("wss://c-ams2.discord.media:443",
                                       host, sizeof host, &port), 1);
    ASSERT_EQ_STR(host, "c-ams2.discord.media");
    ASSERT_EQ_INT(port, 443);
    ASSERT_EQ_INT(voice_endpoint_split("c-ams2.discord.media", host,
                                       sizeof host, &port), 1);
    ASSERT_EQ_STR(host, "c-ams2.discord.media");
    ASSERT_EQ_INT(port, 443);
    ASSERT_EQ_INT(voice_endpoint_split("host.example:8443", host, sizeof host,
                                       &port), 1);
    ASSERT_EQ_INT(port, 8443);
    ASSERT_EQ_INT(voice_endpoint_split("", host, sizeof host, &port), 0);
}

void run_voice_tests(void){
    g_tests_run++; test_voice_payloads();
    g_tests_run++; test_voice_ready();
    g_tests_run++; test_voice_mode_pick();
    g_tests_run++; test_voice_session();
    g_tests_run++; test_voice_discovery();
    g_tests_run++; test_voice_endpoint();
}
