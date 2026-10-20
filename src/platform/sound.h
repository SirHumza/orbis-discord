/* sound.h - PS4 audio out for notification tones (SDL audio backend). */
#ifndef ORBIS_SOUND_H
#define ORBIS_SOUND_H

int sound_init(void);
void sound_load_state(void);
int sound_enabled(void);
void sound_set_enabled(int on);
void sound_message(void);
void sound_mention(void);

#endif
