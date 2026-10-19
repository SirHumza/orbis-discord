# orbis-discord Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (user delegated session-native execution). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Native PS4 homebrew Discord client delivering QR login, full text chat, and a pixel-matched Discord UI.

**Architecture:** Single-threaded event loop (pad + gateway + REST poll). Fork orbisRPC's proven transport (mbedTLS TLS, websocket, backoff) into `src/core/`; add REST client, QR remote-auth, model stores, SDL2 widget UI. Largely portable C99 so host-side tests run on macOS via vendored mbedTLS.

**Tech Stack:** C99, OpenORBIS toolchain, SDL2 (toolchain port), mbedTLS (vendored from orbisRPC), stb_truetype, Nayuki qrcodegen (single-file C, MIT).

**Spec:** `docs/superpowers/specs/2026-10-19-orbis-discord-phase1-design.md`

## Global Constraints

- C99, snake_case, `-Wall -Wextra -Werror` clean, no comments unless constraint-hidden
- Vendored deps only: `third_party/mbedtls` (copy from orbisRPC), `third_party/stb_truetype.h`, `third_party/qrcodegen.{c,h}` — no new network deps
- Token stored at `/data/orbis-discord/token` (on-device); never logged
- TLS: mbedTLS, TLS1.2+, CA verification required (reuse orbisRPC `ca_bundle_pem.h`)
- Discord palette (must appear in `theme.h` verbatim): rail `#1e1f22`, channel bg `#2b2d31`, chat bg `#313338`, blurple `#5865F2`, text default `#dbdee1`
- Layout: 72px server rail, 240px channel list, 240px member list, rest = chat
- Host tests: `make -C tests test` runs on macOS; PS4 build: `make` produces `.elf`; deploy via FTP to `192.168.1.136:2121` like orbisRPC (`make deploy`)
- User-token client: no bot intents field; gateway IDENTIFY uses user client properties

## Review Focus

1. Oversized gateway frames (GUILD_CREATE can be MBs) — must stream, not buffer-blind → pinned by `test_ws_streaming` (Task 1)
2. Malformed/truncated gateway JSON — must not crash or corrupt stores → pinned by `test_events_malformed` (Task 3)
3. 429 burst sends — REST must serialize and honor `retry_after` → pinned by `test_rest_ratelimit` (Task 4)
4. QR session timeout (4003) — must auto-regenerate QR, not hang → pinned by `test_remoteauth_timeout` (Task 5)
5. Multi-byte UTF-8 in messages/IME (emoji) — byte-length logic, no charcount assumptions → pinned by `test_store_utf8` (Task 2), `test_ime_utf8` (Task 11)

---

### Task 1: Scaffold + fork transport + host test harness

**Files:**
- Create: `Makefile`, `tests/Makefile`, `tests/test_main.c`, `tests/test_ws_streaming.c`, `src/core/{tls.c,tls.h,ws.c,ws.h,b64.c,b64.h,jsonlite.c,jsonlite.h,ca_bundle_pem.h}`, `third_party/mbedtls/**` (copied from orbisRPC)
- Copy from: `orbisRPC/orbisrpc/{tls.c,tls.h,ws.c,ws.h,ws_skip.c,ws_env.c,ws_env.h,b64.c,b64.h,jsonlite.c,jsonlite.h,ca_bundle_pem.h}`, `orbisRPC/third_party/mbedtls/`

**Interfaces:**
- Produces: `int ws_connect(ws_t*, const char*, int, const char*, const char*)`, `int ws_recv_frame(ws_t*, char*, size_t, int*, int*)`, `int ws_send_text(ws_t*, const char*, size_t)`, `void ws_close(ws_t*)` (forked verbatim from orbisRPC); TLS behind `tls_connect/tls_read/tls_write/tls_close`; test harness: `tests/test_main.c` with `TEST(name)` + `ASSERT_*` macros, `make -C tests test` exit 0.

- [ ] Copy forked files; strip PS4-only logging deps into a thin `log.c` shim that compiles on host and PS4
- [ ] `make -C tests test` — `test_ws_streaming`: feed a simulated websocket (memory transport) a fragmented 1 MB text frame, assert payload bytes match source exactly
- [ ] `test_b64_roundtrip` + `test_jsonlite_parse` sanity tests pass
- [ ] commit: `feat: scaffold with orbisRPC transport fork + host tests`

