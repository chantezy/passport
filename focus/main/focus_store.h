// main/focus_store.h —— 番茄钟状态的 NVS 落盘。
//
// 只负责"把一份 focus_persist_t 存下去、再取回来",不碰界面、按键与电源。
// NVS 写入可能触发擦页而阻塞几十毫秒,所以本层不可以在按键回调里调用;
// 本应用只在 LVGL 的 tick 回调与休眠前的功耗任务里落盘,两者由本层的互斥量
// 串行化,调用方不必自己加锁。
#pragma once

#include "esp_err.h"
#include "focus_model.h"

#include <stdbool.h>
#include <stdint.h>

// 打开 NVS 分区并建好内部互斥量。成功可重复调用。
// 失败后 load/save 一律安全失败(而不是崩),调用方按"无存档"继续跑。
esp_err_t focus_store_init(void);

// 读取上次状态并校验。有存档且通过校验时返回 true 并覆写 *m。
// 无存档、长度/CRC 不符或字段越界时返回 false 且不改动 *m —— 调用方保留
// 已经初始化好的默认配置,不需要另写一套兜底。
bool focus_store_load(focus_model_t *m);

// 把当前状态落盘。now_ms 用来把 RUNNING 折算成剩余秒数(绝对截止时刻跨不过
// 一次重启,深睡同样是重启)。
esp_err_t focus_store_save(const focus_model_t *m, int64_t now_ms);
