#pragma once
#include "ep_core.h"
#include "lvgl.h"
void ui_render(const app_t*a,bool audio_error,bool storage_error,bool input_error);
lv_obj_t *ui_screen(void);
