#include "tone.h"
#include <math.h>

#define TONE_SR 44100
#define TONE_PI 3.14159265358979323846

int tone_frames(int kind){
    return kind == TONE_MENTION ? TONE_SR * 240 / 1000 : TONE_SR * 140 / 1000;
}

int tone_render(int kind, int16_t *out, int cap){
    int total, n, i;
    double base;
    if(!out || cap <= 0) return 0;
    if(kind != TONE_MSG && kind != TONE_MENTION) return 0;
    total = tone_frames(kind);
    n = total < cap ? total : cap;
    base = kind == TONE_MENTION ? 880.0 : 660.0;
    for(i = 0; i < n; i++){
        double t = (double)i / TONE_SR;
        double freq = (kind == TONE_MENTION && i >= total / 2) ? 1174.0 : base;
        double env = 1.0 - (double)i / total;
        out[i] = (int16_t)(sin(2.0 * TONE_PI * freq * t) * env * 8000.0);
    }
    return n;
}
