/* tenor_net.h - blocking Tenor API GET over TLS (PS4-only). */
#ifndef ORBIS_TENOR_NET_H
#define ORBIS_TENOR_NET_H
#include <stddef.h>

/* path is the API path incl. query, e.g. "/v2/search?q=cats&key=...".
 * On success *out is malloc'd (caller frees) and *len is its length. */
int tenor_net_get(const char *path, char **out, size_t *len);

#endif
