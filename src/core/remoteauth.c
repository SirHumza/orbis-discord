#include "remoteauth.h"
#include "b64.h"
#include "jsonlite.h"
#include "../../third_party/qrcodegen.h"
#include "../../third_party/qrcodegen.c"
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct ra {
    mbedtls_pk_context pk;
    mbedtls_entropy_context ent;
    mbedtls_ctr_drbg_context drbg;
    unsigned char spki[1024];
    size_t spki_len;
    char fingerprint[64];
    int interval_ms;
    unsigned char nonce[512];
    size_t nonce_len;
    char nonce_b64[768];
};

static void b64url(const unsigned char *in, size_t n, char *out, size_t cap){
    char enc[1024];
    size_t el, o = 0;
    if(cap == 0) return;
    if(n > 700) n = 700;
    el = b64_encode(in, n, enc);
    for(size_t i = 0; i < el && o + 1 < cap; i++){
        if(enc[i] == '=') break;
        out[o++] = enc[i] == '+' ? '-' : enc[i] == '/' ? '_' : enc[i];
    }
    out[o] = 0;
}

static int make_key(ra *r){
    unsigned char der[1024];
    unsigned char dig[32];
    int len;
    if(mbedtls_pk_setup(&r->pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0) return -1;
    if(mbedtls_rsa_gen_key(mbedtls_pk_rsa(r->pk), mbedtls_ctr_drbg_random, &r->drbg,
                           2048, 65537) != 0) return -2;
    mbedtls_rsa_set_padding(mbedtls_pk_rsa(r->pk), MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
    len = mbedtls_pk_write_pubkey_der(&r->pk, der, sizeof der);
    if(len <= 0) return -3;
    r->spki_len = (size_t)len;
    memcpy(r->spki, der + sizeof der - (size_t)len, (size_t)len);
    mbedtls_sha256(r->spki, r->spki_len, dig, 0);
    b64url(dig, sizeof dig, r->fingerprint, sizeof r->fingerprint);
    return 0;
}

ra *ra_new(void){
    ra *r = calloc(1, sizeof *r);
    if(!r) return NULL;
    mbedtls_pk_init(&r->pk);
    mbedtls_entropy_init(&r->ent);
    mbedtls_ctr_drbg_init(&r->drbg);
    if(mbedtls_ctr_drbg_seed(&r->drbg, mbedtls_entropy_func, &r->ent,
                             (const unsigned char *)"orbis-discord-ra", 16) != 0){
        ra_free(r);
        return NULL;
    }
    if(make_key(r) != 0){ ra_free(r); return NULL; }
    r->interval_ms = 41250;
    return r;
}

void ra_free(ra *r){
    if(!r) return;
    mbedtls_pk_free(&r->pk);
    mbedtls_ctr_drbg_free(&r->drbg);
    mbedtls_entropy_free(&r->ent);
    free(r);
}

void ra_reset(ra *r){
    if(!r) return;
    mbedtls_pk_free(&r->pk);
    mbedtls_pk_init(&r->pk);
    r->nonce_len = 0;
    r->nonce_b64[0] = 0;
    r->fingerprint[0] = 0;
    make_key(r);
}

int ra_pubkey_b64(ra *r, char *out, size_t cap){
    if(!r || !out) return 0;
    size_t need = 4 * ((r->spki_len + 2) / 3) + 1;
    if(cap < need) return 0;
    b64_encode(r->spki, r->spki_len, out);
    return (int)strlen(out);
}

int ra_fingerprint(ra *r, char *out, size_t cap){
    if(!r || !out || !r->fingerprint[0]) return 0;
    snprintf(out, cap, "%s", r->fingerprint);
    return (int)strlen(out);
}

static int decrypt(ra *r, const char *b64, unsigned char *out, size_t cap, size_t *olen){
    unsigned char ct[1024];
    size_t ctlen;
    if(!r || !b64 || !out || !olen) return -1;
    ctlen = b64_decode(b64, ct, sizeof ct);
    if(ctlen == (size_t)-1 || ctlen == 0) return -2;
    { int rc = mbedtls_rsa_pkcs1_decrypt(mbedtls_pk_rsa(r->pk), mbedtls_ctr_drbg_random, &r->drbg, olen, ct, out, cap); if(rc != 0) return -3; }
    return 0;
}

static char *getstr(jl_val_t *v, const char *k){
    jl_val_t *x = v->type == JL_OBJECT ? jl_obj_get(v, k) : NULL;
    return x && x->type == JL_STRING ? x->str : NULL;
}

int ra_feed(ra *r, const char *json, size_t len, ra_event *out){
    jl_val_t *v;
    char *op;
    if(!r || !json || !out) return -1;
    memset(out, 0, sizeof *out);
    out->kind = RA_NONE;
    v = jl_parse(json, len);
    if(!v) return -2;
    op = getstr(v, "op");
    if(!op){ jl_free(v); return -3; }
    if(!strcmp(op, "hello")){
        jl_val_t *hb = jl_obj_get(v, "heartbeat_interval");
        if(hb && hb->type == JL_NUMBER) r->interval_ms = (int)hb->num;
        out->kind = RA_HELLO;
    } else if(!strcmp(op, "nonce_proof")){
        char *enc = getstr(v, "encrypted_nonce");
        int rc;
        r->nonce_len = 0;
        if(!enc){ jl_free(v); return -4; }
        rc = decrypt(r, enc, r->nonce, sizeof r->nonce, &r->nonce_len);
        if(rc != 0){ jl_free(v); return -4; }
        b64url(r->nonce, r->nonce_len, r->nonce_b64, sizeof r->nonce_b64);
        out->kind = RA_NONCE;
    } else if(!strcmp(op, "pending_remote_init")){
        char *fp = getstr(v, "fingerprint");
        if(!fp || strcmp(fp, r->fingerprint) != 0) out->kind = RA_AUTH_FAILED;
        else {
            snprintf(out->qr_payload, sizeof out->qr_payload,
                     "https://discord.com/ra/%s", fp);
            out->kind = RA_QR_READY;
        }
    } else if(!strcmp(op, "pending_ticket")){
        char *enc = getstr(v, "encrypted_user_payload");
        unsigned char plain[512];
        size_t plen = 0;
        if(!enc || decrypt(r, enc, plain, sizeof plain - 1, &plen) != 0){
            jl_free(v); return -5;
        }
        plain[plen] = 0;
        {
            char *p = (char *)plain;
            char *parts[4] = {0};
            int n = 0;
            parts[n++] = p;
            for(char *q = p; *q && n < 4; q++)
                if(*q == ':'){ *q = 0; parts[n++] = q + 1; }
            if(n < 4){ jl_free(v); return -6; }
            snprintf(out->user_id, sizeof out->user_id, "%s", parts[0]);
            snprintf(out->username, sizeof out->username, "%s", parts[3]);
        }
        out->kind = RA_USER_PENDING;
    } else if(!strcmp(op, "pending_login")){
        char *t = getstr(v, "ticket");
        if(!t){ jl_free(v); return -7; }
        snprintf(out->ticket, sizeof out->ticket, "%s", t);
        out->kind = RA_TICKET;
    } else if(!strcmp(op, "cancel")){
        out->kind = RA_CANCELLED;
    } else if(!strcmp(op, "heartbeat_ack")){
        out->kind = RA_NONE;
    } else {
        jl_free(v);
        return -8;
    }
    jl_free(v);
    return out->kind;
}

int ra_respond_nonce(ra *r, char *json_out, size_t cap){
    int n;
    if(!r || !json_out || !r->nonce_b64[0]) return 0;
    n = snprintf(json_out, cap, "{\"op\":\"nonce_proof\",\"nonce\":\"%s\"}", r->nonce_b64);
    return n > 0 && (size_t)n < cap ? n : 0;
}

int ra_on_close(ra *r, int code, ra_event *out){
    if(!r || !out) return -1;
    memset(out, 0, sizeof *out);
    if(code == 4003 || code == 1000) out->kind = RA_CANCELLED;
    else if(code == 4000 || code == 4001 || code == 4002) out->kind = RA_AUTH_FAILED;
    else out->kind = RA_AUTH_FAILED;
    return out->kind;
}

int ra_decrypt_token(ra *r, const char *enc_b64, char *out, size_t cap){
    unsigned char plain[512];
    size_t plen = 0;
    if(!r || !enc_b64 || !out || cap == 0) return 0;
    if(decrypt(r, enc_b64, plain, sizeof plain - 1, &plen) != 0) return 0;
    if(plen >= cap) return 0;
    memcpy(out, plain, plen);
    out[plen] = 0;
    return (int)plen;
}

int ra_hello_interval(ra *r){ return r ? r->interval_ms : 41250; }

int ra_heartbeat(char *out, size_t cap){
    int n;
    if(!out) return 0;
    n = snprintf(out, cap, "{\"op\":\"heartbeat\"}");
    return n > 0 && (size_t)n < cap ? n : 0;
}

int ra_qr_modules(const char *payload, uint8_t *out, size_t cap, int *size_out){
    static uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    static uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    int size, x, y;
    if(!payload || !out || !size_out) return 0;
    if(!qrcodegen_encodeText(payload, temp, qr, qrcodegen_Ecc_MEDIUM,
                             qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                             qrcodegen_Mask_AUTO, true))
        return 0;
    size = qrcodegen_getSize(qr);
    if(cap < (size_t)size * (size_t)size) return 0;
    for(y = 0; y < size; y++)
        for(x = 0; x < size; x++)
            out[y * size + x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
    *size_out = size;
    return 1;
}