### Task 2: Snowflake + model stores

**Files:**
- Create: `src/model/{snowflake.c,snowflake.h,store.c,store.h}`
- Test: `tests/test_store.c`

**Interfaces:**
- Produces:
  - `uint64_t sn_parse(const char *s); void sn_str(uint64_t id, char out[21]); uint64_t sn_timestamp_ms(uint64_t id);`
  - `typedef struct message { uint64_t id, chan, author_id; char author_name[64]; char content[2048]; uint64_t edited_at; int has_edit; struct message *next; } message;`
  - `typedef struct channel { uint64_t id, guild; int type; char name[100]; int nsfw, position; } channel;`
  - `typedef struct guild { uint64_t id; char name[100]; char icon_url[256]; } guild;`
  - `typedef struct member { uint64_t user, guild; char name[64]; int online; } member;`
  - `store* store_new(void); void store_free(store*);` plus `store_put_message(store*, const struct message*)` (sorted by id, groups consecutive same-author), `store_del_message(store*, uint64_t chan, uint64_t id)`, `store_put_channel/guild/member`, `store_channels_for_guild(store*, uint64_t, uint64_t out[], size_t cap)`
- Step 1: failing tests — ordering (`id 5, 9, 2` → `2,5,9`), message grouping flag, delete missing is no-op, UTF-8 emoji content stored byte-exact (`test_store_utf8` asserts `strlen` bytes == emitted bytes)
- Step 2: run: `make -C tests test` → FAIL; implement; run → PASS
- Step 3: commit `feat: snowflake + model stores`

### Task 3: Gateway event ingestion

**Files:**
- Create: `src/core/{gateway.c,gateway.h,events.c,events.h}`
- Test: `tests/test_events.c`

**Interfaces:**
- Consumes: Task 1 `ws_*`, `jsonlite`; Task 2 stores
- Produces:
  - `typedef struct gateway gateway;` `gateway* gw_new(store*, const char *token);` `int gw_feed(gateway*, const char *frame_json, size_t len);` (pure: parse one op frame, mutate store, return events bitmask) `int gw_expired_heartbeat(gateway*); const char* gw_ack(gateway*, size_t *len);` `int gw_identify(gateway*, char *out, size_t cap);`
  - OPS: 0=dispatch,1=heartbeat,2=identify,10=hello,11=heartbeat_ack,7=reconnect,9=invalid_session
- Step 1: failing tests — `test_events_ready` (READY payload fills guild/user arrays), `test_events_message_create/update/delete` mutate store, `test_events_malformed` (truncated JSON, unknown op, null `d` → returns error code, store unchanged, no crash), `test_events_resumable_seq` (seq increments, RESUME payload built with correct `d.session_id`/`d.seq`)
- Step 2: run tests (fail → implement → pass); commit `feat: gateway event ingestion`

### Task 4: REST client + ratelimit queue

**Files:**
- Create: `src/core/{http.c,http.h,rest.c,rest.h}`
- Test: `tests/test_rest.c`

**Interfaces:**
- Consumes: Task 1 `tls_*` (transport is injected for tests: `http_io` fn pointers `int (*read_fn)(void*,char*,size_t)` / `write_fn`, so tests use in-memory strings)
- Produces:
  - `typedef struct http http; http* http_new(http_io io, void *ctx); int http_request(http*, const char *method, const char *path, const char *body, http_resp *out);` with `http_resp { int status; char *body; char retry_after_ms[32]; }`
  - `rest* rest_new(http*, const char *token);` `int rest_send(rest*, const char *method, const char *path, const char *json_body, rest_cb cb, void *ud);` internal single-flight queue, honors 429 `retry_after` and bucket via `X-RateLimit` headers; `Authorization: <token>` (user token, no `Bot ` prefix)
- Step 1: failing tests — `test_rest_auth_header` (request line has correct header), `test_rest_ratelimit` (three queued sends into a stub server returning 429 with `retry_after: 100` on first → only one in-flight until virtual clock passes 100ms), `test_rest_json_body` (POST body bytes exact)
- Step 2: fail → implement → pass; commit `feat: REST client with ratelimit queue`

