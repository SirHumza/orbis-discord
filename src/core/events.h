#ifndef ORBIS_EVENTS_H
#define ORBIS_EVENTS_H
#include "gateway.h"
#include "jsonlite.h"

int events_dispatch(gateway *, const char *t, jl_val_t *d);
#endif
