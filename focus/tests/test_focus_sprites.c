// 像素水果精灵表的 host 测试。
//
// 这些位图是生成的,肉眼检查的只有样张;真机上如果哪一张缺了、尺寸对不上、或者
// 顺序和水果序号错位(屏幕上会变成"名字叫香蕉的苹果"),那是像素级的错误,看日志
// 看不出来。这里把三件事钉死:
//   1) 6 种 × 5 档一张不缺,尺寸都塞得进 FOCUS_FRUIT_BOX;
//   2) 表的下标与 focus_fruit_name() 对得上(拿第 1 档的尺寸当指纹);
//   3) 整体确实越吃越小,而且总字节数没超出预算(防止有人不小心按"每张各自撑满框"
//      重新生成 —— 那样胡萝卜最后一张会比第一张还大)。
//
// 首页时长选择器两侧的两颗按钮(assets/sprites/focus_buttons.c)也在这里看着:
// 它们是从一张素材图上按 alpha 自动切出来的,切歪了在真机上不容易发现。
//
// 计时页那一格上水果之外的画面(assets/sprites/focus_scenes.c)同样:结束卡一张,
// 加上休息中轮换的睡觉三帧 —— 三帧尺寸必须一致,否则动画会跳。
//
// 链接了全部 30 张水果位图加 6 张其它位图,编译会慢几秒,这是为了在主机上真的读到
// 像素。
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "focus_fruit_math.h"
#include "focus_sprites.h"

// 上机尺寸的预算:当前实测 685,802 字节(外接框 134 见方)。留一点余量,但不给
// "每张各自撑满框"留活路 —— 那种出图的果子会忽大忽小,而且会明显超出这里。
#define SPRITE_BUDGET_BYTES 800000u

// 一张位图里至少要有这么多像素不是纯黑,否则多半是切歪了或切空了。
#define MIN_INK_PERCENT 20

// 首页时长选择器两侧的两颗按钮(assets/sprites/focus_buttons.c)。它们不是画出来的,
// 是从一张素材图上按 alpha 通道自动切出来的 —— 切歪了两三像素在 240×320 的屏上
// 看不出来,但"减号被切掉了半边"或者"两颗按钮一大一小"是一眼就该看出来的错误,
// 所以在这里钉住:等宽、等高、非空、塞得进选择器那一行。
#define BUTTON_BUDGET_BYTES 40000u
#define HOME_ROW_MAX_H 70

// 按钮在屏幕上的实际尺寸。设计尺寸 56 的六成是 34,这一版再缩到它的八成 → 27
// (高按原图比例算出来也是 27,两颗按钮的源图高度只差 1px,生成侧按第一颗统一算,
// 见 tools/gen_buttons.py)。
//
// 钉住这两个数是因为首页那一行的位置(HOME_ROW_Y)就是按这个高度排的:按钮哪天
// 悄悄变大或变小,这一行就歪了,而在 240×320 上说"歪了一点"没人能当场指出哪里错。
// 另外这里顺便守住一条**不该由尺寸负责**的性质:整行在屏幕中线上对称、两颗按钮
// 同宽,所以数字的横坐标只由"两个间距 + 数字宽度"决定 —— 按钮从 34 缩到 27,
// 数字一格没动(见 focus_ui.c 里 HOME_ROW_GAP 的说明)。
#define BUTTON_W 27
#define BUTTON_H 27

// 计时页那一格上水果之外的画面(assets/sprites/focus_scenes.c):专注结束卡一张 +
// 休息中轮换的睡觉三帧。这里最要紧的一条是**三帧尺寸必须完全一样** —— 它们是同一
// 个槽位里每 700ms 换一张的动画,尺寸差一像素换帧时角色就会跳,而那种错误在真机上
// 只表现为"动图有点怪",很难归因。生成侧按床沿对齐、共用同一个裁切框(见
// tools/gen_scenes.py),这里在消费侧再钉一次。
#define SCENE_BUDGET_BYTES 130000u

// 三帧的尺寸指纹。和水果那个 WHOLE[] 一样,是把"生成物"和"屏幕上的观感"绑起来的
// 那根绳子:素材换版后尺寸变了,这里会先失败,而不是等上了设备才发现画面被撑变形。
#define SLEEP_FRAME_W 88
#define SLEEP_FRAME_H 134
#define DONE_CARD_W 134
#define DONE_CARD_H 126

// 第 1 档(整只)的尺寸指纹。顺序必须与 focus_fruit_name() 一一对应 —— 这是把
// "TABLES 的下标"和"水果序号"绑在一起的那根绳子。
static const struct {
    uint16_t w;
    uint16_t h;
} WHOLE[FOCUS_FRUIT_COUNT] = {
    { 111, 121 },  // banana
    { 133, 105 },  // watermelon
    {  87, 132 },  // pineapple
    { 131, 119 },  // tomato
    { 134, 109 },  // carrot
    { 116, 134 },  // apple
};