### Task 5: QR remote auth

**Files:**
- Create: `src/core/{remoteauth.c,remoteauth.h}`, `third_party/qrcodegen/{qrcodegen.c,qrcodegen.h}`
- Test: `tests/test_remoteauth.c`

**Interfaces:**
- Consumes: Task 1 `ws_*`, `b64`; vendored mbedTLS (`mbedtls_rsa`, SHA-256); Task 4 REST for final token fetch
- Produces:
  - `typedef struct ra ra; ra* ra_new(const char *pub_b64 /*SPKI*/);` (generates RSA-2048 keypair internally)
  - `int ra_feed(ra*, const char *frame_json, size_t len, ra_event *out);` with `ra_event { int kind; /*RA_QR_READY, RA_USER_PENDING, RA_TOKEN, RA_CANCELLED*/ char qr_payload[256]; char username[64]; char token[128]; }`
  - `int ra_respond_nonce(ra*, char *json_out, size_t cap);` (sends `nonce_proof`)
  - `int ra_render_qr(const char *payload, uint8_t *rgba_out, size_t cap, int *px_out);` (qrcodegen → RGBA bitmap, quiet zone 4 modules)
  - Token exchange: `POST /users/@me/remote-auth/login {ticket}` → `encrypted_token` → OAEP(SHA-256) decrypt
- Step 1: failing tests — `test_remoteauth_keygen` (keygen → re-encrypt random nonce with own pubkey → OAEP decrypt matches), `test_remoteauth_ops` (feed `hello`/`nonce_proof`/`pending_remote_init` fixture frames from the documented protocol → correct out events + `nonce_proof` JSON exact shape), `test_remoteauth_timeout` (inject 4003-style close → `RA_CANCELLED`, `ra_feed` allows restart, new QR fingerprint generated), `test_remoteauth_ticket_token` (stub REST returning `encrypted_token` for a fixture → decrypted token equals fixture plaintext)
- Step 2: fail → implement → pass; commit `feat: QR remote auth + qrcodegen`

### Task 6: Gateway connect state machine (live wiring)

**Files:**
- Modify: `src/core/gateway.c`, `src/main.c` (loop skeleton: poll ws, feed gw, drive rest queue)
- Test: `tests/test_gateway_loop.c`

**Interfaces:**
- Consumes: Tasks 1–5
- Produces: `int gw_pump(gateway*, ws_t*, int can_read, uint64_t now_ms);` — single tick: read frames → `gw_feed`, heartbeat on interval, reconnect/backoff cursor `0s,1s,2s,4s...30s cap`
- Step 1: failing test `test_gateway_loop` — scripted memory transport: hello → ack cadence at virtual times → drop → exponential backoff values asserted
- Step 2: implement → pass; commit `feat: gateway pump + backoff`
- Step 3: PS4 build check: `make` links against toolchain SDL2 stub-free core (UI not yet); `.elf` builds clean

### Task 7: SDL2 renderer + font + theme

**Files:**
- Create: `src/ui/{renderer.c,renderer.h,font.c,font.h,theme.h}`, `third_party/stb_truetype.h`, `assets/font.ttf` (Inter/Roboto Regular embedded via `xxd -i` at build)
- Test: host `tests/test_theme.c` (palette hex strings parse to RGB triplets exactly); on-device visual smoke

**Interfaces:**
- Produces (draw API used by screens):
  - `int ui_init(int w, int h); void ui_frame_begin(void); void ui_frame_end(void);`
  - `void ui_rect(int x,int y,int w,int h,uint32_t argb);` `void ui_rrect(int x,int y,int w,int h,int radius,uint32_t argb);` `int ui_text(int x,int y,const char *utf8,uint32_t color,int px);` `int ui_text_width(const char *utf8,int px);` `void ui_set_clip(int x,int y,int w,int h);` `void ui_img(const char *key,int x,int y,int w,int h);` (CDN avatar/icon cache, best-effort)
  - `theme.h`: `UI_RAIL 0xFF221F1E` etc. (ARGB from palette above), font sizes `UI_FS_BASE 16`
