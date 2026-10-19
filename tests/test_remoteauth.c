#include "tests.h"
#include "../src/core/remoteauth.h"
#include "../src/core/b64.h"
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>

static char *b64url(const unsigned char *in, size_t n){
    static char tmp[1024];
    char enc[2048];
    size_t el = b64_encode(in, n, enc);
    size_t o = 0;
    for(size_t i = 0; i < el; i++){
        if(enc[i] == '=') break;
        tmp[o++] = enc[i] == '+' ? '-' : enc[i] == '/' ? '_' : enc[i];
    }
    tmp[o] = 0;
    return tmp;
}

static int encrypt_with_pub(ra *r, const char *plain, char *out, size_t cap){
    (void)cap;
    unsigned char der[1024];
    char pb[1024];
    unsigned char enc[512];
    size_t dl, el;
    mbedtls_pk_context pk;
    mbedtls_entropy_context ent;
    mbedtls_ctr_drbg_context drbg;
    const char *pers = "ra-test";
    int rc;
    if(ra_pubkey_b64(r, pb, sizeof pb) <= 0) return -1;
    dl = b64_decode(pb, der, sizeof der);
    if(dl == (size_t)-1) return -1;
    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&ent);
    mbedtls_ctr_drbg_init(&drbg);
    if(mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent,
                             (const unsigned char *)pers, strlen(pers)) != 0) return -1;
    if(mbedtls_pk_parse_public_key(&pk, der, dl) != 0) return -1;
    mbedtls_rsa_set_padding(mbedtls_pk_rsa(pk), MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
    rc = mbedtls_rsa_pkcs1_encrypt(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random, &drbg,
                                   strlen(plain), (const unsigned char *)plain, enc);
    el = 256;
    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&ent);
    if(rc != 0) return -1;
    b64_encode(enc, el, out);
    return 0;
}

static int feed(ra *r, const char *json, ra_event *ev){
    return ra_feed(r, json, strlen(json), ev);
}

TEST(test_ra_keygen_and_fingerprint){
    ra *r = ra_new();
    ASSERT_TRUE(r != NULL);
    char pb[1024];
    ASSERT_TRUE(ra_pubkey_b64(r, pb, sizeof pb) > 0);
    unsigned char der[1024];
    size_t dl = b64_decode(pb, der, sizeof der);
    ASSERT_TRUE(dl != (size_t)-1 && dl > 32);
    ASSERT_EQ_INT(der[0], 0x30);
    unsigned char dig[32];
    char expect[64];
    mbedtls_sha256(der, dl, dig, 0);
    snprintf(expect, sizeof expect, "%s", b64url(dig, 32));
    char fp[64];
    ASSERT_TRUE(ra_fingerprint(r, fp, sizeof fp) > 0);
    ASSERT_EQ_STR(fp, expect);
    ra_free(r);
}

TEST(test_ra_hello_nonce_flow){
    ra *r = ra_new();
    ra_event ev;
    ASSERT_EQ_INT(feed(r, "{\"op\":\"hello\",\"timeout_ms\":142637,\"heartbeat_interval\":41250}", &ev), RA_HELLO);
    ASSERT_EQ_INT(ra_hello_interval(r), 41250);
    char enc[1024];
    ASSERT_EQ_INT(encrypt_with_pub(r, "nonce-123", enc, sizeof enc), 0);
    char frame[1200];
    snprintf(frame, sizeof frame, "{\"op\":\"nonce_proof\",\"encrypted_nonce\":\"%s\"}", enc);
    ASSERT_EQ_INT(feed(r, frame, &ev), RA_NONCE);
    char out[512];
    ASSERT_TRUE(ra_respond_nonce(r, out, sizeof out) > 0);
    ASSERT_TRUE(strstr(out, "\"op\":\"nonce_proof\"") != NULL);
    const char *nonce = b64url((const unsigned char *)"nonce-123", 9);
    ASSERT_TRUE(strstr(out, nonce) != NULL);
    ra_free(r);
}

TEST(test_ra_qr_and_fingerprint_check){
    ra *r = ra_new();
    ra_event ev;
    char fp[64], frame[256];
    ra_fingerprint(r, fp, sizeof fp);
    snprintf(frame, sizeof frame, "{\"op\":\"pending_remote_init\",\"fingerprint\":\"%s\"}", fp);
    ASSERT_EQ_INT(feed(r, frame, &ev), RA_QR_READY);
    char expect[300];
    snprintf(expect, sizeof expect, "https://discord.com/ra/%s", fp);
    ASSERT_EQ_STR(ev.qr_payload, expect);
    ASSERT_EQ_INT(feed(r, "{\"op\":\"pending_remote_init\",\"fingerprint\":\"bogus\"}", &ev), RA_AUTH_FAILED);
    ra_free(r);
}

