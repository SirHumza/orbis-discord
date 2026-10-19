/* tone.h - notification tone synthesis (pure, host-testable). */
#ifndef ORBIS_TONE_H
#define ORBIS_TONE_H
#include <stdint.h>

#define TONE_MSG 0
#define TONE_MENTION 1

/* Mono s16, 44.1 kHz. Frames the tone occupies. */
int tone_frames(int kind);
/* Renders up to cap frames; returns frames written (0 on bad input). */
int tone_render(int kind, int16_t *out, int cap);

#endif
