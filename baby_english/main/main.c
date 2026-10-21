#include "ep_core.h"
#include "ep_input.h"
#include "ep_audio.h"
#include "ui.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
static uint32_t now(void){return(uint32_t)(esp_timer_get_time()/1000);}
void app_main(void){
 static app_t a;app_init(&a,now());nvs_handle_t nvs=0;bool saved=false,storage_error=false;
 if(nvs_flash_init()==ESP_OK&&nvs_open("baby_english",NVS_READWRITE,&nvs)==ESP_OK){saved=true;static uint8_t b[16+1536];size_t z=sizeof b;esp_err_t e=nvs_get_blob(nvs,"progress",b,&z);if(e==ESP_OK&&!app_load(&a,b,z))ESP_LOGW("baby","Progress incompatible or corrupt; starting fresh");else if(e!=ESP_OK&&e!=ESP_ERR_NVS_NOT_FOUND)storage_error=true;}else storage_error=true;
 ESP_ERROR_CHECK(bsp_display_init());if(!bsp_lvgl_init())return;
 bool buttons=bsp_button_init(NULL,NULL)==ESP_OK;bool audio=ep_audio_init();ep_input_t input;ep_input_init(&input,now());
 uint32_t seq=a.audio_seq,progress=0,render=0,health=0,last_save=0;bool dim=false,pending_audio=false;unsigned bad=0;bsp_display_backlight(65);
 for(;;){uint32_t t=now();if(buttons){int mv=bsp_button_read_mv();bad=mv<0?bad+1:0;app_action(&a,ep_input_tick(&input,mv,t,a.dimmed),t);}app_tick(&a,t);
  if(pending_audio&&!ep_audio_busy()){pending_audio=false;app_audio_finished(&a,seq,audio&&!ep_audio_failed(),t);}
  if(seq!=a.audio_seq){seq=a.audio_seq;ep_audio_play(a.audio_id,a.volume);pending_audio=a.audio_id!=0;}
  if(a.progress_seq!=progress&&t-last_save>=1000){last_save=t;static uint8_t b[APP_SAVE_SIZE],c[APP_SAVE_SIZE];size_t z=app_save(&a,b,sizeof b),got=sizeof c;storage_error=!saved||nvs_set_blob(nvs,"progress",b,z)!=ESP_OK||nvs_commit(nvs)!=ESP_OK||nvs_get_blob(nvs,"progress",c,&got)!=ESP_OK||got!=z||memcmp(b,c,z);if(!storage_error)progress=a.progress_seq;}
  if(t-render>=80&&bsp_lvgl_lock(40)){ui_render(&a,!audio||ep_audio_failed(),storage_error,!buttons||bad>100);bsp_lvgl_unlock();render=t;}
  if(dim!=a.dimmed){dim=a.dimmed;bsp_display_backlight(dim?0:65);}
  if(t-health>=30000){health=t;ESP_LOGI("baby","page=%d heap=%lu min=%lu",a.page,(unsigned long)esp_get_free_heap_size(),(unsigned long)esp_get_minimum_free_heap_size());}
  vTaskDelay(pdMS_TO_TICKS(10));
 }
}
