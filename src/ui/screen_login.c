/* screen_login.c - QR remote-auth login screen (Discord remote auth v2).
 *
 * Flow: connect wss remote-auth-gateway -> init -> nonce_proof -> QR shown ->
 * scan in app -> pending_ticket (username) -> pending_login (ticket) ->
 * POST remote-auth/login -> encrypted_token -> RSA decrypt -> caller saves it.
 * Triangle opens the IME for manual token entry; Circle quits.
 *
 * The QR session (ra + ws) is one-use: every reconnect resets the keypair and
 * the server-issued nonce, so ra_reset() must accompany every ws teardown. */
#include "screens.h"
#include "renderer.h"
#include "theme.h"
#include "../core/remoteauth.h"
#include "../core/ws.h"
#include "../core/clock.h"
#include "../core/jsonlite.h"
#include "../core/log.h"
#include "../platform/ime.h"
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RA_HOST "remote-auth-gateway.discord.gg"
#define RA_RESOURCE "/?v=2"
#define RA_ORIGIN "https://discord.com"
#define RA_RECV 16384
#define SCR_W 1920
#define SCR_H 1080
#define QR_MAX_PX 520
#define QR_MODS_MAX (177 * 177)
#define REQ_TIMEOUT_SPINS 4000

typedef struct {
    char payload[256];
    uint8_t mods[QR_MODS_MAX];
    int size;
    int valid;
} qr_view;

typedef struct {
    int done;
    int ok;
    char enc[1024];
    char username[64];
    char error[64];
} net_result;

static uint64_t now_ms(void){
    return (uint64_t)orbis_mono_s() * 1000u;
}

static void center_text(const char *s, int y, uint32_t argb, int px){
    ui_text((SCR_W - ui_text_width(s, px)) / 2, y, s, argb, px);
}

static void draw_qr(const qr_view *q){
    int mod = QR_MAX_PX / (q->size + 8);
    int plate, x0, y0, ox, oy, x, y;
    if(mod < 1) mod = 1;
    plate = (q->size + 8) * mod;
    x0 = (SCR_W - plate) / 2;
    y0 = 280;
    ui_rect(x0, y0, plate, plate, 0xFFFFFFFFu);
    ox = x0 + 4 * mod;
    oy = y0 + 4 * mod;
    for(y = 0; y < q->size; y++){
        int run = -1;
        for(x = 0; x <= q->size; x++){
            int dark = x < q->size && q->mods[y * q->size + x];
            if(dark && run < 0) run = x;
            else if(!dark && run >= 0){
                ui_rect(ox + run * mod, oy + y * mod, (x - run) * mod, mod, 0xFF000000u);
                run = -1;
            }
        }
    }
}

