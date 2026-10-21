// main/focus_sprites.h —— 应用直接画的那些像素图:水果、专注结束卡、睡觉三帧。
//
// 数据本身是生成的(见 tools/gen_sprites.py、tools/gen_scenes.py 与
// assets/README.md),这里只声明形状和查表入口。刻意不引用 LVGL:像素数组是纯数据,
// LVGL 的图像描述符在 focus_sprite_view.c 里现搭 —— 这样主机测试可以直接把这些
// 数组编进来断言尺寸,不用在测试里拉起整个 LVGL。
//
// 像素是 **RGB565、已合成到纯黑底**。屏幕底色就是 #000000(见 focus_ui.c 的
// COL_BG),所以透明边合到黑底之后不再需要 alpha 通道:每像素 2 字节而不是 3 或
// 4 字节,整个精灵表因此少了约 0.55 MB Flash。
#pragma once

#include <stdint.h>

#include "focus_fruit_math.h"

// 一张位图:px 指向 w*h 个 RGB565,按行优先、小端。
// LVGL 在内存里就是按小端读 RGB565 的(ST7789 的大端由 BSP 在 flush 时交换,
// 见 components/bsp/src/bsp_display_lvgl.c 的 swap_bytes),所以这里直接存
// uint16_t 数值即可,不需要预先换字节。
typedef struct {
    const uint16_t *px;
    uint16_t w;
    uint16_t h;
} focus_sprite_t;

// 首页标题的像素字 "Focus"(assets/sprites/focus_wordmark.c,由 tools/gen_brand.py
// 生成)。它不是水果,只是一张同样形状的位图,所以复用 focus_sprite_t。
extern const focus_sprite_t focus_wordmark;

// 首页时长选择器两侧的步进按钮(assets/sprites/focus_buttons.c,由
// tools/gen_buttons.py 从取材图切出来)。它们原来是用 48px 字体的 "-" / "+" 画
// 的,换成图之后那两个字形就从字库里删掉了 —— 同一件事有两种画法时,先删掉不再
// 用的那一种。两张都是 56 宽,高按素材比例。
extern const focus_sprite_t focus_button_minus;
extern const focus_sprite_t focus_button_plus;

// 水果之外,计时页那一格还会出现这几张(assets/sprites/focus_scenes.c,由
// tools/gen_scenes.py 从两张素材图切出来)。顺序即 focus_sprite_view 的槽位序号。
typedef enum {
    FOCUS_PIC_DONE = 0,   // 专注刚走完:合眼比了个勾的那张
    FOCUS_PIC_SLEEP_0,    // 休息中轮换的三帧:嘴边的 z 一个比一个大
    FOCUS_PIC_SLEEP_1,
    FOCUS_PIC_SLEEP_2,
    FOCUS_PIC_COUNT,
} focus_scene_pic_t;

extern const focus_sprite_t focus_scene_done;
extern const focus_sprite_t focus_scene_sleep[FOCUS_PIC_COUNT - FOCUS_PIC_SLEEP_0];

// 每种水果一张五级的表,由 assets/sprites/focus_fruit_<名字>.c 生成。
extern const focus_sprite_t focus_fruit_banana[FOCUS_FRUIT_STAGE_COUNT];
extern const focus_sprite_t focus_fruit_watermelon[FOCUS_FRUIT_STAGE_COUNT];
extern const focus_sprite_t focus_fruit_pineapple[FOCUS_FRUIT_STAGE_COUNT];
extern const focus_sprite_t focus_fruit_tomato[FOCUS_FRUIT_STAGE_COUNT];
extern const focus_sprite_t focus_fruit_carrot[FOCUS_FRUIT_STAGE_COUNT];
extern const focus_sprite_t focus_fruit_apple[FOCUS_FRUIT_STAGE_COUNT];

// 取 (水果, 阶段) 对应的位图。两个序号都越界时不读越界内存:水果夹到 0 号,
// 阶段夹到最后一档(而不是第 0 档 —— 越界时"果子快吃完了"比"果子凭空长回来"更
// 不容易让人以为是 bug)。
const focus_sprite_t *focus_fruit_sprite(uint8_t fruit, uint8_t stage);

// 全部 6 种水果的表,便于遍历校验(序号与 focus_fruit_name() 一致)。
const focus_sprite_t *const *focus_fruit_sprite_tables(void);
