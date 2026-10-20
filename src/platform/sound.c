/* sound.c - one SDL audio device, two pre-rendered mono buffers.
 * Double-buffered so a new tone never overlaps an in-flight read; the
 * callback only reads the active buffer. */
#include "sound.h"
#include "../model/tone.h"
#include "log.h"
#include <SDL2/SDL.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#define SOUND_CFG "/data/orbis-discord/sound"

#define SND_FRAMES 22050

static SDL_AudioDeviceID s_dev;
static int16_t s_buf[2][SND_FRAMES];
static volatile int s_cur = -1;
static volatile int s_len, s_pos;
static volatile int s_enabled = 1;

static void snd_cb(void *ud, Uint8 *stream, int len){
    int frames = len / 2;
    int16_t *out = (int16_t *)stream;
    int i;
    (void)ud;
    memset(stream, 0, (size_t)len);
    if(s_cur < 0) return;
    for(i = 0; i < frames; i++){
        if(s_pos >= s_len){
            s_cur = -1;
            break;
        }
        out[i] = s_buf[s_cur][s_pos++];
    }
}

int sound_init(void){
    SDL_AudioSpec want;
    if(s_dev) return 1;
    if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0){
        log_msg("sound: SDL audio init failed (%s)", SDL_GetError());
        return 0;
    }
    memset(&want, 0, sizeof want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = snd_cb;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if(!s_dev){
        log_msg("sound: open failed (%s)", SDL_GetError());
        return 0;
    }
    SDL_PauseAudioDevice(s_dev, 0);
    log_msg("sound: ready");
    return 1;
}

void sound_load_state(void){
    char b[8];
    ssize_t n;
    int fd = open(SOUND_CFG, O_RDONLY);
    if(fd < 0) return;
    n = read(fd, b, sizeof b - 1);
    close(fd);
    if(n <= 0) return;
    b[n] = 0;
    s_enabled = b[0] != '0';
}

static void sound_save_state(void){
    int fd;
    const char *v = s_enabled ? "1\n" : "0\n";
    mkdir("/data/orbis-discord", 0700);
    fd = open(SOUND_CFG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if(fd < 0) return;
    if(write(fd, v, 2) != 2)
        log_msg("sound: preference save failed");
    close(fd);
}

int sound_enabled(void){ return s_enabled; }
void sound_set_enabled(int on){ s_enabled = on; sound_save_state(); }

static void play(int kind){
    int next, n;
    if(!s_dev || !s_enabled) return;
    next = s_cur == 0 ? 1 : 0;
    n = tone_render(kind, s_buf[next], SND_FRAMES);
    if(n <= 0) return;
    SDL_LockAudioDevice(s_dev);
    s_pos = 0;
    s_len = n;
    s_cur = next;
    SDL_UnlockAudioDevice(s_dev);
}

void sound_message(void){ play(TONE_MSG); }
void sound_mention(void){ play(TONE_MENTION); }
