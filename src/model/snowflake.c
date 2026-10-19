#include "snowflake.h"

uint64_t sn_parse(const char *s){
    uint64_t v = 0;
    if(!s) return 0;
    while(*s >= '0' && *s <= '9'){ v = v * 10 + (uint64_t)(*s - '0'); s++; }
    return v;
}

void sn_str(uint64_t id, char out[21]){
    char tmp[21];
    int i = 20;
    tmp[20] = 0;
    if(id == 0) tmp[--i] = '0';
    while(id > 0){ tmp[--i] = (char)('0' + (id % 10)); id /= 10; }
    size_t n = (size_t)(20 - i);
    for(size_t k = 0; k <= n; k++) out[k] = tmp[i + k];
}

uint64_t sn_timestamp_ms(uint64_t id){
    return (id >> 22) + DISCORD_EPOCH_MS;
}
