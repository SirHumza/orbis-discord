#include "tests.h"
#include "../src/model/perm.h"
#include "../src/model/store.h"

static void add_ow(channel *ch, uint64_t id, int type, uint64_t allow,
                   uint64_t deny){
    overwrite *o = &ch->ow[ch->now++];
    o->id = id;
    o->type = type;
    o->allow = allow;
    o->deny = deny;
}

TEST(test_perm_base){
    guild gu;
    channel ch;
    memset(&gu, 0, sizeof gu);
    memset(&ch, 0, sizeof ch);
    gu.id = 10;
    gu.my_perms = PERM_VIEW_CHANNEL | PERM_SEND_MESSAGES;
    ch.guild = 10;
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_VIEW_CHANNEL), 1);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_SEND_MESSAGES), 1);
    ASSERT_EQ_INT(perm_can(perm_effective(NULL, &gu, 77), PERM_VIEW_CHANNEL), 1);
    ASSERT_EQ_INT(perm_effective(&ch, NULL, 77), 0);
}

TEST(test_perm_everyone_deny){
    guild gu;
    channel ch;
    memset(&gu, 0, sizeof gu);
    memset(&ch, 0, sizeof ch);
    gu.id = 10;
    gu.my_perms = PERM_VIEW_CHANNEL | PERM_SEND_MESSAGES;
    add_ow(&ch, 10, 0, 0, PERM_VIEW_CHANNEL);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_VIEW_CHANNEL), 0);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_SEND_MESSAGES), 1);
}

TEST(test_perm_role_allow_and_member_deny){
    guild gu;
    channel ch;
    memset(&gu, 0, sizeof gu);
    memset(&ch, 0, sizeof ch);
    gu.id = 10;
    gu.my_perms = PERM_VIEW_CHANNEL;
    gu.my_roles[0] = 42;
    gu.my_nroles = 1;
    add_ow(&ch, 10, 0, 0, PERM_VIEW_CHANNEL | PERM_SEND_MESSAGES);
    add_ow(&ch, 42, 0, PERM_VIEW_CHANNEL | PERM_SEND_MESSAGES, 0);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_VIEW_CHANNEL), 1);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_SEND_MESSAGES), 1);
    add_ow(&ch, 77, 1, 0, PERM_SEND_MESSAGES);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_VIEW_CHANNEL), 1);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_SEND_MESSAGES), 0);
}

TEST(test_perm_member_allow_beats_role_deny){
    guild gu;
    channel ch;
    memset(&gu, 0, sizeof gu);
    memset(&ch, 0, sizeof ch);
    gu.id = 10;
    gu.my_roles[0] = 42;
    gu.my_nroles = 1;
    add_ow(&ch, 42, 0, 0, PERM_VIEW_CHANNEL);
    add_ow(&ch, 77, 1, PERM_VIEW_CHANNEL, 0);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_VIEW_CHANNEL), 1);
}

TEST(test_perm_admin_bypass){
    guild gu;
    channel ch;
    memset(&gu, 0, sizeof gu);
    memset(&ch, 0, sizeof ch);
    gu.id = 10;
    gu.my_perms = PERM_ADMINISTRATOR;
    add_ow(&ch, 10, 0, 0, PERM_VIEW_CHANNEL | PERM_SEND_MESSAGES);
    ASSERT_EQ_INT(perm_effective(&ch, &gu, 77), ~0ull);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_SEND_MESSAGES), 1);
}

TEST(test_perm_voice){
    guild gu;
    channel ch;
    memset(&gu, 0, sizeof gu);
    memset(&ch, 0, sizeof ch);
    gu.id = 10;
    gu.my_perms = PERM_VIEW_CHANNEL | PERM_CONNECT_VOICE;
    ch.guild = 10;
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_CONNECT_VOICE), 1);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_SPEAK_VOICE), 0);
    add_ow(&ch, 10, 0, 0, PERM_CONNECT_VOICE);
    ASSERT_EQ_INT(perm_can(perm_effective(&ch, &gu, 77), PERM_CONNECT_VOICE), 0);
}

void run_perm_tests(void){
    g_tests_run++; test_perm_base();
    g_tests_run++; test_perm_everyone_deny();
    g_tests_run++; test_perm_role_allow_and_member_deny();
    g_tests_run++; test_perm_member_allow_beats_role_deny();
    g_tests_run++; test_perm_admin_bypass();
    g_tests_run++; test_perm_voice();
}

