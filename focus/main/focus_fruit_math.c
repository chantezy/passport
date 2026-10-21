// main/focus_fruit_math.c —— 水果序号表与几个纯函数,见同名 .h。
//
// 这个文件刻意不引用 LVGL / ESP-IDF / FreeRTOS:它要能在主机上被直接编译并断言,
// 所以随机源也由调用方传进来,而不是在这里调 esp_random()。
//
// 果子的形状、咬痕都不在这里算 —— 那是素材里画好的五档像素图,见 focus_sprites.*。
#include "focus_fruit_math.h"

// 序号即下标,顺序必须与 main/focus_sprites.c 的 TABLES 一致。
// 名字只进日志,必须是 ASCII:它不渲染,所以不该让字库覆盖检查为它拦下一堆字形。
static const char *const FRUIT_NAMES[FOCUS_FRUIT_COUNT] = {
    "banana",
    "watermelon",
    "pineapple",
    "tomato",
    "carrot",
    "apple",
};

static uint8_t clamp_fruit(uint8_t fruit)
{
    return fruit < FOCUS_FRUIT_COUNT ? fruit : 0;
}

const char *focus_fruit_name(uint8_t fruit)
{
    return FRUIT_NAMES[clamp_fruit(fruit)];
}

uint8_t focus_fruit_pick(uint8_t previous, uint32_t rnd)
{
    uint8_t picked = (uint8_t)(rnd % (uint32_t)FOCUS_FRUIT_COUNT);

    // 不和上一次重复。"上一轮是 X,这一轮又抽到 X"在只有几种水果时观感上很显眼,
    // 而且用户能看出来 —— 宁可让紧邻的下一种多担一点概率,也不要连着两次一样。
    if (previous < FOCUS_FRUIT_COUNT && picked == previous) {
        picked = (uint8_t)((picked + 1u) % (uint32_t)FOCUS_FRUIT_COUNT);
    }
    return picked;
}

uint8_t focus_fruit_permille_to_stage(int permille)
{
    // 这两条提前返回也挡住了调用方传入极值时的整数溢出。
    if (permille <= 0) {
        return 0;
    }
    if (permille >= 1000) {
        return (uint8_t)(FOCUS_FRUIT_STAGE_COUNT - 1);
    }

    // 五档均分,每档 200‰。1000 以内整除不会溢出,结果天然落在 0..4。
    const int stage = permille / (1000 / FOCUS_FRUIT_STAGE_COUNT);
    return (uint8_t)(stage > FOCUS_FRUIT_STAGE_COUNT - 1 ? FOCUS_FRUIT_STAGE_COUNT - 1 : stage);
}
