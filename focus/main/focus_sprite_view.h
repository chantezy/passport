// main/focus_sprite_view.h —— 计时页主视觉那一格位图槽(只做 LVGL,不做查表计算)。
//
// 那一格上先后会出现三种画面:被一口口吃掉的水果、专注结束那张卡、休息中轮换的
// 睡觉三帧。三种从不同时出现,所以它们共用同一个 lv_image —— 各建一个对象就是
// 三份约 380 字节的 LVGL 堆,而这块板子的 LVGL 堆一共只有 24 KB。
//
// 挑哪一只水果、画到第几档在 focus_fruit_math.{h,c},像素在 focus_sprites.{h,c},
// 那边都是纯逻辑/纯数据、可主机测试;这里只把位图挂到那个图像对象上。
#pragma once

#include <stdint.h>

#include "lvgl.h"
#include "focus_fruit_math.h"
#include "focus_sprites.h"

typedef struct {
    lv_obj_t *img;    // 唯一的图像对象,按当前那张图居中放在外接框里
    int32_t box_x;    // 外接框(FOCUS_FRUIT_BOX 见方)左上角在屏幕上的位置
    int32_t box_y;
    uint16_t shown;   // 当前已应用的槽位序号(0xFFFF = 还没应用过)
} focus_sprite_view_t;

// 在 parent 的 (x, y) 处建出这一格。这个点是 FOCUS_FRUIT_BOX 见方的外接框左上角,
// 图在框内居中,所以换图不会让画面的中心跑掉。建完立刻是"0 号水果、整只"。
void focus_sprite_view_create(focus_sprite_view_t *view, lv_obj_t *parent, int32_t x, int32_t y);

// 显示 (水果, 阶段)。水果序号越界按 0 处理,阶段越界按最后一档处理。
// 画面没变时一个对象都不碰(每 100ms 的心跳都会调用它,不能有额外开销)。
void focus_sprite_view_show_fruit(focus_sprite_view_t *view, uint8_t fruit, uint8_t stage);

// 显示水果之外的那几张:专注结束卡与睡觉三帧(FOCUS_PIC_* 见 focus_sprites.h)。
// 序号越界按最后一张处理。
void focus_sprite_view_show_pic(focus_sprite_view_t *view, uint8_t pic);