- Steps: parse palette → rrect via 4 arcs + rects on SDL renderer → font atlas (ASCII +δι octets for base plane; emoji fallback: raw bytes drawn as placeholder box, never crash) → commit `feat: SDL2 renderer, font, theme`
- On-device smoke: deploy, assert window shows three-layout skeleton colors + one text line

### Task 8: Login screen with live QR

**Files:**
- Create: `src/ui/screen_login.c`, `src/ui/screens.h`, wire into `src/main.c`
- Test: on-device flow only (protocol covered by Task 5 tests)

**Interfaces:**
- Consumes: Task 5 `ra_*`, Task 7 draw API, platform IME for token fallback
- Produces: `int screen_login_run(app_state *st);` returns logged-in state with token persisted to `/data/orbis-discord/token` (0600)
- Steps: QR centered + status line (`Scan with your Discord mobile app` / waiting / `Welcome, <name>`), auto-regenerate on cancel/timeout, circle prompt for fallback token entry; on-device: observe full successful login from phone scan; commit `feat: login screen with QR`

### Task 9: Main layout skeleton + navigation

**Files:**
- Create: `src/ui/screen_main.c`, `src/ui/nav.c,nav.h`, `src/platform/{input.c,input.h}` (pad → logical focus events; host-testable mapping)
- Test: `tests/test_nav.c`

**Interfaces:**
- Consumes: Task 7 renderer/theme; gateway stores (Task 2/3); SDL2 pad via existing PS4 得 patterns in orbisRPC `installer`
- Produces: `input_event { IE_NONE, IE_MOVE, IE_SELECT, IE_BACK, IE_UP, IE_DOWN ... }`; `int nav_step(nav*, input_event, int count) -> selected_index`; screens own focus; `void screen_main_draw(app_state *st);`
- Test: nav wrap-around/edge cases; commit `feat: main layout + pad navigation`

### Task 10: Message view (history, render, grouping)

**Files:**
- Modify: `src/ui/screen_main.c`; Create: `src/ui/msgview.c,msgview.h`, `src/core/rest` extended: `rest_get_messages(rest*, uint64_t chan, uint64_t before, rest_messages_cb cb, void *ud)`
- Test: `tests/test_msgview.c` (layout math)

**Interfaces:**
- Consumes: Tasks 2,3,4,7,9
- Produces: `void msgview_draw(msgview*, const message *head, int focus_row);` — wrapping via measured `ui_text_width`, column split at chat width, consecutive-author grouping, day separators
- Steps: fetch last 50 via `GET /channels/{id}/messages?limit=50`; render; scroll up pages; live gateway appends; commit `feat: message view`

### Task 11: Composer + IME + send

**Files:**
- Create: `src/platform/ime.c,ime.h`, `src/ui/composer.c,composer.h`
- Test: `tests/test_ime_utf8.c`, `tests/test_rest` extended for message POST

**Interfaces:**
- Consumes: orbisRPC IME dialog pattern (sceCommonDialog init order from orbisRPC installer), Task 4 REST
- Produces: `int ime_prompt(const char *title, char *out, size_t cap);` (blocks dialog; returns bytes written); `void composer_draw(...)` + on `IE_SELECT` opens IME, then `rest_send(POST /channels/{id}/messages {"content":...})`, optimistic store append replaced by MESSAGE_CREATE ack correction (nonce field)
- Test: UTF-8 emoji string byte-length in queue payload; commit `feat: composer via IME`

### Task 12: Member list, DMs, polish, packaging

**Files:**
- Modify: `src/ui/screen_main.c`; Create: `src/ui/memberlist.c`, `src/core` extend: DM channel list via `GET /users/@me/channels`; `Makefile` PKG target copying orbisRPC `installer` PKG build
- Test: full on-device smoke checklist

**Interfaces:**
- Consumes: all
- Produces: `void memberlist_draw(store*, uint64_t guild, int focus);` presence dots from gateway `PRESENCE_UPDATE`; offline banner; `make` → `.elf` + `make pkg` → `.pkg`; commit `feat: member list, DMs, packaging`
- On-device smoke: login → guilds list → open channel → history renders → send message (self + second account sees it) → emoji → delete own message → kill network 10s → reconnect + RESUME → offline banner clears

## Execution

User delegated execution: session-native (executing-plans), single fresh reviewer at end.
