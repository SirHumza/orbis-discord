#ifndef ORBIS_IME_H
#define ORBIS_IME_H
#include <stddef.h>
#include <stdint.h>

int ime_init(void);
int ime_prompt(const char *title, const char *placeholder, char *out, size_t cap);
/* PS4 wchar_t is 16-bit (verified with the toolchain): dialog text is UTF-16.
 * Pure helper so host tests can pin surrogate/emoji handling. */
size_t ime_utf16_to_utf8(const uint16_t *in, size_t in_len, char *out, size_t cap);
#endif
