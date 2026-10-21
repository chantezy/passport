#ifndef EP_AUDIO_H
#define EP_AUDIO_H
#include <stdbool.h>
#include <stdint.h>
/* Initialize once, before concurrent calls. Validates Flash pack and hardware. */
bool ep_audio_init(void);
/* Nonblocking replacement of the current clip. id=0 cancels. Volume 0..100. */
void ep_audio_play(uint16_t clip, uint8_t volume);
bool ep_audio_busy(void);
/* Initialization/last accepted playback failure. New valid playback clears it. */
bool ep_audio_failed(void);
#endif
