// main/focus_chime.c —— 见 focus_chime.h 的说明。
#include "focus_chime.h"

#include "bsp_audio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdint.h>
#include <string.h>

static const char *TAG = "focus_chime";

// BSP 音频最低支持到 16 kHz,单声道 16 位是 bsp_audio_write 的默认口径。
#define CHIME_HZ 16000u
#define CHIME_BITS 16u
#define CHIME_CHANNELS 1u
#define CHIME_VOLUME 70u

#define CHIME_TASK_STACK 4096
#define CHIME_TASK_PRIO 4
#define CHIME_QUEUE_DEPTH 2

// 一次播放的实际长度上限,quiesce 的等待上限按它取整。
#define CHIME_MAX_MS 1500u

// 音符之间的包络(相对每个音符自己的长度,单位是毫秒)。
// 起音短、落音长:落音够长才不会在音符末尾听到"啪"的一声。
#define CHIME_ATTACK_MS 12u
#define CHIME_RELEASE_MS 70u

// 峰值幅度取满量程的 40%,既能听清又不至于在静夜刺耳。
#define CHIME_AMPLITUDE 13000

// 一次写进去的采样数。256 个 16 位采样 = 512 字节,静态缓冲区就只占这么多,
// 播放任务的 4 KB 栈也放得下。
#define CHIME_CHUNK_FRAMES 256u

typedef struct {
    uint16_t hz;
    uint16_t ms;
} chime_note_t;

// C6 - E6 - G6 三个上行音,落在人耳最舒服的中高音区,总长 520 ms。
static const chime_note_t NOTES[] = {
    {1047, 150},
    {1319, 150},
    {1568, 220},
};
#define NOTE_COUNT (sizeof(NOTES) / sizeof(NOTES[0]))

// 256 点正弦表,定点整数,避免在播放循环里引浮点库。
// 相位用一个 16.16 的小数部分累加,索引取高 8 位。
#define SINE_BITS 8
#define SINE_SIZE (1u << SINE_BITS)
static const int16_t SINE[SINE_SIZE] = {
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
      6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
     12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
     23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
     27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
     32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
     32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
     30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
     27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
     18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
     12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
         0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,
};

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_audio_lock;
static bool s_audio_ready;
// 播放任务的写入循环与 quiesce 之间靠下面三个标志握手,都在任务上下文里读写,
// 但写者可能是另一个任务,故标 volatile。
static volatile bool s_busy;       // 此刻正在出声
static volatile bool s_abort;      // 要求当前这一声立刻收尾
static volatile bool s_suspended;  // 已经交给休眠流程,此后不再出声

