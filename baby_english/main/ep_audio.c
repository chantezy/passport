#include "ep_audio.h"
#include "ep_opus.h"
#include "content.h"
#include "esp_opus_dec.h"
#include "esp_heap_caps.h"
#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

/* One pack holds every clip this book has -- there is no second resource and no
 * external partition to mmap, so the app is self-contained. */
extern const uint8_t voice_start[] asm("_binary_voice_bin_start");
extern const uint8_t voice_end[] asm("_binary_voice_bin_end");
typedef struct { uint16_t clip; uint8_t volume; uint32_t epoch; } audio_job_t;
static const char *TAG="ep_audio";
static ep_opus_pack_t s_pack;
static void *s_decoder;
static QueueHandle_t s_queue;
static portMUX_TYPE s_mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_epoch;
static bool s_ready, s_busy, s_failed;

static bool current(uint32_t epoch) {
    portENTER_CRITICAL(&s_mux); bool same=s_epoch==epoch; portEXIT_CRITICAL(&s_mux); return same;
}
static void finish(uint32_t epoch, bool failed) {
    portENTER_CRITICAL(&s_mux);
    if(s_epoch==epoch) { s_busy=false; s_failed=failed; }
    portEXIT_CRITICAL(&s_mux);
}
static void audio_worker(void *unused) {
    (void)unused; audio_job_t job; int16_t pcm[EP_OPUS_FRAME];
    for(;;) {
        if(xQueueReceive(s_queue,&job,portMAX_DELAY)!=pdTRUE) continue;
        if(!current(job.epoch)) continue;
        if(!job.clip) { bsp_audio_set_volume(0); continue; }
        ep_opus_clip_t clip;
        if(!ep_opus_clip(&s_pack,job.clip,&clip)) {
            finish(job.epoch,true); continue;
        }
        if(esp_opus_dec_reset(s_decoder)!=ESP_AUDIO_ERR_OK){finish(job.epoch,true);continue;}
        bsp_audio_set_volume(job.volume);
        bool failed=false; uint32_t position=0,remaining=clip.samples,skip=clip.skip;
        while(current(job.epoch) && remaining) {
            const uint8_t *packet;uint16_t size;
            if(!ep_opus_packet(&clip,&position,&packet,&size)){failed=true;break;}
            esp_audio_dec_in_raw_t raw={.buffer=(uint8_t*)packet,.len=size};
            esp_audio_dec_out_frame_t frame={.buffer=(uint8_t*)pcm,.len=sizeof pcm};
            esp_audio_dec_info_t info={0};
            if(esp_opus_dec_decode(s_decoder,&raw,&frame,&info)!=ESP_AUDIO_ERR_OK || raw.consumed!=size || frame.decoded_size!=sizeof pcm || info.sample_rate!=16000 || info.channel!=1){failed=true;break;}
            unsigned n=frame.decoded_size/2,offset=skip<n?skip:n;skip-=offset;n-=offset;
            if(n>remaining)n=remaining;
            if(n && bsp_audio_write(pcm+offset,n*sizeof *pcm)!=ESP_OK){failed=true;break;}
            remaining-=n;
        }
        if(failed) ESP_LOGE(TAG,"Playback failed for clip %u",job.clip);
        /* Six 240-frame DMA descriptors: drain audible tail before clearing busy.
         * Polling the epoch keeps a replacement/cancel responsive. */
        for(unsigned i=0;i<10 && current(job.epoch) && !failed;++i) vTaskDelay(pdMS_TO_TICKS(10));
        finish(job.epoch,failed);
    }
}
bool ep_audio_init(void) {
    if(s_ready) return true;
    s_failed=true;
    /* The pack is immutable and embedded in the app's DROM segment: no external
     * Flash installation, no dependency on a previous firmware's resources. */
    if(!ep_opus_open(&s_pack,voice_start,(size_t)(voice_end-voice_start),CONTENT_VERSION) ||
       s_pack.first!=1 || s_pack.count!=AUDIO_COUNT) {
        ESP_LOGE(TAG,"Voice resources do not match this book build"); return false;
    }
    esp_opus_dec_cfg_t cfg={.sample_rate=16000,.channel=1,.frame_duration=ESP_OPUS_DEC_FRAME_DURATION_20_MS,.self_delimited=false};
    if(esp_opus_dec_open(&cfg,sizeof cfg,&s_decoder)!=ESP_AUDIO_ERR_OK){ESP_LOGE(TAG,"Opus decoder allocation failed");return false;}
    if(bsp_audio_init()!=ESP_OK || bsp_audio_set_format(16000,16,1)!=ESP_OK) {
        bsp_audio_deinit(); ESP_LOGE(TAG,"Audio hardware initialization failed"); return false;
    }
    s_queue=xQueueCreate(1,sizeof(audio_job_t));
    if(!s_queue) { bsp_audio_deinit(); return false; }
    if(xTaskCreate(audio_worker,"ep_audio",20480,NULL,4,NULL)!=pdPASS) {
        vQueueDelete(s_queue); s_queue=NULL; bsp_audio_deinit(); return false;
    }
    s_ready=true; s_failed=false;
    ESP_LOGI(TAG,"Validated %u local voice clips at 16000 Hz",s_pack.count);
    ESP_LOGI(TAG,"Heap after audio init: free=%u largest=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return true;
}
void ep_audio_play(uint16_t clip, uint8_t volume) {
    portENTER_CRITICAL(&s_mux);
    if(!s_ready) { s_failed=true; portEXIT_CRITICAL(&s_mux); return; }
    if(clip>AUDIO_COUNT) { s_failed=true; portEXIT_CRITICAL(&s_mux); return; }
    audio_job_t job={clip,volume>100?100:volume,++s_epoch};
    s_busy=clip!=0; if(clip) s_failed=false;
    /* Zero wait, single-slot overwrite; critical section serializes publication
     * with epoch/busy, including concurrent callers and worker completion. */
    if(xQueueOverwrite(s_queue,&job)!=pdPASS) { s_busy=false; s_failed=true; }
    portEXIT_CRITICAL(&s_mux);
}
bool ep_audio_busy(void) {
    portENTER_CRITICAL(&s_mux); bool value=s_busy; portEXIT_CRITICAL(&s_mux); return value;
}
bool ep_audio_failed(void) {
    portENTER_CRITICAL(&s_mux); bool value=s_failed; portEXIT_CRITICAL(&s_mux); return value;
}
