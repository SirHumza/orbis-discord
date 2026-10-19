#ifndef ORBIS_MSGJSON_H
#define ORBIS_MSGJSON_H
#include "store.h"
#include "../core/jsonlite.h"

int msg_from_json(message *m, jl_val_t *o);
int emoji_from_json(emoji_item *e, jl_val_t *o);
void msg_resolve_mentions(store *st, message *m, uint64_t self_id);

#endif
