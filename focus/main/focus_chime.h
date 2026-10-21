// main/focus_chime.h —— 阶段走完时的提示音。
//
// 波形在运行时合成(256 点正弦表 + 逐音符包络),不占 Flash 也不占静态 RAM 缓冲;
// 播放放在专职任务里,调用方(按键回调或 LVGL tick)只入队,永不阻塞。
//
// 提示音是锦上添花:BSP 音频初始化失败时本模块照样建队列,只是不出声 ——
// 一个坏掉的 codec 不应该让计时器起不来。
#pragma once

#include "esp_err.h"

#include <stdbool.h>

// 建队列、设好 I2S 采样格式并起播放任务。成功可重复调用。
// 音频不可用时返回 ESP_OK 但日志里会写明降级为静音。
esp_err_t focus_chime_init(void);

// 请求播放一次提示音。非阻塞:队列满时丢弃本次请求 ——
// 宁可少响一声,也不让按键回调卡在队列上。
void focus_chime_play(void);

// 此刻是否正在出声。休眠前用它决定要不要再等一会儿。
bool focus_chime_busy(void);

// 让提示音任务彻底安静下来,并保证此后不再触碰音频硬件。休眠前调用。
// 会打断正在播放的一声,最长阻塞约 1.5 秒;调用后本模块不再出声。
void focus_chime_quiesce(void);
