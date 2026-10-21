// 水果挑选与阶段推进的 host 测试。这两件事在屏幕上都"看着差不多对"—— 抽到重复的
// 水果只会让人觉得巧,阶段切早了半分钟也没什么感觉 —— 所以用断言把它钉住,
// 而不是靠肉眼。位图本身的检查在 tests/test_focus_sprites.c。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "focus_fruit_math.h"

// 五档均分,每档 200‰。
#define STEP (1000 / FOCUS_FRUIT_STAGE_COUNT)

int main(void)
{
    // ---- 阶段:均分的边界 ----
    assert(focus_fruit_permille_to_stage(-500) == 0);
    assert(focus_fruit_permille_to_stage(0) == 0);
    assert(focus_fruit_permille_to_stage(STEP - 1) == 0);
    assert(focus_fruit_permille_to_stage(STEP) == 1);          // 20% 咬第一口
    assert(focus_fruit_permille_to_stage(2 * STEP - 1) == 1);
    assert(focus_fruit_permille_to_stage(2 * STEP) == 2);      // 40%
    assert(focus_fruit_permille_to_stage(3 * STEP) == 3);      // 60%
    assert(focus_fruit_permille_to_stage(4 * STEP) == 4);      // 80% 只剩一点
    assert(focus_fruit_permille_to_stage(999) == 4);
    assert(focus_fruit_permille_to_stage(1000) == FOCUS_FRUIT_STAGE_COUNT - 1);
    assert(focus_fruit_permille_to_stage(2000000000) == FOCUS_FRUIT_STAGE_COUNT - 1);  // 不许溢出

    // ---- 阶段:单调不减、不越界、走到头一定是最后一档 ----
    int prev = 0;
    for (int permille = -10; permille <= 1010; permille++) {
        const int stage = focus_fruit_permille_to_stage(permille);
        assert(stage >= prev);
        assert(stage >= 0 && stage < FOCUS_FRUIT_STAGE_COUNT);
        prev = stage;
    }
    assert(prev == FOCUS_FRUIT_STAGE_COUNT - 1);

    // ---- 挑水果:永远不等于上一只,且落回范围内 ----
    for (uint32_t rnd = 0; rnd < 200; rnd++) {
        for (uint8_t previous = 0; previous < FOCUS_FRUIT_COUNT; previous++) {
            const uint8_t picked = focus_fruit_pick(previous, rnd);
            assert(picked < FOCUS_FRUIT_COUNT);
            assert(picked != previous);
        }
    }
    // previous 越界时(还没抽过第一只)只按随机数取,不读越界内存。
    for (uint32_t rnd = 0; rnd < 20; rnd++) {
        assert(focus_fruit_pick(255, rnd) == rnd % FOCUS_FRUIT_COUNT);
    }
    // 随机数均匀走过一圈时,每种水果都抽得到。
    bool seen[FOCUS_FRUIT_COUNT] = {false};
    for (uint32_t rnd = 0; rnd < FOCUS_FRUIT_COUNT; rnd++) {
        seen[focus_fruit_pick(255, rnd)] = true;
    }
    for (uint8_t i = 0; i < FOCUS_FRUIT_COUNT; i++) {
        assert(seen[i]);
    }

    // ---- 名字:只进日志,必须是 ASCII 且互不相同 ----
    for (uint8_t fruit = 0; fruit < FOCUS_FRUIT_COUNT; fruit++) {
        const char *name = focus_fruit_name(fruit);
        assert(name != NULL && name[0] != '\0');
        for (const char *c = name; *c != '\0'; c++) {
            assert(*c > 0x20 && *c < 0x7F);
        }
        for (uint8_t other = (uint8_t)(fruit + 1); other < FOCUS_FRUIT_COUNT; other++) {
            assert(name != focus_fruit_name(other));
            assert(name[0] != focus_fruit_name(other)[0]);
        }
    }

    // 序号越界一律夹到 0 号,而不是读越界内存(界面与应用层都可能传错)。
    assert(focus_fruit_name(FOCUS_FRUIT_COUNT) == focus_fruit_name(0));
    assert(focus_fruit_name(255) == focus_fruit_name(0));

    puts("focus fruit math tests: PASS");
    return 0;
}
