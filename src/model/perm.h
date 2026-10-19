/* perm.h - Discord permission bit math (pure, host-tested). */
#ifndef ORBIS_PERM_H
#define ORBIS_PERM_H
#include <stdint.h>
#include "store.h"

#define PERM_CREATE_INVITE (1ull << 0)
#define PERM_ADMINISTRATOR (1ull << 3)
#define PERM_VIEW_CHANNEL  (1ull << 10)
#define PERM_SEND_MESSAGES (1ull << 11)
#define PERM_ATTACH_FILES  (1ull << 15)
#define PERM_CONNECT_VOICE (1ull << 20)
#define PERM_SPEAK_VOICE   (1ull << 21)

uint64_t perm_effective(const channel *ch, const guild *gu, uint64_t self_id);
int perm_can(uint64_t perms, uint64_t bit);

#endif
