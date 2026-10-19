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
- [~] T7 SDL2 renderer + font + theme (code done 72/72 green + elf builds; commit/ledger pending)
- [~] T7b image pipeline PNG/JPEG/GIF + LRU cache + save, gifdec memory-input patch (code done; commit/ledger pending)
- [ ] ws.c: optional `Origin: https://discord.com` header for remote-auth gateway
- [ ] ra_render_qr: qrcodegen → RGBA bitmap, quiet zone 4, host test
- [ ] T8 login screen with live QR + manual token fallback + persist `/data/orbis-discord/token` (0600)
- [ ] T9 main layout skeleton + pad navigation (`src/platform/input.c`, `nav.c`, `test_nav`)
- [ ] T10 message view: history fetch `GET /channels/{id}/messages?limit=50`, wrapping, grouping, day separators, live append, scroll paging (`rest_get_messages`, `msgview.c`, `test_msgview`)
- [ ] T11 composer + IME dialog (sceCommonDialog) + send `POST /channels/{id}/messages`, optimistic append + ack correction (`platform/ime.c`, `test_ime_utf8`)
- [ ] T12 member list + presence dots + offline banner + DM list (`GET /users/@me/channels`) + PKG packaging (`make pkg`)
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
