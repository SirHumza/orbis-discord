#ifndef ORBIS_IME_H
#define ORBIS_IME_H
#include <stddef.h>

int ime_init(void);
int ime_prompt(const char *title, const char *placeholder, char *out, size_t cap);
#endif
