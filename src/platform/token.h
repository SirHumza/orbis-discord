#ifndef ORBIS_TOKEN_H
#define ORBIS_TOKEN_H
#include <stddef.h>

#define ORBIS_TOKEN_PATH "/data/orbis-discord/token"

int token_load(char *out, size_t cap);
int token_save(const char *token);
void token_clear(void);
#endif
