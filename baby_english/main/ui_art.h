#pragma once
#include "lvgl.h"
/* speaker -- fixed UI chrome, not content. */
typedef enum {UI_ART_SPEAKER,UI_ART_COUNT} ui_art_t;
const lv_image_dsc_t *ui_art_image(unsigned id);
