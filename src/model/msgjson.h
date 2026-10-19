#ifndef ORBIS_MSGJSON_H
#define ORBIS_MSGJSON_H
#include "store.h"
#include "../core/jsonlite.h"

int msg_from_json(message *m, jl_val_t *o);

#endif
