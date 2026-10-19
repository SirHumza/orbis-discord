/* input.h - pad -> logical focus events.
 * Host-safe header (no orbis includes); the pad read lives in input.c. */
#ifndef ORBIS_INPUT_H
#define ORBIS_INPUT_H

typedef enum {
    IE_NONE = 0,
    IE_UP, IE_RIGHT, IE_DOWN, IE_LEFT,
    IE_SELECT, IE_BACK
} input_event;

int input_init(void);
int input_poll(input_event *out, int max);

#endif
