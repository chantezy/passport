#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "content.h"
#define APP_SAVE_SIZE (16u+WORD_COUNT)
typedef enum { EP_INPUT_NONE, EP_UP, EP_DOWN, EP_OK, EP_BACK, EP_WAKE, EP_LONG } ep_input_event_t;
/* HOME is the picture: a short press starts the cards, a long press opens
   SETTINGS, and SETTINGS holds the only way into LICENSE. */
typedef enum { HOME, WORD, SETTINGS, LICENSE } page_t;
typedef struct {
 page_t page;
 /* focus drives the list pages only (SETTINGS / LICENSE); a card has no
    highlight because each of its three buttons is a key of its own. */
 uint16_t focus,index,notice_page;
 uint16_t queue[WORD_COUNT],queue_count,queue_pos;
 uint32_t random;
 uint8_t volume;
 /* Nothing writes `mastery` any more -- the notebook is gone -- but the field
    stays so the save blob keeps the exact size and shape this book has always
    written, and a device's existing volume survives the upgrade. */
 uint8_t mastery[WORD_COUNT];
 bool dimmed,playing;
 uint32_t last_input,audio_seq,progress_seq;
 uint16_t audio_id;
} app_t;
void app_init(app_t *a,uint32_t now);
void app_action(app_t *a,ep_input_event_t e,uint32_t now);
void app_tick(app_t *a,uint32_t now);
size_t app_save(const app_t *a,uint8_t *out,size_t cap);
bool app_load(app_t *a,const uint8_t *in,size_t n);
void app_audio_finished(app_t *a,uint32_t sequence,bool success,uint32_t now);
