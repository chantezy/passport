#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* EPOP v1, 40-byte header / 20-byte index, 16 kHz mono 20 ms Opus.
 * Two independently checked packs may occupy non-contiguous flash regions. */
/* One Opus packet of 20 ms at 16 kHz. afconvert emits exactly one frame per
 * packet, so a clip's sample count must fit whole packets. tools/say_audio.py
 * writes the same number into the header; the two must not drift apart. */
#define EP_OPUS_FRAME 320
typedef struct {const uint8_t *table,*data;uint32_t size;uint16_t first,count;} ep_opus_pack_t;
typedef struct {const uint8_t *data;uint32_t bytes,samples;uint16_t skip;} ep_opus_clip_t;
bool ep_opus_open(ep_opus_pack_t *out,const void *bytes,size_t size,uint32_t content);
bool ep_opus_clip(const ep_opus_pack_t *pack,uint16_t id,ep_opus_clip_t *out);
bool ep_opus_packet(const ep_opus_clip_t *clip,uint32_t *position,const uint8_t **bytes,uint16_t *size);
