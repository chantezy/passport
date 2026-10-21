// main/focus_ui.h
// 番茄钟的界面层:深色底 + 一格主视觉(被吃掉的水果 / 专注结束卡 / 睡觉三帧)+ 倒计时。
//
// 界面只读模型、不改模型。所有 lv_* 调用必须在 bsp_lvgl_lock() 里进行。
// 本层不碰 NVS、按键、功耗,也不 include 任何 BSP 头文件 —— 电量数值由调用方
// 传进来,这样界面层可以独立于硬件被复用与阅读。
#pragma once

#include "lvgl.h"
#include "focus_model.h"
#include "focus_sprite_view.h"

#include <stdbool.h>
#include <stdint.h>

// 电量未知时的显示(读不到电量计)。
#define FOCUS_UI_BATTERY_UNKNOWN (-1)

// 首页时长选择器那两颗按钮图各自的位。focus_ui_refresh 的 press_flash 参数收的是
// 这两个位的按位或:置位的那颗按钮图会被压暗一档,用来表示"这颗键此刻是按下的"。
// 0 表示两颗都是常态。**哪颗键对应哪颗图由调用方判断** —— 界面层不认识键,
// 也不该认识(见本文件顶部:界面只读模型、不碰按键)。
#define FOCUS_UI_FLASH_MINUS 0x01u
#define FOCUS_UI_FLASH_PLUS  0x02u

// 计时页那一格主视觉此刻该画哪一张。它同时是"该重绘了吗"的依据:返回值一变就重绘,
// 所以休息中那三帧的动画不需要另开定时器(见 focus_app.c 的 tick_cb)。
typedef enum {
    FOCUS_SCENE_FRUIT = 0,  // 待开始 / 专注中:那只被一口口吃掉的水果
    FOCUS_SCENE_DONE,       // 专注刚走完:合眼比勾的那张,底下一句"专注结束"
    FOCUS_SCENE_SLEEP_0,    // 休息中:睡觉三帧,按已走过的时长轮换
    FOCUS_SCENE_SLEEP_1,
    FOCUS_SCENE_SLEEP_2,
} focus_scene_t;

typedef struct {
    // 首页:标题 + 专注时长选择器
    lv_obj_t *scr_home;
    lv_obj_t *home_brand;    // 像素字 "Focus"(o 是那颗番茄,见 focus_sprites.h)
    lv_obj_t *home_row;      // 时长选择器那一行,横向 flex:减按钮 / 数值 / 加按钮
    lv_obj_t *home_minus;    // 减号按钮图(assets/sprites/focus_buttons.c)
    lv_obj_t *home_minutes;  // 专注时长,像素大字
    lv_obj_t *home_plus;     // 加号按钮图
    lv_obj_t *home_hint;     // "单击开始 · 长按设置"
    lv_obj_t *home_battery;

    // 计时页
    lv_obj_t *scr_timer;
    lv_obj_t *lbl_phase;
    lv_obj_t *lbl_battery;
    focus_sprite_view_t scene;  // 主视觉:水果 / 结束卡 / 睡觉三帧共用一个图像对象
    lv_obj_t *lbl_time;
    lv_obj_t *lbl_status;
    lv_obj_t *dot_row;
    lv_obj_t *dot[FOCUS_MAX_ROUNDS];
    uint8_t dot_count;  // dot[] 中此刻实际存在的点数

    // 上一次画的主视觉是哪一张(focus_scene_t)。只有它变了才需要重排结束卡那一屏
    // 的两行文字 —— 每次刷新都调一遍 lv_obj_align 会把整屏的布局反复标脏,而这一格
    // 每 100ms 就被刷新一次。0 是 FOCUS_SCENE_FRUIT,正好等于建屏时的布局。
    uint8_t shown_scene;

    lv_obj_t *lbl_hint;  // 底栏唯一的一行操作提示

    // 设置页
    lv_obj_t *scr_settings;
    lv_obj_t *set_row[FOCUS_SETTING_COUNT];
    lv_obj_t *set_val[FOCUS_SETTING_COUNT];

    // 底栏那一行。它随光标走:停在值行上写"单击修改",停在最后那个动作行上写
    // "单击恢复"(见 focus_ui.c 的 SETTINGS_HINT_*)。和 shown_scene 同理,缓存
    // 上一次是哪种,只在光标真的换了行时才去动这个标签 —— 否则计时页在跑的时候,
    // 这一页每 100ms 就被刷新一次,每次都对着同一个字符串重排一遍。
    lv_obj_t *lbl_set_hint;
    bool set_hint_is_reset;
} focus_ui_t;

// 建好三个页面。默认显示首页。必须在 bsp_lvgl_lock() 内调用。
// 可重复调用:第二次起直接返回,不再重复创建对象。
void focus_ui_create(focus_ui_t *ui);

// 按当前模型状态刷新全部可见内容。必须在 bsp_lvgl_lock() 内调用。
// battery_percent 传 0..100,或 FOCUS_UI_BATTERY_UNKNOWN。
// press_flash 是 FOCUS_UI_FLASH_MINUS / FOCUS_UI_FLASH_PLUS 的按位或:置位的那颗
// 按钮图被压暗,表示这颗键此刻按着。它和别的内容是两回事 —— 前者是"手正按着"的
// 瞬时反馈,后者是模型的状态,所以它不走模型、直接由调用方传进来。
void focus_ui_refresh(focus_ui_t *ui, const focus_model_t *m, int64_t now_ms,
                      int battery_percent, uint8_t press_flash);

// 计时页那一格现在该画哪一张。纯读模型,不碰任何对象,调用方可以在锁外调它 ——
// 心跳靠它的变化决定要不要重绘(见 focus_ui.h 顶部关于 focus_scene_t 的说明)。
focus_scene_t focus_ui_scene(const focus_model_t *m, int64_t now_ms);
