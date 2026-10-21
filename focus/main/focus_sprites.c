// main/focus_sprites.c —— 见同名 .h。这里只有一张指针表和一个夹边界的查表函数。
#include "focus_sprites.h"

// 顺序必须与 focus_fruit_math.c 的 FRUIT_NAMES 一致:序号即下标,改这里等于改
// 存档里 fruit 字节的含义。数组故意不写长度,下面那条 _Static_assert 就会在少写
// 或多种一种水果时直接编译失败,而不是留一个 NULL 指针到运行时才炸。
static const focus_sprite_t *const TABLES[] = {
    focus_fruit_banana,      // 0
    focus_fruit_watermelon,  // 1
    focus_fruit_pineapple,   // 2
    focus_fruit_tomato,      // 3
    focus_fruit_carrot,      // 4
    focus_fruit_apple,       // 5
};

_Static_assert(sizeof(TABLES) / sizeof(TABLES[0]) == FOCUS_FRUIT_COUNT,
               "TABLES must hold exactly one sprite table per fruit");

const focus_sprite_t *focus_fruit_sprite(uint8_t fruit, uint8_t stage)
{
    if (fruit >= FOCUS_FRUIT_COUNT) {
        fruit = 0;
    }
    // 阶段越界夹到最后一档,而不是第 0 档:越界时"果子快吃完了"比"果子凭空长回来"
    // 更不容易让人以为是 bug。
    if (stage >= FOCUS_FRUIT_STAGE_COUNT) {
        stage = (uint8_t)(FOCUS_FRUIT_STAGE_COUNT - 1);
    }
    return &TABLES[fruit][stage];
}

const focus_sprite_t *const *focus_fruit_sprite_tables(void)
{
    return TABLES;
}
