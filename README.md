# orbis-discord

A native Discord client for the PlayStation 4. C99, SDL2, OpenOrbis toolchain,
statically vendored mbedTLS. No browser, no WebView: the UI is drawn from
scratch to match the Discord desktop layout pixel for pixel.

Status: **Phase 1 in progress.** The client boots, logs in with the Discord
mobile app (QR remote auth) or a pasted user token, connects to the gateway,
and shows the guild/channel shell. Message history, composer, voice and the
rest of the feature set are the next milestones.

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
| Embeds: color bar, title, description wrap, image/thumbnail rows | Done | host tests, PS4 elf |
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
tests/        host test suite (162 checks) + fixtures
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

- [ ] Attachment upload/download, link previews
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
- [ ] Slash commands, context menus, keyboard/pad shortcuts, settings screen
- [ ] Voice channels: Opus, UDP voice gateway, XSalsa20-Poly1305, per-user playback
- [ ] Mic capture (sceAudioIn) and input device selection
- [ ] Screenshare: capture, encode, WebRTC go-live (hardware AVC access TBD)

## Known limitations

- Reconnect always re-IDENTIFYs; there is no RESUME (session id is lost to
  the oversized-READY envelope). Repeated drops hit Discord's identify budget.
- Gateway frames above 64 KB are reduced to `op/s/t`; a rare oversized
  MESSAGE_CREATE is recovered by refetching history, not live.
- Voice channels are listed but not joinable yet.
- Attachments are fetched synchronously from the CDN on first draw, so a slow
  or large image can stall the render loop (HTTP responses are capped at 4 MB).
- Attachments are capped at 4 per message and embeds at 2; embed fields,
  footers, authors and video embeds are not rendered, and uploads are not
  implemented.
- User-token clients are against Discord's terms; use at your own risk.

## Credits

OpenOrbis PS4 toolchain, SDL2, mbedTLS, stb_image/stb_truetype, gifdec,
qrcodegen (Project Nayuki), Roboto and NotoEmoji fonts.
