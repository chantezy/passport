// main/focus_store.c —— 见 focus_store.h 的说明。
#include "focus_store.h"

#include <stddef.h>
#include <string.h>

#include "esp_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "focus_store";

#define FOCUS_STORE_NAMESPACE "focus"
#define FOCUS_STORE_KEY "state"
#define FOCUS_STORE_LOCK_TIMEOUT_MS 500

// 存档 = 长度 + 状态 + CRC32。
//
// 为什么要自带 CRC:NVS 每条记录内部也有校验,但那是 NVS 的实现细节;"这份存档
// 是否可信"应该写在存档自己身上,换存储后端、换写入路径都不会丢掉这层保护。
// 前 4 字节的长度是给"改了 focus_persist_t 却忘了升版本"留的兜底 —— 布局一变,
// 长度立刻对不上,旧数据会被判为损坏而不是被当成新布局解读。
typedef struct {
    uint32_t len;
    focus_persist_t state;
    uint32_t crc;
} focus_store_blob_t;

static nvs_handle_t s_handle;
static SemaphoreHandle_t s_lock;
static bool s_ready;

static uint32_t blob_crc(const focus_store_blob_t *b)
{
    // 只覆盖 crc 之前的字节:长度字段也参与校验,否则改长度就能骗过校验。
    return esp_crc32_le(0, (const uint8_t *)&b->len, offsetof(focus_store_blob_t, crc));
}

esp_err_t focus_store_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 本应用是 NVS 分区的唯一使用者(分区表里没有别的 data 分区)。
        // 分区不可用时只能重建,别的选择是永久失去落盘能力。
        ESP_LOGW(TAG, "NVS 分区需要重建: %s", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败,本次运行不落盘: %s", esp_err_to_name(err));
        return err;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            ESP_LOGE(TAG, "建互斥量失败");
            return ESP_ERR_NO_MEM;
        }
    }

    err = nvs_open(FOCUS_STORE_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 命名空间失败: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    return ESP_OK;
}

bool focus_store_load(focus_model_t *m)
{
    if (!s_ready || m == NULL) {
        return false;
    }

    focus_store_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t len = sizeof(blob);

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(FOCUS_STORE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "取锁超时,跳过读取");
        return false;
    }
    esp_err_t err = nvs_get_blob(s_handle, FOCUS_STORE_KEY, &blob, &len);
    xSemaphoreGive(s_lock);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "没有历史存档,按默认配置启动");
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "读取存档失败,按默认配置启动: %s", esp_err_to_name(err));
        return false;
    }
    if (len != sizeof(blob) || blob.len != (uint32_t)sizeof(focus_persist_t)) {
        ESP_LOGW(TAG, "存档长度不符(读回 %u,期望 %u),丢弃", (unsigned)len,
                 (unsigned)sizeof(blob));
        return false;
    }
    if (blob.crc != blob_crc(&blob)) {
        ESP_LOGW(TAG, "存档校验和不符,丢弃");
        return false;
    }
    if (!focus_model_import(m, &blob.state)) {
        ESP_LOGW(TAG, "存档字段越界或版本不符,丢弃");
        return false;
    }

    ESP_LOGI(TAG, "已恢复上次状态:阶段=%s 运行态=%d 剩余=%u 秒",
             focus_phase_name(m->phase), (int)m->run, (unsigned)(m->remaining_ms / 1000u));
    return true;
}

esp_err_t focus_store_save(const focus_model_t *m, int64_t now_ms)
{
    if (!s_ready || m == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    focus_store_blob_t blob;
    // 整块清零而不只是逐字段赋值:focus_persist_t 末尾有对齐填充字节,它们也会
    // 被写进 NVS 并参与 CRC。不先归零的话,填充字节带着上一次的栈内容进来,
    // 同一份状态会算出两种校验和。
    memset(&blob, 0, sizeof(blob));
    focus_model_export(m, now_ms, &blob.state);
    blob.len = (uint32_t)sizeof(focus_persist_t);
    blob.crc = blob_crc(&blob);

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(FOCUS_STORE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "取锁超时,状态未落盘");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = nvs_set_blob(s_handle, FOCUS_STORE_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(s_handle);
    }
    xSemaphoreGive(s_lock);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "状态落盘失败: %s", esp_err_to_name(err));
    }
    return err;
}
