#pragma once
#include <stdint.h>
#define WORD_COUNT 60
#define AUDIO_COUNT 60
#define UNIT_COUNT 6
#define CONTENT_VERSION 3092071670u
#define WORD_CONTENT_VERSION 628472539u
#define LEGACY_V1_COMPATIBLE 0
#define CONTENT_COMPLETE 0
#define PREVIOUS_WORD_COUNT 0
#define PREVIOUS_WORD_VERSION 0u
typedef struct {const char *en,*zh;uint16_t audio;} item_t;
typedef struct {const char *title;uint16_t word_start,word_count;} unit_t;
extern const item_t items[WORD_COUNT];
/* `units` groups the word list for the compiler, the illustration sheet and the
   content tests; nothing on screen shows a unit any more. */
extern const char *word_ipa[WORD_COUNT];
extern const unit_t units[UNIT_COUNT];
extern const uint16_t previous_word_indices[WORD_COUNT];
