#ifndef ORBIS_VOICE_H
#define ORBIS_VOICE_H
#include <stddef.h>
#include <stdint.h>

#define VOICE_MODE_XCHACHA 1
#define VOICE_MODE_XSALSA 2
#define VOICE_MODE_AES256 4

typedef struct {
    uint32_t ssrc;
    char ip[64];
    int port;
    int modes;
} voice_ready;

int voice_state_json(uint64_t guild, uint64_t chan, char *out, size_t cap);
int voice_identify_json(uint64_t guild, uint64_t user, const char *session,
                        const char *token, char *out, size_t cap);
int voice_select_json(const char *ip, int port, const char *mode,
                      char *out, size_t cap);
int voice_speaking_json(uint32_t ssrc, int speaking, char *out, size_t cap);
int voice_heartbeat_json(uint64_t nonce, char *out, size_t cap);
int voice_ready_parse(const char *json, voice_ready *out);
int voice_session_parse(const char *json, char *mode, size_t mode_cap,
                        unsigned char *key, size_t key_cap, size_t *key_len);
const char *voice_pick_mode(int modes);
int voice_discovery_packet(uint32_t ssrc, unsigned char *out, size_t cap);
int voice_discovery_parse(const unsigned char *buf, size_t len, char *ip,
                          size_t ip_cap, int *port);
int voice_endpoint_split(const char *endpoint, char *host, size_t host_cap,
                         int *port);

#endif