TEST(test_ra_ticket_and_login){
    ra *r = ra_new();
    ra_event ev;
    char enc[1024], frame[1200];
    ASSERT_EQ_INT(encrypt_with_pub(r, "123456789:0:avatarhash:zoe", enc, sizeof enc), 0);
    snprintf(frame, sizeof frame, "{\"op\":\"pending_ticket\",\"encrypted_user_payload\":\"%s\"}", enc);
    ASSERT_EQ_INT(feed(r, frame, &ev), RA_USER_PENDING);
    ASSERT_EQ_STR(ev.username, "zoe");
    ASSERT_EQ_STR(ev.user_id, "123456789");
    ASSERT_EQ_INT(feed(r, "{\"op\":\"pending_login\",\"ticket\":\"tk-42\"}", &ev), RA_TICKET);
    ASSERT_EQ_STR(ev.ticket, "tk-42");
    ra_free(r);
}

TEST(test_ra_decrypt_token){
    ra *r = ra_new();
    char enc[1024], token[256];
    ASSERT_EQ_INT(encrypt_with_pub(r, "mfa.abc123", enc, sizeof enc), 0);
    ASSERT_TRUE(ra_decrypt_token(r, enc, token, sizeof token) > 0);
    ASSERT_EQ_STR(token, "mfa.abc123");
    ra_free(r);
}

TEST(test_ra_timeout_regen){
    ra *r = ra_new();
    ra_event ev;
    char fp1[64], fp2[64];
    ra_fingerprint(r, fp1, sizeof fp1);
    ASSERT_EQ_INT(ra_on_close(r, 4003, &ev), RA_CANCELLED);
    ra_reset(r);
    ra_fingerprint(r, fp2, sizeof fp2);
    ASSERT_TRUE(strcmp(fp1, fp2) != 0);
    char frame[256];
    snprintf(frame, sizeof frame, "{\"op\":\"pending_remote_init\",\"fingerprint\":\"%s\"}", fp2);
    ASSERT_EQ_INT(feed(r, frame, &ev), RA_QR_READY);
    ra_free(r);
}

TEST(test_ra_bad_frames){
    ra *r = ra_new();
    ra_event ev;
    ASSERT_TRUE(feed(r, "{", &ev) < 0);
    ASSERT_TRUE(feed(r, "{\"op\":7}", &ev) < 0);
    ASSERT_TRUE(feed(r, "{\"op\":\"nonce_proof\",\"encrypted_nonce\":\"!!!!\"}", &ev) < 0);
    ra_free(r);
}

TEST(test_ra_qr_modules){
    static uint8_t mods[128 * 128];
    static uint8_t mods2[128 * 128];
    int n = 0, n2 = 0, i;
    ASSERT_EQ_INT(ra_qr_modules("https://discord.com/ra/AbCdEf123", mods, sizeof mods, &n), 1);
    ASSERT_TRUE(n >= 21);
    ASSERT_TRUE(n <= 128);
    ASSERT_EQ_INT(mods[0], 1);
    ASSERT_EQ_INT(mods[n - 1], 1);
    ASSERT_EQ_INT(mods[n * (n - 1)], 1);
    ASSERT_EQ_INT(mods[1 * n + 1], 0);
    ASSERT_EQ_INT(mods[2 * n + 2], 1);
    ASSERT_EQ_INT(mods[7], 0);
    ASSERT_EQ_INT(ra_qr_modules("https://discord.com/ra/AbCdEf123", mods2, sizeof mods2, &n2), 1);
    ASSERT_EQ_INT(n, n2);
    for(i = 0; i < n * n; i++)
        if(mods[i] != mods2[i]){ ASSERT_TRUE(0); }
    ASSERT_EQ_INT(ra_qr_modules("https://discord.com/ra/AbCdEf123", mods, 10, &n2), 0);
}

void run_remoteauth_tests(void){
    g_tests_run++; test_ra_keygen_and_fingerprint();
    g_tests_run++; test_ra_hello_nonce_flow();
    g_tests_run++; test_ra_qr_and_fingerprint_check();
    g_tests_run++; test_ra_ticket_and_login();
    g_tests_run++; test_ra_decrypt_token();
    g_tests_run++; test_ra_timeout_regen();
    g_tests_run++; test_ra_bad_frames();
    g_tests_run++; test_ra_qr_modules();
}
