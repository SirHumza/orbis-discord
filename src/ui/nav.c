/* nav.c - list selection cursor. Pure: compiled by host tests via include. */
#include "nav.h"

void nav_init(nav *n){
    if(n) n->index = 0;
}

int nav_step(nav *n, input_event ev, int count){
    if(!n) return 0;
    if(count <= 0){ n->index = 0; return 0; }
    if(n->index < 0) n->index = 0;
    if(n->index >= count) n->index = count - 1;
    if(ev == IE_UP)   n->index = (n->index + count - 1) % count;
    if(ev == IE_DOWN) n->index = (n->index + 1) % count;
    return n->index;
}

int nav_index(const nav *n){
    return n ? n->index : 0;
}