// 一张位图里非黑像素的个数。判断"切空了"和"底色没抠掉"都靠它。
static uint32_t ink_pixels(const focus_sprite_t *sprite)
{
    const uint32_t count = (uint32_t)sprite->w * sprite->h;
    uint32_t ink = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (sprite->px[i] != 0) {
            ink++;
        }
    }
    return ink;
}

int main(void)
{
    const focus_sprite_t *const *tables = focus_fruit_sprite_tables();
    assert(tables != NULL);

    uint32_t total_bytes = 0;
    for (uint8_t fruit = 0; fruit < FOCUS_FRUIT_COUNT; fruit++) {
        const focus_sprite_t *table = tables[fruit];
        assert(table != NULL);

        for (uint8_t stage = 0; stage < FOCUS_FRUIT_STAGE_COUNT; stage++) {
            const focus_sprite_t *sprite = focus_fruit_sprite(fruit, stage);
            assert(sprite == &table[stage]);
            assert(sprite->px != NULL);

            // 尺寸:非零、塞得进框。框是正方形的,两条边都要卡。
            // 这条不是洁癖:框下面就是倒计时的字,顶出框的果子和数字叠在一起。
            // 会挂在这里说明 tools/gen_sprites.py 的缩放系数又只看第 1 档了 ——
            // 被咬过的果子外接框偶尔比整只还宽(西瓜第 2 档就比整只宽 1 像素)。
            assert(sprite->w > 0 && sprite->h > 0);
            assert(sprite->w <= FOCUS_FRUIT_BOX);
            assert(sprite->h <= FOCUS_FRUIT_BOX);
            total_bytes += (uint32_t)sprite->w * sprite->h * 2u;

            // 内容:不是一片黑。精灵是按墨迹外接框裁出来的,本身就填得比较满。
            const uint32_t count = (uint32_t)sprite->w * sprite->h;
            const uint32_t ink = ink_pixels(sprite);
            assert(ink * 100u >= count * MIN_INK_PERCENT);
            // 也不该是"整块非黑":那说明背景没抠掉,底色被当成墨迹了。
            assert(ink < count);
        }

        // 整体确实被吃掉了:最后一档的面积必须小于整只。中途允许有起伏 ——
        // 素材里画家画的第 2 档外接框偶尔比第 1 档大一两像素,那是原样保留的。
        const focus_sprite_t *whole = focus_fruit_sprite(fruit, 0);
        const focus_sprite_t *last = focus_fruit_sprite(fruit, FOCUS_FRUIT_STAGE_COUNT - 1);
        assert((uint32_t)last->w * last->h < (uint32_t)whole->w * whole->h);

        // 指纹:整只的尺寸 + 名字,两者一起把顺序钉住。
        assert(whole->w == WHOLE[fruit].w && whole->h == WHOLE[fruit].h);
        printf("  %-11s %u×%u → %u×%u\n", focus_fruit_name(fruit),
               whole->w, whole->h, last->w, last->h);
    }

    assert(total_bytes <= SPRITE_BUDGET_BYTES);
    printf("  sprite bytes: %u (budget %u)\n", total_bytes, SPRITE_BUDGET_BYTES);

    // ---- 首页那两颗步进按钮 ----
    //
    // tools/gen_buttons.py 用 alpha 通道自动找方块,所以最容易出的错是"切到了相邻的
    // 那一颗"或者"多带进来一条边"。两颗同尺寸是最省事的判据:它们本来就取自同一张
    // 素材上高度相同、宽度只差 1 像素的两个方块(减号 450、加号 449),缩到同一个
    // 宽度之后必须连高度也一样。真机上把减号画成加号这种错误,只有这里能拦住。
    const focus_sprite_t *buttons[2] = { &focus_button_minus, &focus_button_plus };
    const char *button_names[2] = { "minus", "plus" };
    uint32_t button_bytes = 0;
    for (int i = 0; i < 2; i++) {
        const focus_sprite_t *sprite = buttons[i];
        assert(sprite->px != NULL);
        assert(sprite->w > 0 && sprite->h > 0);
        // 塞得进那一行:按钮和 35px 数字共用一条横向 flex 容器(HOME_ROW_MAX_H)。
        assert(sprite->h <= HOME_ROW_MAX_H);
        assert(sprite->w <= 2 * HOME_ROW_MAX_H);

        // 内容:实心的圆角方块,墨迹占比本来就高;两头都要卡 —— 全黑是切空了,
        // 满格是背景没抠掉(素材的底是一圈半透明的白光晕,不是纯透明)。
        const uint32_t count = (uint32_t)sprite->w * sprite->h;
        const uint32_t ink = ink_pixels(sprite);
        assert(ink * 100u >= count * MIN_INK_PERCENT);
        assert(ink < count);

        // 两颗必须同宽同高:同一张素材、同一个目标尺寸,差一像素就是切歪了。
        // (理由见 tools/gen_buttons.py:高度是按第一块算一次、两块共用,不能各算各的。)
        assert(sprite->w == buttons[0]->w && sprite->h == buttons[0]->h);
        // 尺寸指纹:和首页 HOME_ROW_Y 那套排版绑在一起,改大改小都要有人回看一眼。
        assert(sprite->w == BUTTON_W && sprite->h == BUTTON_H);
        button_bytes += (uint32_t)sprite->w * sprite->h * 2u;
        printf("  button %-5s %u×%u\n", button_names[i], sprite->w, sprite->h);
    }
    assert(button_bytes <= BUTTON_BUDGET_BYTES);
    printf("  button bytes: %u (budget %u)\n", button_bytes, BUTTON_BUDGET_BYTES);

    // ---- 结束卡 + 睡觉三帧(assets/sprites/focus_scenes.c)----
    uint32_t scene_bytes = 0;
    const focus_sprite_t *scenes[] = { &focus_scene_done, &focus_scene_sleep[0],
                                       &focus_scene_sleep[1], &focus_scene_sleep[2] };
    const char *scene_names[] = { "done", "sleep-1", "sleep-2", "sleep-3" };
    const uint16_t scene_w[] = { DONE_CARD_W, SLEEP_FRAME_W, SLEEP_FRAME_W, SLEEP_FRAME_W };
    const uint16_t scene_h[] = { DONE_CARD_H, SLEEP_FRAME_H, SLEEP_FRAME_H, SLEEP_FRAME_H };

    for (int i = 0; i < 4; i++) {
        const focus_sprite_t *sprite = scenes[i];
        assert(sprite->px != NULL);
        assert(sprite->w > 0 && sprite->h > 0);

        // 塞得进那一格:它和水果共用同一个正方形槽位(FRUIT_X/FRUIT_Y 那个框),
        // 顶出去就会压到下面那行倒计时。
        assert(sprite->w <= FOCUS_FRUIT_BOX);
        assert(sprite->h <= FOCUS_FRUIT_BOX);

        // 内容:既不是一片黑,也不是整块非黑(背景没抠掉)。素材的底是画上去的
        // 棋盘格,不是 alpha 通道,抠漏了会整张留在里面。
        const uint32_t count = (uint32_t)sprite->w * sprite->h;
        const uint32_t ink = ink_pixels(sprite);
        assert(ink * 100u >= count * MIN_INK_PERCENT);
        assert(ink < count);

        // 尺寸指纹:换了素材、或者缩放系数被改成"每张各自撑满框",这里先报错。
        assert(sprite->w == scene_w[i] && sprite->h == scene_h[i]);

        scene_bytes += (uint32_t)sprite->w * sprite->h * 2u;
        printf("  scene %-8s %u×%u\n", scene_names[i], sprite->w, sprite->h);
    }

    // 三帧完全同尺寸。这是这个数组唯一存在的理由:同一个槽位里换图做动画,
    // 尺寸不同就会跳。上面那轮循环已经逐张比过指纹,这里再按对子比一次 ——
    // 指纹写错时(比如把某一帧的期望值抄成另一个数)这道闸才会响。
    for (int i = 1; i < 3; i++) {
        assert(focus_scene_sleep[i].w == focus_scene_sleep[0].w);
        assert(focus_scene_sleep[i].h == focus_scene_sleep[0].h);
    }
    assert(scene_bytes <= SCENE_BUDGET_BYTES);
    printf("  scene bytes: %u (budget %u)\n", scene_bytes, SCENE_BUDGET_BYTES);

    // ---- 越界一律夹住,而不是读越界内存 ----
    assert(focus_fruit_sprite(FOCUS_FRUIT_COUNT, 0) == focus_fruit_sprite(0, 0));
    assert(focus_fruit_sprite(255, 255) == focus_fruit_sprite(0, FOCUS_FRUIT_STAGE_COUNT - 1));
    assert(focus_fruit_sprite(0, FOCUS_FRUIT_STAGE_COUNT) ==
           focus_fruit_sprite(0, FOCUS_FRUIT_STAGE_COUNT - 1));
    // 阶段越界夹到最后一档而不是第 0 档:越界时"果子快吃完了"比"果子凭空长回来"
    // 更不容易让人以为是 bug。
    assert(focus_fruit_sprite(0, 200)->w ==
           focus_fruit_sprite(0, FOCUS_FRUIT_STAGE_COUNT - 1)->w);

    puts("focus sprite tests: PASS");
    return 0;
}
