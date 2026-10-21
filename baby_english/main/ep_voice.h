#ifndef EP_VOICE_H
#define EP_VOICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* EPAV v1: 32-byte LE header, 20-byte index entries, WAV IMA mono blocks.
 * The backing bytes must stay immutable and alive while a pack/clip is used. */
#define EP_VOICE_SAMPLE_RATE 16000u
#define EP_VOICE_BLOCK_BYTES 256u
#define EP_VOICE_BLOCK_SAMPLES 505u
typedef struct {
    const uint8_t *bytes, *table, *payload;
    size_t size;
    uint32_t payload_size;
    uint16_t count;
} ep_voice_pack_t;
typedef struct {
    const uint8_t *data;
    uint32_t size, samples;
} ep_voice_clip_t;
typedef struct {
    ep_voice_clip_t clip;
    uint32_t position;
    int32_t predictor;
    int16_t step_index;
    bool failed;
} ep_voice_decoder_t;

/* Validates exact length, contiguous IDs/offsets, both CRCs and all IMA headers. */
bool ep_voice_open(ep_voice_pack_t *out, const void *bytes, size_t size);
bool ep_voice_clip(const ep_voice_pack_t *pack, uint16_t id, ep_voice_clip_t *out);
bool ep_voice_decoder_init(ep_voice_decoder_t *out, const ep_voice_clip_t *clip);
/* Returns 0 at EOF or error; inspect decoder.failed to distinguish. */
size_t ep_voice_decode(ep_voice_decoder_t *decoder, int16_t *pcm, size_t capacity);
uint32_t ep_voice_crc32(const void *bytes, size_t size);
#endif
