// main/focus_sprite_view.c —— 见同名 .h。
//
// 槽位是扁平的:0..29 是六种水果 × 五个阶段(assets/sprites/focus_fruit_*.c),30 起
// 是水果之外的那几张(assets/sprites/focus_scenes.c)。不管画哪一张,走的都是同一条路:
// 按槽位取到一个 focus_sprite_t,把它居中放进外接框。所以这里只有一份代码。
//
// 图像描述符必须活得和 lv_image 一样久 —— lv_image_set_src() 只存指针,真正取像素是
// 在后面的重绘里。所以每个槽位一份描述符、初始化一次就不再变:换图时换的是"指向哪一个
// 描述符",而不是就地改同一个描述符的字段(那样会在"旧区域还没重绘完、描述符已经指向
// 新图"时画出撕裂的画面)。
#include "focus_sprite_view.h"

// 30 个水果槽 + 4 个场景槽,每个 header 8 字节加两个指针和一个长度,合计约 820 字节的
// .bss。相对一块 1.1 MB 的精灵表不值一提,换来的是换图时绝不碰共享状态。
#define FRUIT_SLOTS (FOCUS_FRUIT_COUNT * FOCUS_FRUIT_STAGE_COUNT)
#define SLOT_COUNT (FRUIT_SLOTS + FOCUS_PIC_COUNT)

static lv_image_dsc_t s_dsc[SLOT_COUNT];
static bool s_dsc_ready;

// 槽位 -> 位图。水果那一段按 (水果, 阶段) 排,后面接专注结束卡与睡觉三帧。
static const focus_sprite_t *sprite_for(uint16_t slot)
{
    if (slot < FRUIT_SLOTS) {
        return focus_fruit_sprite((uint8_t)(slot / FOCUS_FRUIT_STAGE_COUNT),
                                  (uint8_t)(slot % FOCUS_FRUIT_STAGE_COUNT));
    }

    const uint8_t pic = slot - FRUIT_SLOTS < FOCUS_PIC_COUNT ? (uint8_t)(slot - FRUIT_SLOTS)
                                                            : (uint8_t)(FOCUS_PIC_COUNT - 1);
    if (pic == FOCUS_PIC_DONE) {
        return &focus_scene_done;
    }
    return &focus_scene_sleep[pic - FOCUS_PIC_SLEEP_0];
}

// 把一张位图包装成 LVGL 的图像描述符。指针指向 Flash 里的 const 数组,LVGL 直接读,
// 不解码、不拷贝 —— 这也是不能用 PNG 的原因:那块板子没有 PSRAM,LVGL 堆只有 24 KB,
// 开不起解码缓冲。
static const lv_image_dsc_t *descriptor(uint16_t slot)
{
    if (!s_dsc_ready) {
        for (uint16_t i = 0; i < SLOT_COUNT; i++) {
            const focus_sprite_t *sprite = sprite_for(i);
            lv_image_dsc_t *dsc = &s_dsc[i];
            dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
            dsc->header.cf = LV_COLOR_FORMAT_RGB565;
            dsc->header.flags = 0;
            dsc->header.w = sprite->w;
            dsc->header.h = sprite->h;
            dsc->header.stride = (uint16_t)(sprite->w * 2u);
            dsc->data_size = (uint32_t)sprite->w * sprite->h * 2u;
            dsc->data = (const uint8_t *)sprite->px;
        }
        s_dsc_ready = true;
    }
    return &s_dsc[slot];
}

// 把边长 size 的东西居中放进边长 box 的框。位图比框大时按 0 处理(出图时已经保证
// 不会,这里是防着以后换了素材)。
static int32_t center(int32_t box, int32_t size)
{
    return size >= box ? 0 : (box - size) / 2;
}

static void show_slot(focus_sprite_view_t *view, uint16_t slot)
{
    if (slot == view->shown) {
        return;
    }
    view->shown = slot;

    const focus_sprite_t *sprite = sprite_for(slot);
    lv_image_set_src(view->img, descriptor(slot));
    lv_obj_set_pos(view->img,
                   view->box_x + center(FOCUS_FRUIT_BOX, sprite->w),
                   view->box_y + center(FOCUS_FRUIT_BOX, sprite->h));
}

void focus_sprite_view_create(focus_sprite_view_t *view, lv_obj_t *parent, int32_t x, int32_t y)
{
    view->box_x = x;
    view->box_y = y;
    view->shown = 0xFFFF;  // 无效值:下面这次一定会真的挂上图

    view->img = lv_image_create(parent);
    lv_obj_remove_flag(view->img, LV_OBJ_FLAG_CLICKABLE);

    // 先摆上 0 号水果的整只:万一在刷新函数跑起来之前这一屏就被显示了,
    // 看到的是一只完整的水果,而不是一块空白。
    focus_sprite_view_show_fruit(view, 0, 0);
}

void focus_sprite_view_show_fruit(focus_sprite_view_t *view, uint8_t fruit, uint8_t stage)
{
    if (fruit >= FOCUS_FRUIT_COUNT) {
        fruit = 0;
    }
    if (stage >= FOCUS_FRUIT_STAGE_COUNT) {
        stage = (uint8_t)(FOCUS_FRUIT_STAGE_COUNT - 1);
    }
    show_slot(view, (uint16_t)((uint16_t)fruit * FOCUS_FRUIT_STAGE_COUNT + stage));
}

void focus_sprite_view_show_pic(focus_sprite_view_t *view, uint8_t pic)
{
    if (pic >= FOCUS_PIC_COUNT) {
        pic = (uint8_t)(FOCUS_PIC_COUNT - 1);
    }
    show_slot(view, (uint16_t)(FRUIT_SLOTS + pic));
}
