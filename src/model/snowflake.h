#ifndef ORBIS_SNOWFLAKE_H
#define ORBIS_SNOWFLAKE_H
#include <stdint.h>
#include <stddef.h>
#define DISCORD_EPOCH_MS 1420070400000ULL
uint64_t sn_parse(const char *s);
void sn_str(uint64_t id, char out[21]);
uint64_t sn_timestamp_ms(uint64_t id);
#endif
