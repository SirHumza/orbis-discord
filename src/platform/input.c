/* input.c - DualShock 4 -> input_event mapping with hold-to-repeat.
 * Only compiled for PS4 (src/platform is not in the host test build). */
#include "input.h"
#include "log.h"
#include "clock.h"
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <stdint.h>

/* The SDK models the sticks as 0..255 byte pairs, center 128. */
#define STICK_DEADZONE 96
#define REPEAT_DELAY_MS 380
#define REPEAT_RATE_MS 90

static const input_event EV[14] = {
    IE_UP, IE_RIGHT, IE_DOWN, IE_LEFT, IE_SELECT, IE_BACK,
    IE_PGUP, IE_PGDN, IE_BOTTOM, IE_VIEW, IE_MENU, IE_PINS, IE_SEARCH,
    IE_FRIENDS
};

static int s_pad = -1;
static unsigned s_prev;
static uint64_t s_since[14];

static uint64_t mono_ms(void){
    return (uint64_t)orbis_mono_s() * 1000u;
}

static unsigned pad_mask(const OrbisPadData *d){
    unsigned b = d->buttons, m = 0;
    int lx = (int)d->leftStick.x - 128;
    int ly = (int)d->leftStick.y - 128;
    if((b & ORBIS_PAD_BUTTON_UP)    || ly < -STICK_DEADZONE) m |= 1u << 0;
    if((b & ORBIS_PAD_BUTTON_RIGHT) || lx >  STICK_DEADZONE) m |= 1u << 1;
    if((b & ORBIS_PAD_BUTTON_DOWN)  || ly >  STICK_DEADZONE) m |= 1u << 2;
    if((b & ORBIS_PAD_BUTTON_LEFT)  || lx < -STICK_DEADZONE) m |= 1u << 3;
    if(b & ORBIS_PAD_BUTTON_CROSS)  m |= 1u << 4;
    if(b & ORBIS_PAD_BUTTON_CIRCLE) m |= 1u << 5;
    if(b & ORBIS_PAD_BUTTON_L1)     m |= 1u << 6;
    if(b & ORBIS_PAD_BUTTON_R1)     m |= 1u << 7;
    if(b & ORBIS_PAD_BUTTON_R3)     m |= 1u << 8;
    if(b & ORBIS_PAD_BUTTON_TRIANGLE) m |= 1u << 9;
    if(b & ORBIS_PAD_BUTTON_SQUARE)   m |= 1u << 10;
    if(b & ORBIS_PAD_BUTTON_L2)       m |= 1u << 11;
    if(b & ORBIS_PAD_BUTTON_R2)       m |= 1u << 12;
    if(b & ORBIS_PAD_BUTTON_OPTIONS)  m |= 1u << 13;
    return m;
}

int input_init(void){
    int32_t uid = 0;
    if(s_pad >= 0) return 1;
    if(sceUserServiceGetForegroundUser(&uid) < 0 || uid <= 0) uid = 1;
    s_pad = scePadOpen(uid, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if(s_pad < 0){
        log_msg("input: pad open failed (0x%x)", s_pad);
        return 0;
    }
    log_msg("input: pad ready");
    return 1;
}

int input_poll(input_event *out, int max){
    OrbisPadData d;
    uint64_t now;
    unsigned m, pressed;
    int i, n = 0;
    if(!out || max <= 0 || s_pad < 0) return 0;
    if(scePadReadState(s_pad, &d) < 0) return 0;
    now = mono_ms();
    m = pad_mask(&d);
    pressed = m & ~s_prev;
    for(i = 0; i < 14 && n < max; i++){
        unsigned bit = 1u << i;
        if(!(m & bit)){ s_since[i] = 0; continue; }
        if(pressed & bit){
            out[n++] = EV[i];
            s_since[i] = now;
        } else if(i < 4 || i == 6 || i == 7){
            if(s_since[i] == 0) s_since[i] = now;
            else if(now - s_since[i] >= REPEAT_DELAY_MS){
                out[n++] = EV[i];
                s_since[i] = now - (REPEAT_DELAY_MS - REPEAT_RATE_MS);
            }
        }
    }
    s_prev = m;
    return n;
}