// 把一个音符按包络渲染并写进 I2S。返回 false 表示写入失败或中途被要求停止。
static bool play_note(const chime_note_t *note)
{
    static int16_t buf[CHIME_CHUNK_FRAMES];

    // step 的单位是 1/256 个周期 × 2^16。用 64 位先乘再除,免得 hz 一高就溢出。
    const uint32_t step =
        (uint32_t)(((uint64_t)note->hz << (SINE_BITS + 16)) / CHIME_HZ);
    const uint32_t total = (uint32_t)(((uint32_t)note->ms * CHIME_HZ) / 1000u);
    const uint32_t attack = (CHIME_ATTACK_MS * CHIME_HZ) / 1000u;
    const uint32_t release = (CHIME_RELEASE_MS * CHIME_HZ) / 1000u;

    uint32_t phase = 0;
    for (uint32_t done = 0; done < total;) {
        if (s_abort) {
            return false;
        }
        uint32_t chunk = total - done;
        if (chunk > CHIME_CHUNK_FRAMES) {
            chunk = CHIME_CHUNK_FRAMES;
        }
        for (uint32_t i = 0; i < chunk; i++) {
            const uint32_t pos = done + i;
            int32_t amp = CHIME_AMPLITUDE;
            if (pos < attack) {
                amp = (amp * (int32_t)pos) / (int32_t)attack;
            } else if (total - pos < release) {
                amp = (amp * (int32_t)(total - pos)) / (int32_t)release;
            }
            buf[i] = (int16_t)(((int32_t)SINE[(phase >> 16) & (SINE_SIZE - 1)] * amp) >> 15);
            phase += step;
        }
        if (bsp_audio_write(buf, chunk * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGW(TAG, "写入 I2S 失败,本次提示音中止");
            return false;
        }
        done += chunk;
    }
    return true;
}

static void chime_task(void *arg)
{
    (void)arg;
    uint8_t cmd;
    for (;;) {
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (s_suspended || !s_audio_ready) {
            continue;
        }
        if (xSemaphoreTake(s_audio_lock, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        // 拿到锁之后再判一次:suspended 可能是在等待锁的过程中被设上的。
        if (!s_suspended && !s_abort) {
            s_busy = true;
            for (size_t i = 0; i < NOTE_COUNT; i++) {
                if (!play_note(&NOTES[i])) {
                    break;
                }
            }
            s_busy = false;
        }
        s_abort = false;
        xSemaphoreGive(s_audio_lock);
    }
}

esp_err_t focus_chime_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }

    s_audio_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(CHIME_QUEUE_DEPTH, sizeof(uint8_t));
    if (s_audio_lock == NULL || s_queue == NULL) {
        ESP_LOGE(TAG, "建队列/互斥量失败,提示音不可用");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(chime_task, "focus_chime", CHIME_TASK_STACK, NULL, CHIME_TASK_PRIO, NULL) !=
        pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        ESP_LOGE(TAG, "建提示音任务失败,提示音不可用");
        return ESP_ERR_NO_MEM;
    }

    // 格式要在播放之前定好:bsp_audio_set_format 会在格式变化时先 close 再 open,
    // 播放中调用会把正在放的 PCM 打断。
    esp_err_t err = bsp_audio_set_format(CHIME_HZ, CHIME_BITS, CHIME_CHANNELS);
    if (err != ESP_OK) {
        // 队列已经建好,调用方不需要为"能不能出声"写分支:play 会静默丢弃。
        ESP_LOGW(TAG, "音频格式设置失败,提示音降级为静音: %s", esp_err_to_name(err));
        return ESP_OK;
    }
    bsp_audio_set_volume(CHIME_VOLUME);
    s_audio_ready = true;
    ESP_LOGI(TAG, "提示音就绪:%u Hz / %u bit / %u 声道,音量 %u%%", (unsigned)CHIME_HZ,
             (unsigned)CHIME_BITS, (unsigned)CHIME_CHANNELS, (unsigned)CHIME_VOLUME);
    return ESP_OK;
}

void focus_chime_play(void)
{
    if (s_queue == NULL || s_suspended || !s_audio_ready) {
        return;
    }
    const uint8_t cmd = 1;
    (void)xQueueSend(s_queue, &cmd, 0);
}

bool focus_chime_busy(void)
{
    return s_busy;
}

void focus_chime_quiesce(void)
{
    if (s_queue == NULL) {
        return;
    }
    // 顺序要紧:先禁止再出声(suspended),再打断当前这一声(abort),
    // 最后清掉还排着的请求。反过来的话,清完队列可能又被塞进新的一声。
    s_suspended = true;
    s_abort = true;
    xQueueReset(s_queue);

    // 拿到锁就说明播放循环已经退出。取不到也不阻塞休眠 —— 真出不来就带着
    // 一次未播完的写入进休眠,总好过卡在这里醒不了。
    if (s_audio_lock != NULL) {
        if (xSemaphoreTake(s_audio_lock, pdMS_TO_TICKS(CHIME_MAX_MS)) == pdTRUE) {
            xSemaphoreGive(s_audio_lock);
        } else {
            ESP_LOGW(TAG, "提示音未在 %u ms 内停止", (unsigned)CHIME_MAX_MS);
        }
    }
    s_busy = false;
}
