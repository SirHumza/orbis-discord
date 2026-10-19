# orbis-discord — todo

Legend: `[x]` done · `[~]` in progress · `[ ]` pending.
Gates: host `make -C tests test` · PS4 `./scripts/build.sh elf` · console FTP `192.168.1.136:2121`.

## Phase 1 (plan tasks)

- [x] T1 scalar scaffold + orbisRPC transport fork + host test harness (43→41 checks at the time)
- [x] T2 snowflake + model stores (sorted messages, grouping, UTF-8 byte-exact)
- [x] T3 gateway event ingestion (READY/messages/presence/malformed-safe)
- [x] T4 REST client + ratelimit queue (429 retry_after honored)
- [x] T5 QR remote auth (RSA-OAEP, nonce proof, fingerprint, 4003 cancel)
- [x] T6 gateway pump + backoff/heartbeat + PS4 elf build check
- [x] T7 SDL2 renderer + font + theme (commit 404e204)
- [x] T7b image pipeline PNG/JPEG/GIF + LRU cache + save, gifdec memory-input patch (commit 06cc616)
- [x] ws.c: optional `Origin: https://discord.com` header for remote-auth gateway
- [x] ra_qr_modules: qrcodegen → 1-bit module matrix (renderer draws rects), host test
- [x] T8 login screen with live QR + manual token fallback + persist `/data/orbis-discord/token` (0600) (commit eea0277)
- [x] T9 main layout skeleton + pad navigation + live gateway session (`platform/input.c`, `nav.c`, `screen_main.c`, `test_nav`)
- [x] T10 message view: history fetch `GET /channels/{id}/messages?limit=50`, wrapping, grouping, day separators, live append, scroll paging (`rest_get_messages`, `msgview.c`, `test_msgview`)
- [x] T11 composer + IME dialog (sceCommonDialog) + send `POST /channels/{id}/messages`, optimistic append + ack correction (`platform/ime.c`, `test_ime_utf8`)
- [ ] T12 member list + presence dots + offline banner + DM list (`GET /users/@me/channels`) + PKG packaging (`make pkg`)
- [ ] Full parity feature backlog (see README roadmap)
- [ ] Final whole-branch review (fresh reviewer) + one fix pass
- [ ] Deploy to console + on-device smoke checklist (login → guild → channel → history → send → emoji → reconnect)

## Expanded scope (user additions)

- [ ] Specs/plan update: voice, mic, screenshare design doc
- [ ] Images/GIF rendering in messages + save to `/data/orbis-discord/downloads` — DONE in T7b (host-tested); on-device verify pending
- [ ] Mic capture: sceAudioIn, 48kHz, ring buffer
- [ ] Voice chat: Discord voice UDP (IP discovery, select_protocol, XSalsa20-Poly1305, Opus 48k stereo, speaking events, per-user playback via sceAudioOut)
- [ ] Screenshare: frame capture + encode + WebRTC go-live (feasibility assessed on PS4; hardware AVC encoder access TBD)

## Verification gates

- Host tests must stay green after every task: `make -C tests test`
- PS4 elf must build after every task: `./scripts/build.sh elf`
- Ledger: `.superpowers/sdd/2026-10-19-orbis-discord-phase1.md/progress.md`
