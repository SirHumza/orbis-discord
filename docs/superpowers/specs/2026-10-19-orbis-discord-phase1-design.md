# orbis-discord — Phase 1 Design: Login, Chat, Pixel-Matched UI

Date: 2026-10-19
Status: Approved direction (user delegated execution: "all up to you")

## Goal

A native PS4 homebrew Discord client (`.elf`, packaged for GoldHEN / homebrew menu)
that looks and behaves like Discord. Phase 1 delivers: QR-code login, full text chat
(servers, channels, DMs, messages, member list), and a Discord-matched UI.

Voice and screenshare are later phases; server discovery is out (invite-code join only).

## Prior art and reuse

From `orbisRPC/orbisrpc` (proven on-device, fork into this repo):
- `tls.c` / `ca_bundle_pem.h` — mbedTLS client, CA verification
- `ws.c`, `ws_skip.c`, `ws_env.c` — TLS websocket client incl. oversized-frame handling
- `discord.c` — gateway HELLO/IDENTIFY/READY, heartbeat, reconnect with backoff
- `jsonlite.c`, `jsonstr.c`, `b64.c` — JSON + base64
- IME keyboard pattern from `installer` (scePadInit + Pad common dialog init order)

Protocol references:
- Concord (C99 Discord lib) — gateway/REST structure reference
- Discord Userdoccers / undocumented docs — remote auth (QR) protocol:
  `wss://remote-auth-gateway.discord.gg/?v=2`, ops `hello`/`init`/`nonce_proof`/
  `pending_remote_init`/`pending_ticket`/`pending_login`/`cancel`; RSA-2048 OAEP
  via mbedTLS; token retrieved with `POST /users/@me/remote-auth/login`.

## Auth (QR code login)

Primary: QR login. User needs zero typing on the gamepad.
1. Connect remote-auth gateway, RSA keygen (2048, SPKI base64), `init`
2. Verify `nonce_proof` (OAEP decrypt + base64 proof)
3. On `pending_remote_init`: render QR of `https://discord.com/ra/{fingerprint}`
4. Poll until `pending_ticket` (show avatar/username from decrypted payload)
5. On mobile confirm → `pending_login` → `POST /users/@me/remote-auth/login`
   → OAEP-decrypt `encrypted_token`
6. Persist token to `/data/orbis-discord/token`
Fallback: manual token paste via IME keyboard (settings screen).

Caveat stated to user: user-token clients operate outside Discord's ToS; account
risk is theirs. Recommend a throwaway account for testing.

## Architecture

```
src/
  core/     net (REST over TLS, HTTP/1.1 client), gateway (reuse ws/discord.c),
            remoteauth (QR handshake), ratelimit (5xx/429 handling)
  model/    store: guilds, channels, messages, users, members; snowflake utils;
            event handlers (READY, MESSAGE_CREATE/UPDATE/DELETE, presence)
  ui/       renderer (SDL2), font (stb_truetype), widgets (server rail, channel
            list, message view, member list, composer, login screen, toasts),
            theme (Discord palette/spacing constants), nineslice/rounded rect
  platform/ ime (on-screen keyboard), storage (/data), pad input, clock, net poll
main.c      event loop: poll pad + gateway + net; single-threaded with async
            REST queue driven by the same poll loop as orbisRPC's daemon
assets/     embedded font(s); icons drawn or embedded
```

UI screens: boot → login (QR big center, status line) → main layout ==
Discord: 72px server rail | 240px channel list | chat column | 240px member list
(collapsed on 720p-follow). Gamepad navigation: D-pad/stick focus, cross=select,
options=menu, touchpad=?? (no), triangle=back. Text input only via IME dialog.

Discord UI match: exact color palette (e.g. #313338 chat bg, #2b2d31 channel bg,
#1e1f22 rail, #5865F2 blurple), 16px base font, message grouping/avatars from
CDN (image loads best-effort; text-only fallback when image fetch fails).

## Data flow

Boot → token? → QR/rest login → gateway connect (IDENTIFY w/ user props) →
READY populates stores → subscribe guild/channel layout → UI renders from stores.
Outbound: composer → IME string → `POST /channels/{id}/messages` (respect
ratelimit headers). Inbound: gateway dispatch → store patch → UI refresh marks.

## Error handling

Reconnect with exponential backoff (orbisRPC pattern), RESUME session where
possible, ratelimit: single-flight REST queue honoring 429 retry_after, offline
banner when gateway down, QR timeout (4003) → regenerate QR automatically.

## Testing

Host-side (macOS): unit tests for snowflake decode, JSON message parsing, store
diff logic, remote-auth payload crypto (mock server), theme constant sanity.
On-device: build .elf via OpenORBIS toolchain, deploy via FTP to 192.168.1.136
(orbisRPC deploy pattern), manual smoke: login, load guilds, send/receive message,
reconnect after network blip.

## Milestones

1. Transport skeleton: fork orbisRPC net files, REST client, gateway connect+READY logged
2. QR login working (token persisted), manual token fallback
3. Model stores + gateway handlers; host-side tests green
4. SDL2 renderer + widget kit + theme; login screen pixel-matched
5. Main layout: rail, channel list, message view, member list, composer (IME)
6. Polish: scroll, message grouping, presence dots, offline/reconnect UX, PKG build
