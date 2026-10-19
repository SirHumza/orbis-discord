#include "tests.h"
#include "../src/ui/theme.h"

TEST(test_ui_rail){ ASSERT_EQ_INT(UI_RAIL, 0xFF1E1F22u); }
TEST(test_ui_channel_bg){ ASSERT_EQ_INT(UI_CHANNEL_BG, 0xFF2B2D31u); }
TEST(test_ui_chat_bg){ ASSERT_EQ_INT(UI_CHAT_BG, 0xFF313338u); }
TEST(test_ui_blurple){ ASSERT_EQ_INT(UI_BLURPLE, 0xFF5865F2u); }
TEST(test_ui_text){ ASSERT_EQ_INT(UI_TEXT, 0xFFDBDEE1u); }
TEST(test_ui_text_dim){ ASSERT_EQ_INT(UI_TEXT_DIM, 0xFF949BA4u); }
TEST(test_ui_text_link){ ASSERT_EQ_INT(UI_TEXT_LINK, 0xFF00A8FCu); }
TEST(test_ui_danger){ ASSERT_EQ_INT(UI_DANGER, 0xFFF23F43u); }
TEST(test_ui_online){ ASSERT_EQ_INT(UI_ONLINE, 0xFF23A55Au); }
TEST(test_ui_idle){ ASSERT_EQ_INT(UI_IDLE, 0xFFF0B232u); }
TEST(test_ui_dnd){ ASSERT_EQ_INT(UI_DND, 0xFFF23F43u); }
TEST(test_ui_offline){ ASSERT_EQ_INT(UI_OFFLINE, 0xFF80848Eu); }
TEST(test_ui_header_bg){ ASSERT_EQ_INT(UI_HEADER_BG, 0xFF313338u); }
TEST(test_ui_sep){ ASSERT_EQ_INT(UI_SEP, 0xFF3F4147u); }
TEST(test_ui_bg_modal){ ASSERT_EQ_INT(UI_BG_MODAL, 0xFF2B2D31u); }

TEST(test_ui_layout){
    ASSERT_EQ_INT(UI_RAIL_W, 72);
    ASSERT_EQ_INT(UI_CHANLIST_W, 240);
    ASSERT_EQ_INT(UI_MEMBERLIST_W, 240);
    ASSERT_EQ_INT(UI_FS_BASE, 16);
    ASSERT_EQ_INT(UI_FS_SMALL, 14);
    ASSERT_EQ_INT(UI_FS_TITLE, 20);
}

void run_ui_tests(void){
    g_tests_run++; test_ui_rail();
    g_tests_run++; test_ui_channel_bg();
    g_tests_run++; test_ui_chat_bg();
    g_tests_run++; test_ui_blurple();
    g_tests_run++; test_ui_text();
    g_tests_run++; test_ui_text_dim();
    g_tests_run++; test_ui_text_link();
    g_tests_run++; test_ui_danger();
    g_tests_run++; test_ui_online();
    g_tests_run++; test_ui_idle();
    g_tests_run++; test_ui_dnd();
    g_tests_run++; test_ui_offline();
    g_tests_run++; test_ui_header_bg();
    g_tests_run++; test_ui_sep();
    g_tests_run++; test_ui_bg_modal();
    g_tests_run++; test_ui_layout();
}