static void ticket_cb(int status, const char *body, void *ud){
    net_result *r = ud;
    jl_val_t *v, *et;
    r->done = 1;
    if(status != 200){
        snprintf(r->error, sizeof r->error, "HTTP %d", status);
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    et = v ? jl_obj_get(v, "encrypted_token") : NULL;
    if(et && et->type == JL_STRING){
        snprintf(r->enc, sizeof r->enc, "%s", et->str);
        r->ok = 1;
    } else {
        snprintf(r->error, sizeof r->error, "no encrypted_token");
    }
    jl_free(v);
}

static void me_cb(int status, const char *body, void *ud){
    net_result *r = ud;
    jl_val_t *v, *un;
    r->done = 1;
    if(status != 200){
        snprintf(r->error, sizeof r->error, "HTTP %d", status);
        return;
    }
    v = body ? jl_parse(body, strlen(body)) : NULL;
    un = v ? jl_obj_get(v, "username") : NULL;
    if(un && un->type == JL_STRING){
        snprintf(r->username, sizeof r->username, "%s", un->str);
        r->ok = 1;
    } else {
        snprintf(r->error, sizeof r->error, "bad user object");
    }
    jl_free(v);
}

static unsigned pad_buttons(int handle){
    OrbisPadData d;
    if(handle < 0) return 0;
    if(scePadReadState(handle, &d) < 0) return 0;
    return d.buttons;
}

static void session_reset(ws_t *ws, ra *r, qr_view *qv, int *ws_up, uint64_t *hb_at){
    ws_close(ws);
    *ws_up = 0;
    *hb_at = 0;
    ra_reset(r);
    qv->valid = 0;
}

static int manual_token(app_state *st, const char *why){
    char tok[512];
    net_result vr;
    int spins = 0;
    tok[0] = 0;
    if(!ime_prompt("Sign in with a token", "user token", tok, sizeof tok) || !tok[0]){
        log_msg("login: ime cancelled");
        return 0;
    }
    memset(&vr, 0, sizeof vr);
    rest_free(st->rest);
    st->rest = rest_new(st->http, tok);
    rest_send(st->rest, "GET", "/api/v9/users/@me", NULL, me_cb, &vr);
    while(!vr.done && spins < REQ_TIMEOUT_SPINS){
        rest_pump(st->rest, now_ms());
        if(!vr.done) usleep(15000);
        spins++;
    }
    if(vr.ok){
        snprintf(st->token, sizeof st->token, "%s", tok);
        snprintf(st->self_name, sizeof st->self_name, "%s", vr.username);
        log_msg("login: manual token accepted (%s)", vr.username);
        return 1;
    }
    log_msg("login: manual token rejected (%s)", why ? why : "");
    rest_free(st->rest);
    st->rest = rest_new(st->http, "");
    return 0;
}

int screen_login_run(app_state *st){
    ws_t ws;
    ra *r = NULL;
    qr_view qv;
    net_result lw;
    char *recv = NULL;
    char status_buf[192];
    const char *status = "Connecting to Discord...";
    int pad = -1;
    int32_t uid = 0;
    int ws_up = 0;
    int rc = 0;
    int retry_ms = 1500;
    int hb_ms = 41250;
    int prev_buttons = 0;
    int login_waiting = 0;
    uint64_t retry_at = 0;
    uint64_t hb_at = 0;

    memset(&ws, 0, sizeof ws);
    memset(&qv, 0, sizeof qv);
    memset(&lw, 0, sizeof lw);
    st->token[0] = 0;
    st->self_name[0] = 0;
    recv = malloc(RA_RECV);
    r = ra_new();
    if(!recv || !r){
        log_msg("login: alloc fail");
        free(recv);
        ra_free(r);
        return 0;
    }
    hb_ms = ra_hello_interval(r);
    if(sceUserServiceGetForegroundUser(&uid) < 0 || uid <= 0) uid = 1;
    pad = scePadOpen(uid, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if(pad < 0) log_msg("login: pad open failed (0x%x)", pad);
    log_msg("login: screen up");

    for(;;){
        uint64_t t = now_ms();

        if(!ws_up && (retry_at == 0 || t >= retry_at)){
            char key[32];
            if(ws_client_key(key, sizeof key) > 0 &&
               ws_connect(&ws, RA_HOST, 443, RA_RESOURCE, key, RA_ORIGIN) == 0){
                char pk[1024], init[1200];
                int n;
                ws_up = 1;
                retry_ms = 1500;
                hb_at = 0;
                status = "Securing session...";
                n = ra_pubkey_b64(r, pk, sizeof pk);
                if(n > 0){
                    n = snprintf(init, sizeof init,
                                 "{\"op\":\"init\",\"encoded_public_key\":\"%s\"}", pk);
                    if(n > 0) ws_send_text(&ws, init, (size_t)n);
                }
                log_msg("login: ws connected");
            } else {
                status = "Cannot reach Discord. Retrying...";
                log_msg("login: connect failed, retry in %dms", retry_ms);
                retry_at = t + (uint64_t)retry_ms;
                retry_ms = retry_ms < 15000 ? retry_ms * 2 : 15000;
            }
        }

        if(ws_up){
            int n, op, fin;
            while((n = ws_recv_frame(&ws, recv, RA_RECV - 1, &op, &fin)) != 0){
                if(n < 0) break;
                if(op == 9){ ws_pong(&ws); continue; }
                if(op == 8){
                    int code = n >= 2 ? (((unsigned char)recv[0] << 8) |
                                         (unsigned char)recv[1]) : 1006;
                    log_msg("login: ws closed (%d)", code);
                    session_reset(&ws, r, &qv, &ws_up, &hb_at);
                    if(!st->token[0]){
                        status = "Session closed. Showing a fresh code...";
                        retry_at = t + 1500;
                    }
                    break;
                }
                if(op != 1) continue;
                recv[n] = 0;
                {
                    ra_event ev;
                    int k = ra_feed(r, recv, (size_t)n, &ev);
                    if(k == RA_HELLO){
                        hb_ms = ra_hello_interval(r);
                        hb_at = t + (uint64_t)hb_ms;
                    } else if(k == RA_NONCE){
                        char nb[800];
                        int nn = ra_respond_nonce(r, nb, sizeof nb);
                        if(nn > 0) ws_send_text(&ws, nb, (size_t)nn);
                    } else if(k == RA_QR_READY){
                        snprintf(qv.payload, sizeof qv.payload, "%s", ev.qr_payload);
                        if(ra_qr_modules(qv.payload, qv.mods, sizeof qv.mods, &qv.size)){
                            qv.valid = 1;
                            status = "Scan this QR code with the Discord mobile app";
                            log_msg("login: qr ready (%d modules)", qv.size);
                        } else {
                            log_msg("login: qr encode failed");
                        }
                    } else if(k == RA_USER_PENDING){
                        snprintf(st->self_name, sizeof st->self_name, "%s", ev.username);
                        snprintf(status_buf, sizeof status_buf,
                                 "Logged in as %s. Confirm on your phone", ev.username);
                        status = status_buf;
                        log_msg("login: user pending (%s)", ev.username);
                    } else if(k == RA_TICKET && !login_waiting){
                        char body[384];
                        snprintf(body, sizeof body, "{\"ticket\":\"%s\"}", ev.ticket);
                        memset(&lw, 0, sizeof lw);
                        rest_send(st->rest, "POST", "/api/v9/users/@me/remote-auth/login",
                                  body, ticket_cb, &lw);
                        login_waiting = 1;
                        status = "Finishing login...";
                        log_msg("login: ticket received");
                    } else if(k == RA_CANCELLED){
                        log_msg("login: cancelled in app");
                        status = "Cancelled. Showing a fresh code...";
                        session_reset(&ws, r, &qv, &ws_up, &hb_at);
                        retry_at = t + 1500;
                    } else if(k == RA_AUTH_FAILED){
                        log_msg("login: auth failed");
                        status = "Login failed. Retrying...";
                        session_reset(&ws, r, &qv, &ws_up, &hb_at);
                        retry_at = t + 1500;
                    }
                }
            }
            if(ws_up && hb_at && t >= hb_at){
                char hb[64];
                int hn = ra_heartbeat(hb, sizeof hb);
                if(hn > 0) ws_send_text(&ws, hb, (size_t)hn);
                hb_at = t + (uint64_t)hb_ms;
            }
        }

        if(login_waiting){
            if(!lw.done){
                rest_pump(st->rest, t);
                if(lw.done) continue;
            } else {
                login_waiting = 0;
                if(ra_decrypt_token(r, lw.enc, st->token, sizeof st->token) > 0){
                    log_msg("login: token acquired");
                    rc = 1;
                    break;
                }
                snprintf(status_buf, sizeof status_buf,
                         "Login failed (%s). Retrying...", lw.error);
                status = status_buf;
                session_reset(&ws, r, &qv, &ws_up, &hb_at);
                retry_at = t + 1500;
            }
        }

        {
            unsigned b = pad_buttons(pad);
            unsigned pressed = b & ~(unsigned)prev_buttons;
            prev_buttons = (int)b;
            if(pressed & ORBIS_PAD_BUTTON_CIRCLE){
                log_msg("login: user quit");
                break;
            }
            if(pressed & ORBIS_PAD_BUTTON_TRIANGLE){
                if(manual_token(st, "user requested")){
                    rc = 1;
                    break;
                }
                status = "Token rejected. Try again.";
            }
        }

        ui_frame_begin();
        ui_rect(0, 0, SCR_W, SCR_H, UI_CHAT_BG);
        center_text("orbis-discord", 120, UI_BLURPLE, 40);
        center_text("Discord for PlayStation 4", 176, UI_TEXT_DIM, 18);
        if(qv.valid) draw_qr(&qv);
        center_text(status, 852, UI_TEXT, 20);
        if(pad >= 0)
            center_text("Triangle: enter token manually          Circle: quit",
                        1000, UI_TEXT_DIM, 16);
        ui_frame_end();
        ui_pump();
        usleep(15000);
    }

    ws_close(&ws);
    ra_free(r);
    free(recv);
    if(pad >= 0) scePadClose(pad);
    log_msg("login: done (%s)", rc ? "token" : "quit");
    return rc;
}
