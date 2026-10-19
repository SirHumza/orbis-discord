# orbis-discord

A native Discord client for the PlayStation 4. C99, SDL2, OpenOrbis toolchain,
statically vendored mbedTLS. No browser, no WebView: the UI is drawn from
scratch to match the Discord desktop layout pixel for pixel.

Status: **WIP (work in progress).** This is a hobby project, not a finished
client. It boots, logs in with the Discord mobile app (QR remote auth) or a
pasted user token, connects to the gateway, and implements text messaging,
DMs, friends, reactions, threads, forums, invites, and settings. Voice and
mic capture are in progress; screenshare is not started. Nothing has been
smoke-tested on retail hardware yet.

## What works today

| Area | State | Verified by |
|---|---|---|
| TLS 1.2/1.3 HTTP + WebSocket client (mbedTLS, curated CA bundle) | Done | host tests, PS4 elf |
| Gateway session: identify, heartbeat, zombie detection, backoff reconnect | Done | host tests, PS4 elf |
| REST queue with 429 `retry_after` handling | Done | host tests |
| QR login (remote auth v2: nonce proof, RSA-OAEP, ticket exchange) | Done | host tests, PS4 elf |
| Manual token login fallback via PS4 IME, token stored `0600` | Done | PS4 elf |
| Guild + channel list (REST bootstrap), pad navigation with hold-repeat | Done | host tests, PS4 elf |
| SDL2 renderer, Roboto + NotoEmoji embedded, Discord palette | Done | host tests, PS4 elf |
| PNG/JPEG/GIF decode, LRU cache, save to `/data/orbis-discord/downloads` | Done | host tests |
| Message attachments: image/GIF thumbnails and file rows, CDN fetch wired | Done | host tests, PS4 elf |
| Avatars (chat, members, DMs) and guild icons from the CDN | Done | host tests, PS4 elf |
| Role colors: guild roles parsed, author/member names colored | Done | host tests, PS4 elf |
| Embeds/link previews: color bar, site/author, title, description, image, footer | Done | host tests, PS4 elf |
| Reactions: chips, counts, me highlight, live gateway updates | Done | host tests, PS4 elf |
| Typing indicators: TYPING_START, composer strip, /typing POST | Done | host tests, PS4 elf |
| Reply previews: referenced-message rows, preserved on edit | Done | host tests, PS4 elf |
| Mentions: token resolution + mentions-me highlight | Done | host tests, PS4 elf |
| Stickers: PNG/APNG/GIF first frame, Lottie name fallback | Done | host tests, PS4 elf |
| Unread dots on channel/DM rows (session-scoped read state) | Done | host tests, PS4 elf |
| Image viewer: fullscreen fit, UP/DOWN cycling, TRIANGLE/CROSS close | Done | host tests, PS4 elf |
| Message actions: SQUARE select, UP/DOWN step, CROSS menu (reply/edit/delete) | Done | host tests, PS4 elf |
| Reaction interaction: quick picker, REST PUT/DELETE own reaction | Done | host tests, PS4 elf |
| Pins: L2 overlay, pinned message list, CROSS jumps to the message | Done | PS4 elf |
| Message search: R2 query overlay (guild/channel endpoint), CROSS jumps | Done | PS4 elf |
| Friends: OPTIONS overlay from relationships, CROSS opens/creates a DM | Done | PS4 elf |
| Group DMs: multi-select create, rename, TRIANGLE member list with add/remove | Done | host tests, PS4 elf |
| Presence: L3 status picker (online/idle/dnd/invisible) via gateway op 3 | Done | host tests, PS4 elf |
| Member profiles: CROSS menu card with avatar, tag, joined date, bot badge | Done | PS4 elf |
| Custom emoji picker: guild emoji row in the reaction picker, sends name:id | Done | host tests, PS4 elf |
| GIF picker: Tenor search/trending grid (touchpad), CROSS sends the GIF link | Done | host tests, PS4 elf |
| Guild rail badges: unread dot, mention-count pill per guild | Done | host tests, PS4 elf |
| Notification sounds: distinct message/mention tones via SDL audio | Done | host tests, PS4 elf |
| Role hoisting: member list grouped by hoisted role, colored headers | Done | host tests, PS4 elf |
| Channel categories: header rows group channels, CROSS collapses a category | Done | PS4 elf |
| Channel permissions: role/overwrite VIEW filtering, SEND_MESSAGES gating | Done | host tests, PS4 elf |
| Channel invites: SQUARE on a text channel lists, creates, and revokes invites | Done | host tests, PS4 elf |
| Threads: TRIANGLE lists active threads, CROSS opens one in the channel list | Done | host tests, PS4 elf |
| Forums: forum channels open a posts overlay, CROSS opens a post as a thread | Done | host tests, PS4 elf |
| Settings: L3 -> Settings shows account/gateway info, sound toggle, logout | Done | host tests, PS4 elf |
| Voice join: CROSS on a voice channel runs the voice WS/UDP handshake | Partial (no audio yet) | host tests, PS4 elf |
| Attachments: SQUARE in the viewer saves to downloads, L1 uploads a saved file | Done | host tests, PS4 elf |
| Message history: 50-msg pages, UTF-8 wrap, grouping, day separators, scroll paging | Done | host tests, PS4 elf |
| Composer via PS4 IME, send with optimistic append + ack correction | Done | host tests, PS4 elf |
| Member list (REST members, presence dots from gateway), DM home, offline banner | Done | host tests, PS4 elf, PKG |
| Voice chat, mic capture, screenshare | Not started (design pending) | — |

Nothing has been smoke-tested on retail hardware yet; "PS4 elf" means it
compiles and links against the OpenOrbis SDK, not that it has run on a console.

