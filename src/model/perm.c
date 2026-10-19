/* perm.c - base guild perms + channel overwrites, Discord order.
 * base = everyone role OR self roles; @everyone and role overwrites are
 * OR-ed together, then the member overwrite applies last. */
#include "perm.h"

uint64_t perm_effective(const channel *ch, const guild *gu, uint64_t self_id){
    uint64_t p = gu ? gu->my_perms : 0;
    uint64_t allow = 0, deny = 0;
    int i, j;
    if(!ch) return p;
    if(p & PERM_ADMINISTRATOR) return ~0ull;
    for(i = 0; i < ch->now; i++)
        if(ch->ow[i].type == 0 && gu && ch->ow[i].id == gu->id){
            allow |= ch->ow[i].allow;
            deny |= ch->ow[i].deny;
        }
    for(j = 0; gu && j < gu->my_nroles; j++)
        for(i = 0; i < ch->now; i++)
            if(ch->ow[i].type == 0 && ch->ow[i].id == gu->my_roles[j]){
                allow |= ch->ow[i].allow;
                deny |= ch->ow[i].deny;
            }
    p = (p & ~deny) | allow;
    for(i = 0; i < ch->now; i++)
        if(ch->ow[i].type == 1 && ch->ow[i].id == self_id)
            p = (p & ~ch->ow[i].deny) | ch->ow[i].allow;
    return p;
}

int perm_can(uint64_t perms, uint64_t bit){
    return (perms & bit) != 0;
}
