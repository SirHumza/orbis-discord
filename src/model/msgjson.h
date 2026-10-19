#ifndef ORBIS_MSGJSON_H
#define ORBIS_MSGJSON_H
#include "store.h"
#include "../core/jsonlite.h"

int msg_from_json(message *m, jl_val_t *o);
int emoji_from_json(emoji_item *e, jl_val_t *o);
int recipients_from_json(jl_val_t *arr, recip_item *out, int cap);
int overwrites_from_json(jl_val_t *arr, channel *ch);
void msg_resolve_mentions(store *st, message *m, uint64_t self_id);

#endif
