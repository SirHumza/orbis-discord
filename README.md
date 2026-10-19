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
tests/        host test suite (107 checks) + fixtures
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

- [ ] Attachments (upload/download), embeds, link previews, image viewer
- [ ] Reactions, custom emoji, GIF picker, sticker rendering
- [ ] Replies, edits, deletes, pins, message search
- [ ] Typing indicators, unread badges, mentions, notification sounds
- [ ] Avatars/guild icons from the CDN, member profiles, role colors
- [ ] DMs and group DMs, friends list, presence control
- [ ] Threads, forums, invites, channel categories, permissions
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
- User-token clients are against Discord's terms; use at your own risk.

## Credits

OpenOrbis PS4 toolchain, SDL2, mbedTLS, stb_image/stb_truetype, gifdec,
qrcodegen (Project Nayuki), Roboto and NotoEmoji fonts.