## Layout

```
src/core/     transport (TLS, ws, http, rest), gateway, events, remote auth
src/model/    snowflake ids + in-memory store (messages, channels, guilds, members)
src/ui/       renderer, font, theme, login + main screens, nav
src/platform/ PS4 glue: token file, IME dialog, pad input
src/image.c   image/GIF pipeline + cache
tests/        host test suite (175 checks) + fixtures
third_party/  mbedTLS, stb_image, stb_truetype, gifdec (patched), qrcodegen
```

Data flow: the gateway carries realtime deltas; bulk state (guild list,
channels, history) is fetched over REST. Large gateway frames (a real
account's READY is ~12 MB) are streamed to an `op/s/t` envelope instead of
buffered, because buffering them exhausted console memory.

## Build and run

Host tests:

```sh
make -C tests clean && make -C tests test
```

PS4 ELF (needs OpenOrbis at `$OO_PS4_TOOLCHAIN` or `/Users/mac/PS4Toolchain/OpenOrbis/PS4Toolchain`):

```sh
./scripts/build.sh elf     # build/orbis-discord.elf
./scripts/build.sh fself   # fake-signed self for a jailbroken console
make pkg                   # build/orbis-discord.pkg (installable package)
```

Runtime paths on the console:
`/data/orbis-discord/token` (0600), `/data/orbis-discord/orbis-discord.log`,
`/data/orbis-discord/downloads/`.

## Roadmap to full parity

Phase 1 (in progress):

- [x] Transport fork, stores, gateway ingestion, REST, QR remote auth
- [x] SDL2 renderer/font/theme, image + GIF pipeline
- [x] Login screen, main layout, pad navigation, live gateway session
- [x] Message view: history fetch, UTF-8 wrapping, grouping, day separators, scroll paging, live append
- [x] Composer + IME + send, optimistic append, ack correction
- [x] Member list + presence dots, offline banner, DM list, PKG packaging
- [ ] Whole-branch review, deploy, on-device smoke (login → channel → history → send → emoji → reconnect)

Phase 2, messaging parity:

- [x] Attachment download (viewer save) and upload (multipart, saved files)
- [x] Link previews (embed cards: site/author, title, description, image, footer)
- [x] Image viewer: fullscreen fit with UP/DOWN cycling, TRIANGLE opens, CROSS closes
- [x] Reaction rendering: chip row, counts, me highlight, live gateway updates
- [x] Stickers: PNG/APNG rendered, GIF first frame, Lottie name fallback
- [x] Reaction interaction: quick-reaction picker with REST PUT/DELETE
- [x] Custom emoji picker (guild emoji row in the reaction picker)
- [x] GIF picker (Tenor search/trending grid, CROSS sends the GIF link)
- [x] Reply previews: referenced-message preview rows, preserved across edits
- [x] Reply send (REST), own-message edit/delete via SQUARE-selected action menu
- [x] Pins overlay (L2) with jump-to-message
- [x] Message search (R2, scoped to the open channel) with jump-to-message
- [x] Typing indicators: TYPING_START receive, composer strip, /typing POST on composer open
- [x] Mentions: @user/#channel/:emoji: token resolution, mentions-me row highlight
- [x] Unread dots on channel and DM rows (read state is session-scoped)
- [x] Guild-rail unread dots and mention-count badges
- [x] Notification sounds (message/mention tones, gateway-driven)
- [x] Avatars/guild icons from the CDN, role colors on author/member names
- [x] Member profiles (avatar, tag, joined date, bot badge)
- [x] Role hoisting (member list sections by hoisted role)
- [x] DM list (REST), friends overlay (OPTIONS) with open/create DM
- [x] Group DM creation (friends multi-select), rename, member add/remove
- [x] Presence control (L3 picker, gateway op 3)
- [x] Channel categories (grouped channel list, CROSS collapse)
- [x] Channel permissions (VIEW_CHANNEL filtering, SEND_MESSAGES gating)
- [x] Channel invites (list/create/revoke)
- [x] Active threads (list, open)
- [x] Forums (browse and open active posts; creating posts is not wired)
- [x] Settings screen (account/gateway info, sound toggle, logout)
- [ ] Slash commands, context menus, extra keyboard/pad shortcuts
- [x] Voice join: gateway op 4, voice WS identify, UDP IP discovery, select_protocol
- [ ] Voice receive: Opus decode, XChaCha20/XSalsa20 decrypt, per-user playback
- [ ] Mic capture (sceAudioIn) and input device selection
- [ ] Screenshare: capture, encode, WebRTC go-live (hardware AVC access TBD)

## Known limitations

- Reconnect always re-IDENTIFYs; there is no RESUME (session id is lost to
  the oversized-READY envelope). Repeated drops hit Discord's identify budget.
- Gateway frames above 64 KB are reduced to `op/s/t`; a rare oversized
  MESSAGE_CREATE is recovered by refetching history, not live.
- Voice joins complete the voice WS/UDP handshake but carry no audio yet;
  DAVE (E2EE) negotiation is not implemented, so servers that require it
  will reject the session.
- Attachments are fetched synchronously from the CDN on first draw, so a slow
  or large image can stall the render loop (HTTP responses are capped at 4 MB).
- Attachments are capped at 4 per message and embeds at 2; embed fields
  and video embeds are not rendered. Uploads are
  attachment-only (no caption), limited to 8 MB, and read from
  `/data/orbis-discord/downloads`.
- User-token clients are against Discord's terms; use at your own risk.

## Credits

OpenOrbis PS4 toolchain, SDL2, mbedTLS, stb_image/stb_truetype, gifdec,
qrcodegen (Project Nayuki), Roboto and NotoEmoji fonts.
