#ifndef ORBIS_REMOTEAUTH_H
#define ORBIS_REMOTEAUTH_H
#include <stdint.h>
#include <stddef.h>

#define RA_NONE 0
#define RA_HELLO 1
#define RA_NONCE 2
#define RA_QR_READY 3
#define RA_USER_PENDING 4
#define RA_TICKET 5
#define RA_CANCELLED 6
#define RA_AUTH_FAILED 7

typedef struct {
    int kind;
    char qr_payload[256];
    char username[64];
    char user_id[64];
    char ticket[256];
} ra_event;

typedef struct ra ra;

ra *ra_new(void);
void ra_free(ra *);
void ra_reset(ra *);
int ra_pubkey_b64(ra *, char *out, size_t cap);
int ra_fingerprint(ra *, char *out, size_t cap);
int ra_feed(ra *, const char *json, size_t len, ra_event *out);
int ra_respond_nonce(ra *, char *json_out, size_t cap);
int ra_on_close(ra *, int code, ra_event *out);
int ra_decrypt_token(ra *, const char *enc_b64, char *out, size_t cap);
int ra_qr_modules(const char *payload, uint8_t *out, size_t cap, int *size_out);
int ra_hello_interval(ra *);
int ra_heartbeat(char *out, size_t cap);
#endif
