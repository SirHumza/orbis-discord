#include "ime.h"
#include "log.h"
#include <orbis/CommonDialog.h>
#include <orbis/ImeDialog.h>
#include <orbis/Pad.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <string.h>
#include <unistd.h>

#define IME_MAX 1024

static int s_ime_ready;
static int s_ime_running;

int ime_init(void){
    if(s_ime_ready) return 1;
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG);
    sceCommonDialogInitialize();
    sceUserServiceInitialize(NULL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
    scePadInit();
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_BACKEND);
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);
    s_ime_ready = 1;
    log_msg("ime: subsystems ready");
    return 1;
}

int ime_prompt(const char *title, const char *placeholder, char *out, size_t cap){
    static wchar_t wbuf[IME_MAX + 1];
    static wchar_t wtitle[64];
    static wchar_t wplace[64];
    OrbisImeDialogSetting st;
    int32_t uid = 0;
    size_t i;
    if(!out || cap == 0 || cap > IME_MAX) return 0;
    if(!s_ime_ready && !ime_init()) return 0;
    for(i = 0; i < cap - 1 && out[i]; i++) wbuf[i] = (wchar_t)(unsigned char)out[i];
    wbuf[i] = 0;
    for(i = 0; i < 63 && title && title[i]; i++) wtitle[i] = (wchar_t)(unsigned char)title[i];
    wtitle[i] = 0;
    for(i = 0; i < 63 && placeholder && placeholder[i]; i++)
        wplace[i] = (wchar_t)(unsigned char)placeholder[i];
    wplace[i] = 0;
    if(sceUserServiceGetForegroundUser(&uid) < 0 || uid <= 0) uid = 1;
    memset(&st, 0, sizeof st);
    st.userId = (uint32_t)uid;
    st.type = 0;
    st.supportedLanguages = 0;
    st.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
    st.inputMethod = 0;
    st.filter = 0;
    st.option = 0;
    st.maxTextLength = (uint32_t)(cap - 1);
    st.inputTextBuffer = wbuf;
    st.posx = 960;
    st.posy = 540;
    st.horizontalAlignment = ORBIS_H_CENTER;
    st.verticalAlignment = ORBIS_V_CENTER;
    st.placeholder = wplace;
    st.title = wtitle;
    if(s_ime_running){ sceImeDialogTerm(); s_ime_running = 0; }
    if(sceImeDialogInit(&st, NULL) < 0){ log_msg("ime: init fail"); return 0; }
    s_ime_running = 1;
    {
        int spins = 0;
        OrbisDialogStatus ds;
        while(s_ime_running){
            ds = sceImeDialogGetStatus();
            if(ds == ORBIS_DIALOG_STATUS_STOPPED){
                OrbisDialogResult res;
                memset(&res, 0, sizeof res);
                sceImeDialogGetResult(&res);
                sceImeDialogTerm();
                s_ime_running = 0;
                if(res.endstatus != ORBIS_DIALOG_OK) return 0;
                break;
            }
            if(ds == ORBIS_DIALOG_STATUS_NONE && ++spins > 2500){
                log_msg("ime: timeout");
                sceImeDialogTerm();
                s_ime_running = 0;
                return 0;
            }
            if(ds != ORBIS_DIALOG_STATUS_NONE) spins = 0;
            usleep(20000);
        }
    }
    for(i = 0; i < cap - 1 && wbuf[i]; i++)
        out[i] = (wbuf[i] < 128 && wbuf[i] >= 32) ? (char)wbuf[i] : '?';
    out[i] = 0;
    return 1;
}
