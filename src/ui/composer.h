/* composer.h - message send payload.
 * Pure: the IME dialog and drawing stay in screen_main.c, so host tests can
 * pin the JSON escaping and UTF-8 byte accounting without SDL/orbis. */
#ifndef ORBIS_COMPOSER_H
#define ORBIS_COMPOSER_H
#include <stdint.h>
#include <stddef.h>

/* Builds {"content":"...","nonce":"..."} with JSON escapes; UTF-8 bytes pass
 * through untouched. Returns bytes written (no NUL) or -1 if out is too small. */
int composer_payload(const char *content, uint64_t nonce, char *out, size_t cap);

#endif
