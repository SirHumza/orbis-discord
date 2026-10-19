#ifndef ORBIS_MULTIPART_H
#define ORBIS_MULTIPART_H
#include <stddef.h>

int multipart_build(const char *boundary, const char *payload,
                    const char *filename, const char *ctype,
                    const void *data, size_t len, char **out, size_t *outlen);

#endif
