/* nav.h - list selection cursor (pure logic, host-tested via test_nav). */
#ifndef UI_NAV_H
#define UI_NAV_H
#include "../platform/input.h"

typedef struct {
    int index;
} nav;

void nav_init(nav *n);
int nav_step(nav *n, input_event ev, int count);
int nav_index(const nav *n);

#endif
