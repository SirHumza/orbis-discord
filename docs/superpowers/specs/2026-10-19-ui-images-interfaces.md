# Frozen interfaces: UI, images/GIFs, fonts (Task 7 / Task 7b)

All code C99, snake_case, no comments unless constraint-hidden. Must compile clean with
`-Wall -Wextra -Werror` on host tests and via `./scripts/build.sh elf` for PS4.

## ui/theme.h (agent A)

Exact ARGB values (uint32 = 0xAARRGGBB):
```
UI_RAIL       0xFF1E1F22   /* #1e1f22 */
UI_CHANNEL_BG 0xFF2B2D31   /* #2b2d31 */
UI_CHAT_BG    0xFF313338   /* #313338 */
UI_BLURPLE    0xFF5865F2   /* #5865F2 */
UI_TEXT       0xFFDBDEE1   /* #dbdee1 */
UI_TEXT_DIM   0xFF949BA4   /* #949ba4 */
UI_TEXT_LINK  0xFF00A8FC   /* #00a8fc */
UI_DANGER     0xFFF23F43   /* #f23f43 */
UI_ONLINE     0xFF23A55A   /* #23a55a */
UI_IDLE       0xFFF0B232   /* #f0b232 */
UI_DND        0xFFF23F43   /* #f23f43 */
UI_OFFLINE    0xFF80848E   /* #80848e */
UI_HEADER_BG  0xFF313338
UI_SEP        0xFF3F4147   /* #3f4147 */
UI_BG_MODAL   0xFF2B2D31
```
Layout: `UI_RAIL_W 72`, `UI_CHANLIST_W 240`, `UI_MEMBERLIST_W 240`, `UI_FS_BASE 16`,
`UI_FS_SMALL 14`, `UI_FS_TITLE 20` (pixels; logical surface 1920x1080).

## ui/renderer.h (agent A)

```
int  ui_init(void);
void ui_frame_begin(void);
void ui_frame_end(void);
void ui_rect(int x,int y,int w,int h,uint32_t argb);
void ui_rrect(int x,int y,int w,int h,int radius,uint32_t argb);
int  ui_text(int x,int y,const char *utf8,uint32_t argb,int px);
int  ui_text_width(const char *utf8,int px);
void ui_set_clip(int x,int y,int w,int h);
void ui_clear_clip(void);
int  ui_img(const char *key,int x,int y,int w,int h);
void ui_pump(void);
```
- `ui_img` calls into image.h (`image_get`/`image_ready`/`image_pixels_rgba`/`image_frame_ts`);
  when the image is not ready it draws nothing and returns 0. SDL texture per URL key,
  re-uploaded when `image_frame_ts` changes (GIF frames).
- Fonts: `ui_text` uses Roboto for text; NotoEmoji fallback for emoji codepoints;
  unknown glyphs draw a box, never crash.
- `ui_pump` polls SDL events; pad mapping is platform/input.c's job later (not this task).

## third_party (agents A+B fetch, commit)

- `stb_truetype.h`, `stb_image.h` from https://raw.githubusercontent.com/nothings/stb/master/
- `gifdec.h`, `gifdec.c` from https://raw.githubusercontent.com/lecram/gifdec/master/
- `qrcodegen.h`, `qrcodegen.c` from https://raw.githubusercontent.com/nayuki/QR-Code-generator/master/c/qrcodegen.c (.h same dir)
- assets: Roboto-Regular.ttf and NotoEmoji-Regular.ttf (both redistributable: Apache-2.0 / OFL).
  Embed as generated C headers (`assets/roboto_ttf.h`, `assets/notoemoji_ttf.h`) with
  `unsigned char <name>_ttf[]` + `unsigned int <name>_ttf_len`. Regenerate via
  `python3 scripts/embed_assets.py` (agent A writes this script).

## image.h (agent B)

```
typedef struct image image;
void image_set_fetch(int (*fetch)(const char *url, unsigned char **out, size_t *len, void *ud), void *ud);
image *image_get(const char *url);
void  image_tick(uint64_t now_ms);
int   image_ready(const image *);
int   image_w(const image *);
int   image_h(const image *);
const uint32_t *image_pixels_rgba(const image *);
uint64_t image_frame_ts(const image *);
const char *image_path_on_disk(const image *);
int   image_save(const char *url, const char *dir);
void  image_cache_clear(void);
```
- Formats: PNG/JPEG via stb_image; GIF (incl. animated, frame stepping by timestamp,
  disposal handling) via gifdec. RGBA8888, top-left origin.
- Cache keyed by exact URL; one fetch per URL; failed fetch = permanent miss (return NULL
  pixels, ready=0) but never crash. Max cache 96 entries, LRU evict.
- `image_save`: downloads if needed, writes bytes to `<dir>/<fnv1a16(url)>.<ext>`
  (.png/.jpg/.gif by magic bytes), returns 1 on success, 0 otherwise. `/data` path
  injection: `image_set_save_dir(const char*)` default `/data/orbis-discord/downloads`.
- Fetch callback is injected: PS4 wiring uses tls/http; host tests inject stubs.
- Disk cache: when fetch fails on device later this is out of scope; no disk cache now.

## Host tests (agent B: tests/test_image.c, agent A: tests/test_ui.c)

- Fixtures generated with Python into `tests/fixtures/` as C arrays (`tests/fixtures.c`):
  `png_2x2` (red/green/blue/white), `gif_anim_2f` (2 frames, different colors),
  `gif_static`, `bad_bytes`.
- test_image: unknown url -> image_get returns handle, ready=0, pixels NULL;
  after fetch stub + image_tick -> ready=1, dims correct, pixel colors exact;
  same url twice -> fetch called once; gif frame ts changes after tick(t+delay);
  save writes magic-correct file; broken bytes -> no crash, not ready.
- test_ui: theme constants byte-exact ARGB values above.

## build integration (pre-wired by controller; DO NOT edit build.sh)

`scripts/build.sh` globs `src/ui/*.c`, `src/platform/*.c`, `src/core/*.c`,
`src/model/*.c` and links `-lSDL2 -lSDL2main`. Add sources as files; no build.sh edits.
Host Makefile: agents add pure sources + their test file to `tests/Makefile` lists
(`APP_SRCS`, `TESTS`). test_main.c already calls `run_ui_tests()`/`run_image_tests()`;
declare/define them in tests.h and the agents' test files.
