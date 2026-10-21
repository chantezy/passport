// main/focus_ui.c —— 界面的具体搭建与刷新。设计约定见 focus_ui.h。
//
// 三个页面:
//   首页   —— 像素字 "Focus" + 一行时长选择器(左减按钮 / 中数字 / 右加按钮),
//             上下键直接改这个数,OK 开始;
//   计时页 —— 上半屏一格主视觉,先后是三种画面:被一口口吃掉的水果 / 专注刚走完
//             那张合眼比勾的结束卡 / 休息中轮换的睡觉三帧(见 focus_scene_t);
//             下面一行倒计时数字,再下是状态、循环点阵,最底部一行操作提示
//             (计时中那一句只有半截 ——"长按放弃"/"长按跳过":番茄钟没有暂停,
//             一段计时走起来之后能做的除了等它走完,就只有不走了,而"不走"
//             在专注上是作废、在休息上是跳过);
//   设置页 —— 休息时长、长休间隔、提示音。
// 除阶段色(专注暖红 / 短休绿 / 长休蓝)与水果自身的颜色外不使用任何装饰色,
// 低光环境下不刺眼。所有文字都走自带的中文字体(见 focus_fonts.h)。
#include "focus_ui.h"
#include "focus_fonts.h"
#include "focus_sprites.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// 尺寸与配色
// ---------------------------------------------------------------------------

#define PAD_X 20
#define TOPBAR_Y 8

// 首页。标题是现成的像素字位图(203×49,见 assets/sprites/focus_wordmark.c),
// 240 宽的屏上两侧各留 18px,居中放。时长是一行横向 flex:左边减按钮、中间大数字、
// 右边加按钮。两侧的按钮是图(见 assets/sprites/focus_buttons.c),中间那个数走
// focus_pixel_35 —— 也是像素风,和标题、和计时页那行倒计时用同一套点阵格子。
#define BRAND_Y 56
// focus_pixel_35 的行框就是字模本身(35 高、基线贴底),数字墨迹占行框顶 +0..+34;
// 两颗按钮是 27×27,比行框矮,由 flex 的交叉轴居中摆平 —— 这一行的高度因此由
// focus_pixel_35 的行高定,是 35(按钮 55 高时是按钮说了算,现在不是了)。
// 容器顶在 166:数字墨迹 166..200,按钮居中落在 170..196;上距标题下沿(105)61px,
// 下距提示行墨迹(约 262 起)约 62px —— 整行在标题与提示之间居中,而不是像按钮
// 还高那一版那样挤在上半屏。想让整行更靠上/靠下,只改 HOME_ROW_Y。
//
// HOME_ROW_GAP 决定的是**按钮与数字之间的空**,不是按钮的边距。整行以屏幕中线对称、
// 两颗按钮同宽,于是数字的位置只由"两个 gap + 数字宽度"决定 —— 按钮 34→27 之后
// 数字一格没动:flex 把整行重新居中,少掉的 14px 左右各让 7px,正好被变窄的按钮吃掉。
// "缩按钮别把数字带跑"就是靠这条成立的,所以别再给按钮单独加外边距。
#define HOME_ROW_Y 166
#define HOME_ROW_GAP 14
#define HOME_HINT_Y 258

// 主视觉外接框:134×134,每一档位图都在框内居中。框的**中心**钉在 (120, 108) ——
// 这原来是 168 见方的框的位置,缩到 80% 之后中心不动,果子才是在原地变小,
// 而不是往上/往左挪。顶在 y=41(顶栏行框是 8..38,果柄不会碰到顶栏文字),
// 底在 175 —— 倒计时就接在它下面。
// 每种水果只有一个缩放系数,由它最苛刻的那一档定。框是硬边界,由
// tests/test_focus_sprites.c 看着;改 FOCUS_FRUIT_BOX 必须同时改这里的 X/Y
// 让中心不动,也见 tools/gen_sprites.py 的 BOX。睡觉三帧也走同一个框。
#define FRUIT_X 53
#define FRUIT_Y 41
#define FRUIT_SIZE FOCUS_FRUIT_BOX

// 倒计时:focus_pixel_35 的行框就是字模本身(35 高、基线贴底),墨迹占行框顶
// +0..+34。时间行的数字、格式("05:00")与首页那个数共用一款字体,见 focus_fonts.h。
//
// 行框顶定在 195,墨迹落到 195..229。这一行和下面那行状态在**任何时刻都不会同时
// 出现**(状态行只在 DONE 时才有字,而 DONE 那一屏倒计时是隐藏的,见 format_status
// 与下面那次重新对齐),所以它俩之间不用留间距 —— 中间那块空是果子下沿(175)到
// 点阵顶(250)之间的 75px,这一行 35px 的字就摆在它的正中:上留 20、下留 21。
// 行再往上顶着果子,再往下就要挨到点阵了,所以这个数是"居中"而不是"随便挪一点"。
#define TIME_Y 195

// 20px 字体的 line_height 是 30(Noto Sans CJK 的行高含较大行距),墨迹占行框顶
// +6..+25;下面这些 Y 都按 30px 的行框排,不是按字形墨迹的高度。
// 这一行现在只在"阶段刚走完、等下一次开始"时才真的有字(见 format_status),位置
// 仍要算准:倒计时涨到 35px 之后它往下挪了 6px,免得"休息结束"那句压到上面那行
// 大数字上。
#define STATUS_Y 220
// 专注刚走完那一屏:倒计时让位给结束卡,状态行单独上移到图底下(见 lay_out_timer)。
// 图占 45..171,行框顶 178 → 墨迹 184..203,和图之间留 13px。
#define DONE_STATUS_Y 178
// 点阵跟着状态行一起往下挪 4px:点到 260 为止。
#define DOTS_Y 250
#define DOTS_SIZE 10
#define DOTS_GAP 8
// 计时页底部只留一行操作提示。点阵到 260,提示行框 268..298,之间留 8px、下方留 22px。
// 原来两行提示时第一行在 258,现在只有一行,往下挪 10px 视觉上更居中。
// 设置页的提示行也用这个 Y —— 三个页面底部那一行的位置是同一件事。
#define HINT_Y 268

#define SET_TOP 46
#define SET_ROW_H 36

// 设置页底栏那一行,随光标停在哪一行而变。值行上单击是"改这一项的值";
// 停在最后那个动作行上时,同样的单击做的是另一件事,底栏跟着说出来 ——
// 否则屏幕上写着"单击修改"、按下去却把四项一起改了。
#define SETTINGS_HINT_VALUE "单击修改 · 长按返回"
#define SETTINGS_HINT_RESET "单击恢复 · 长按返回"

#define COL_BG 0x000000u
#define COL_TRACK 0x25272Du
#define COL_TEXT 0xEFF1F4u
#define COL_DIM 0x868B95u
#define COL_LINE 0x2B2E35u
#define COL_SEL_BG 0x1B1E24u
#define COL_FOCUS 0xFF6B5Au
#define COL_SHORT 0x35C08Au
#define COL_LONG 0x4E9BFFu

// 首页那两颗按钮被按下时压到的透明度。四成是"明显暗下去了、但还认得出是同一颗
// 按钮"的一档:再暗图形就糊没了,不压则和常态长得一模一样。图是合成到纯黑底上的
// RGB565(见 tools/gen_buttons.py),压透明度就是往黑里兑,和界面底色同色,不会
// 在按钮周围浮出一层别的颜色。
#define FLASH_OPA LV_OPA_40

static uint32_t phase_color(focus_phase_t phase)
{
    switch (phase) {
        case FOCUS_PHASE_SHORT_BREAK:
            return COL_SHORT;
        case FOCUS_PHASE_LONG_BREAK:
            return COL_LONG;
        case FOCUS_PHASE_FOCUS:
        default:
            return COL_FOCUS;
    }
}

// ---------------------------------------------------------------------------
// 建对象的小工具
// ---------------------------------------------------------------------------

static lv_obj_t *new_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_label_set_text(lbl, "");
    return lbl;
}

static void flatten(lv_obj_t *obj, uint32_t bg, lv_opa_t bg_opa, int32_t radius)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, bg_opa, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_screen(lv_obj_t *scr)
{
    flatten(scr, COL_BG, LV_OPA_COVER, 0);
}

// ---------------------------------------------------------------------------
// 首页
// ---------------------------------------------------------------------------

// 像素图(标题、加减按钮)的图像描述符。和水果那边是同一条理由:
// lv_image_set_src() 只存指针,真正取像素是在后面的重绘里,所以描述符必须活得和
// 这个 lv_image 一样久 —— 每张图一份文件级静态,初始化一次就不再变。
//
// 首页这三张图共用这一个构造函数:差别只有像素数组和尺寸,多写两遍就是把"stride 是
// 字节数、data_size 是 w*h*2"这类容易写错的账抄三次。计时页那一格另有
// focus_sprite_view.c 管,那边槽位多(水果 30 档加场景 4 张),得按序号查表。
static const lv_image_dsc_t *sprite_descriptor(const focus_sprite_t *sprite,
                                               lv_image_dsc_t *dsc, bool *ready)
{
    if (!*ready) {
        dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc->header.cf = LV_COLOR_FORMAT_RGB565;
        dsc->header.flags = 0;
        dsc->header.w = sprite->w;
        dsc->header.h = sprite->h;
        dsc->header.stride = (uint16_t)(sprite->w * 2u);
        dsc->data_size = (uint32_t)sprite->w * sprite->h * 2u;
        dsc->data = (const uint8_t *)sprite->px;
        *ready = true;
    }
    return dsc;
}

static lv_image_dsc_t s_brand_dsc;
static bool s_brand_ready;
static lv_image_dsc_t s_btn_minus_dsc;
static bool s_btn_minus_ready;
static lv_image_dsc_t s_btn_plus_dsc;
static bool s_btn_plus_ready;

static void build_home_screen(focus_ui_t *ui)
{
    ui->scr_home = lv_obj_create(NULL);
    style_screen(ui->scr_home);

    ui->home_battery = new_label(ui->scr_home, &focus_text_20, COL_DIM);
    lv_obj_align(ui->home_battery, LV_ALIGN_TOP_RIGHT, -PAD_X, TOPBAR_Y);

    ui->home_brand = lv_image_create(ui->scr_home);
    lv_obj_remove_flag(ui->home_brand, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->home_brand, sprite_descriptor(&focus_wordmark, &s_brand_dsc,
                                                       &s_brand_ready));
    lv_obj_align(ui->home_brand, LV_ALIGN_TOP_MID, 0, BRAND_Y);

    // 时长选择器:一行横向 flex,左减按钮、中数字、右加按钮。三个都挂这一个容器,
    // 位置由 flex 排,不用各自算居中偏移 —— 数字位数变了(5 → 25 → 90)整行仍然
    // 以屏幕中线对称。按钮是图,自己带着"可以按"的样子;中间那个数是当前值。
    ui->home_row = lv_obj_create(ui->scr_home);
    flatten(ui->home_row, COL_BG, LV_OPA_TRANSP, 0);
    lv_obj_set_size(ui->home_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ui->home_row, LV_ALIGN_TOP_MID, 0, HOME_ROW_Y);
    lv_obj_set_flex_flow(ui->home_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->home_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui->home_row, HOME_ROW_GAP, 0);

    ui->home_minus = lv_image_create(ui->home_row);
    lv_obj_remove_flag(ui->home_minus, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->home_minus, sprite_descriptor(&focus_button_minus,
                                                       &s_btn_minus_dsc, &s_btn_minus_ready));

    ui->home_minutes = new_label(ui->home_row, &focus_pixel_35, COL_TEXT);

    ui->home_plus = lv_image_create(ui->home_row);
    lv_obj_remove_flag(ui->home_plus, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->home_plus, sprite_descriptor(&focus_button_plus,
                                                      &s_btn_plus_dsc, &s_btn_plus_ready));

    // 首页底部只留一行,两个动作各占一半:"单击开始 · 长按设置" —— 两句都在 OK
    // 键上(见 input_home)。用 COL_DIM:底部的提示都是"告诉你怎么按",不是内容本身,
    // 比正文浅一档才不会和标题抢眼。
    ui->home_hint = new_label(ui->scr_home, &focus_text_20, COL_DIM);
    lv_label_set_text(ui->home_hint, "单击开始 · 长按设置");
    lv_obj_align(ui->home_hint, LV_ALIGN_TOP_MID, 0, HOME_HINT_Y);
}

// ---------------------------------------------------------------------------
// 计时页
// ---------------------------------------------------------------------------

static void build_timer_screen(focus_ui_t *ui)
{
    ui->scr_timer = lv_obj_create(NULL);
    style_screen(ui->scr_timer);

    ui->lbl_phase = new_label(ui->scr_timer, &focus_text_20, COL_FOCUS);
    lv_obj_align(ui->lbl_phase, LV_ALIGN_TOP_LEFT, PAD_X, TOPBAR_Y);

    ui->lbl_battery = new_label(ui->scr_timer, &focus_text_20, COL_DIM);
    lv_obj_align(ui->lbl_battery, LV_ALIGN_TOP_RIGHT, -PAD_X, TOPBAR_Y);

    // 主视觉:一只会被一口口吃掉的水果。这一屏的"进度"不再是一条环,而是果子本身
    // 被啃掉的样子 —— 六种水果各五档现成的像素图,挑哪只在 fruit_math 里,
    // 图在 focus_sprites.*。同一格还兼画专注结束卡与睡觉三帧(见 focus_sprite_view.h)。
    focus_sprite_view_create(&ui->scene, ui->scr_timer, FRUIT_X, FRUIT_Y);

    // 倒计时挂在水果下面,不再锚在圆环中心。像素字按行框顶定位(见 TIME_Y)。
    // 用的是 focus_pixel_35 —— 和首页那个专注时长同一个字号:它们本来就是"同一套
    // 像素字的同一个大小",只是各画在各自的页面上。
    ui->lbl_time = new_label(ui->scr_timer, &focus_pixel_35, COL_TEXT);
    lv_obj_align(ui->lbl_time, LV_ALIGN_TOP_MID, 0, TIME_Y);

    ui->lbl_status = new_label(ui->scr_timer, &focus_text_20, COL_DIM);
    lv_obj_align(ui->lbl_status, LV_ALIGN_TOP_MID, 0, STATUS_Y);

    ui->dot_row = lv_obj_create(ui->scr_timer);
    flatten(ui->dot_row, COL_BG, LV_OPA_TRANSP, 0);
    lv_obj_set_size(ui->dot_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ui->dot_row, LV_ALIGN_TOP_MID, 0, DOTS_Y);
    lv_obj_set_flex_flow(ui->dot_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->dot_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui->dot_row, DOTS_GAP, 0);

    // 底栏只有一行:这一行必须把当前状态下"能按的两个动作"都写出来,所以它随
    // 运行状态变(见 format_hint)。两行提示的形式(主操作一行、次操作一行)已经
    // 取消 —— 同一件事写两个按法,用户还得先分清哪行是哪种手势。
    // COL_DIM:底部提示统一用浅一档的颜色,和内容(时间、水果)分层。
    ui->lbl_hint = new_label(ui->scr_timer, &focus_text_20, COL_DIM);
    lv_obj_align(ui->lbl_hint, LV_ALIGN_TOP_MID, 0, HINT_Y);
}

// 循环点阵的数量只在 rounds_per_long 变化时才需要重排。
static void rebuild_dots(focus_ui_t *ui, uint8_t count)
{
    if (count > FOCUS_MAX_ROUNDS) {
        count = FOCUS_MAX_ROUNDS;
    }
    if (ui->dot_count == count && ui->dot_row != NULL && lv_obj_get_child_count(ui->dot_row) == count) {
        return;
    }
    lv_obj_clean(ui->dot_row);
    for (uint8_t i = 0; i < count; i++) {
        lv_obj_t *dot = lv_obj_create(ui->dot_row);
        lv_obj_set_size(dot, DOTS_SIZE, DOTS_SIZE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        ui->dot[i] = dot;
    }
    ui->dot_count = count;
}

// 倒计时下面那一行状态。它只在**阶段刚走完、等下一次开始**时才有话说 —— 其余
// 各状态都留空,因为每一种都已经有别的出口在说同一件事了:
//   专注中   —— 顶栏写着阶段名,进度由下面那排点阵说(点就是一轮);
//   休息中   —— 顶栏写着"短休息"/"长休息",画面本身就是那三帧轮换的睡觉图;
//   未开始   —— 那个数刚刚在首页选过,到这一页再报一遍是重复。
// 一行字挤不出新信息,却把画面往下压 —— 这一行空着的时候,主视觉与倒计时之间
// 就是干净的。
static void format_status(const focus_model_t *m, char *buf, size_t size)
{
    switch (m->run) {
        case FOCUS_RUN_DONE:
            // DONE 时阶段已经切到"下一个"了:专注走完停在休息阶段,休息走完停在专注阶段。
            // 直接写死一句会把刚专注完的画面显示成"休息结束"。
            // 这一句留着:它是"刚才那一段过完了"的唯一出口 —— 顶栏已经换成下一次的
            // 阶段名,光看顶栏反而像是下一段已经开始了。
            snprintf(buf, size, m->phase == FOCUS_PHASE_FOCUS ? "休息结束" : "专注结束");
            break;
        case FOCUS_RUN_RUNNING:
        case FOCUS_RUN_IDLE:
        default:
            buf[0] = '\0';
            break;
    }
}

// 底栏那一行字。它必须和模型允许的动作严格一致 —— 写了一个按不出来的动作,
// 用户会以为设备坏了。有单击动作的那几句是"单击 X · 长按 Y"的形式:**两句都指
// OK 键**(界面上一律写"单击",不写键名 —— 机身上只有一个能单击的键),所以提示里
// 写的长按动作就落在 OK 长按上,首页/设置页那两句也一样,见 focus_model_input 的
// 入口分发。深睡挂在长按上键上,不写进提示行。
//
// 长按那一句写的是"放弃"还是"跳过",取决于这一段还能不能接着算:专注上的长按是
// 放弃(这一轮作废、回首页),休息上的长按是跳过(这段休息不走了,直接进入下一个
// 番茄钟,已经走完的那几个照算)。"长按返回"只在既没有本轮可放弃、也没有休息可
// 跳过时出现(未开始 / 休息刚走完),那时长按是回首页。
static const char *format_hint(const focus_model_t *m)
{
    switch (m->run) {
        case FOCUS_RUN_RUNNING:
            // 这一句**只有半截**,因为计时中确实只有一个动作:番茄钟没有暂停,
            // 计时中按 OK 什么也不做(见 input_timer),所以没有"单击 X"可写。
            // 硬凑一个出来(哪怕是"单击暂停")就等于把已经去掉的规则又写回屏幕上。
            return m->phase == FOCUS_PHASE_FOCUS ? "长按放弃" : "长按跳过";
        case FOCUS_RUN_DONE:
            // DONE 时阶段已经切到"下一个"了:专注走完停在休息阶段,休息走完停在
            // 专注阶段。所以这里按阶段分辨接着要走的是休息还是专注 ——
            // 停在休息上是"这次休息还没开始,单击开它 / 长按跳过它",
            // 停在专注上是"下一个番茄钟已经摆好了,单击开始 / 长按回首页"。
            return m->phase == FOCUS_PHASE_FOCUS ? "单击开始 · 长按返回"
                                                 : "单击休息 · 长按跳过";
        case FOCUS_RUN_IDLE:
        default:
            return "单击开始 · 长按返回";
    }
}

// ---------------------------------------------------------------------------
// 设置页
// ---------------------------------------------------------------------------

// 没有"专注时长"这一行:它在首页上加减(见 build_home_screen)。同一处设置有两个
// 入口,用户没法分清哪一处说了算,所以值行只留休息相关的三项加提示音。
//
// 最后一行是动作而不是取值,所以它右边**不写值**(见 focus_ui_refresh 里那一格
// 留空的分支):"恢复默认"右边再跟一个词,无论写什么都会被读成"当前的默认值是
// 这个"。它按下去的结果由上面四行自己说 —— 数值当场变回出厂值。
static const char *const SETTING_NAMES[FOCUS_SETTING_COUNT] = {
    "短休息", "长休息", "长休间隔", "提示音", "恢复默认",
};

static void build_settings_screen(focus_ui_t *ui)
{
    ui->scr_settings = lv_obj_create(NULL);
    style_screen(ui->scr_settings);

    lv_obj_t *title = new_label(ui->scr_settings, &focus_text_20, COL_TEXT);
    lv_label_set_text(title, "设置");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, PAD_X, TOPBAR_Y);

    for (uint8_t i = 0; i < FOCUS_SETTING_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(ui->scr_settings);
        lv_obj_set_size(row, 200, 32);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, SET_TOP + (int32_t)i * SET_ROW_H);
        flatten(row, COL_SEL_BG, LV_OPA_TRANSP, 10);
        ui->set_row[i] = row;

        lv_obj_t *name = new_label(row, &focus_text_20, COL_TEXT);
        lv_label_set_text(name, SETTING_NAMES[i]);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);

        ui->set_val[i] = new_label(row, &focus_text_20, COL_DIM);
        lv_obj_align(ui->set_val[i], LV_ALIGN_RIGHT_MID, -12, 0);
    }

    // 底部只留一行,和另外两页一致 —— 位置也用同一个 HINT_Y。原来还有
    // "上下选择 · OK 修改"(选中行的高亮已经说了这件事)和"长按 OK · 休眠"
    // (这一页独有的冷知识,和上一行是同一类噪音)两行,一并去掉,一行只写真正的
    // 两个动作。这两个动作都在 OK 键上:单击改当前项,长按回首页(见 input_settings)。
    //
    // 这一行是会被改写的:光标停在"恢复默认"上时单击不再叫"修改"(见
    // focus_ui_refresh),所以这个标签要留着句柄。
    ui->lbl_set_hint = new_label(ui->scr_settings, &focus_text_20, COL_DIM);
    lv_label_set_text(ui->lbl_set_hint, SETTINGS_HINT_VALUE);
    lv_obj_align(ui->lbl_set_hint, LV_ALIGN_TOP_MID, 0, HINT_Y);
    ui->set_hint_is_reset = false;
}

// ---------------------------------------------------------------------------

void focus_ui_create(focus_ui_t *ui)
{
    if (ui == NULL || ui->scr_timer != NULL) {
        return;
    }
    memset(ui, 0, sizeof(*ui));

    build_home_screen(ui);
    build_timer_screen(ui);
    build_settings_screen(ui);
    lv_screen_load_anim(ui->scr_home, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

// ---------------------------------------------------------------------------
// 主视觉:这一刻该画哪一张
// ---------------------------------------------------------------------------

// 睡觉三帧的换帧间隔。休息时那一格要有动图的感觉,但这一版没有独立的动画定时器 ——
// 心跳每 100ms 问一次这个函数的结果,值变了才重绘(见 focus_app.c 的 tick_cb),
// 所以换帧间隔要落在 100ms 的网格上才不会被抽帧。700ms 正好。
#define SLEEP_FRAME_MS 700u

focus_scene_t focus_ui_scene(const focus_model_t *m, int64_t now_ms)
{
    if (m == NULL) {
        return FOCUS_SCENE_FRUIT;
    }

    if (m->run == FOCUS_RUN_DONE) {
        // 专注刚走完那一屏画那张比勾的结束卡(阶段这时已经切到接下来那次休息上,
        // 所以判断条件是"专注刚走完"而不是 phase 本身)。休息刚走完不画它 ——
        // 那一屏说的是"下一轮可以开始了",画面回到水果。
        return m->phase == FOCUS_PHASE_FOCUS ? FOCUS_SCENE_FRUIT : FOCUS_SCENE_DONE;
    }

    if (m->phase != FOCUS_PHASE_FOCUS && m->run == FOCUS_RUN_RUNNING) {
        // 休息中:三帧按**已走过的时长**轮换,不是按墙上时钟 —— 画面因此和倒计时
        // 同源,不会出现"数字停了、z 还在变大"这种两处说法不一致。
        // 被除数是 0 时(刚开始那一瞬)落在第 0 帧,不需要特判。
        const uint32_t total = focus_model_total_ms(m);
        const uint32_t left = focus_model_remaining_ms(m, now_ms);
        const uint32_t elapsed = total > left ? total - left : 0;
        // 0→1→2→1 四拍比 0→1→2→0 更像一次呼吸,而不是硬循环。
        static const uint8_t cycle[4] = {0, 1, 2, 1};
        return (focus_scene_t)(FOCUS_SCENE_SLEEP_0 + cycle[(elapsed / SLEEP_FRAME_MS) % 4u]);
    }

    return FOCUS_SCENE_FRUIT;
}

void focus_ui_refresh(focus_ui_t *ui, const focus_model_t *m, int64_t now_ms,
                      int battery_percent, uint8_t press_flash)
{
    if (ui == NULL || m == NULL || ui->scr_timer == NULL) {
        return;
    }

    const uint32_t accent = phase_color(m->phase);
    char buf[32];

    // 三个页面一一对应,没有压在谁上面的一层(见 focus_model.h 的 focus_view_t)。
    lv_obj_t *want;
    switch (m->view) {
        case FOCUS_VIEW_HOME:
            want = ui->scr_home;
            break;
        case FOCUS_VIEW_SETTINGS:
            want = ui->scr_settings;
            break;
        case FOCUS_VIEW_TIMER:
        default:
            want = ui->scr_timer;
            break;
    }
    if (lv_screen_active() != want) {
        lv_screen_load_anim(want, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    }

    // ---- 首页 ----
    snprintf(buf, sizeof(buf), "%d", (int)m->cfg.focus_min);
    lv_label_set_text(ui->home_minutes, buf);

    // 两颗按钮图按 press_flash 压暗或复原。这里不记"上次是哪一档":掩码为 0 就是
    // 常态,两条赋值比再养一个跨刷新的缓存便宜,也不会出现缓存和实际样式对不上。
    lv_obj_set_style_image_opa(ui->home_minus,
                               (press_flash & FOCUS_UI_FLASH_MINUS) ? FLASH_OPA : LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(ui->home_plus,
                               (press_flash & FOCUS_UI_FLASH_PLUS) ? FLASH_OPA : LV_OPA_COVER, 0);
    if (battery_percent == FOCUS_UI_BATTERY_UNKNOWN) {
        lv_label_set_text(ui->home_battery, "--");
    } else {
        snprintf(buf, sizeof(buf), "%d%%", battery_percent);
        lv_label_set_text(ui->home_battery, buf);
    }

    // ---- 顶栏 ----
    lv_label_set_text(ui->lbl_phase, focus_phase_name(m->phase));
    lv_obj_set_style_text_color(ui->lbl_phase, lv_color_hex(accent), 0);

    if (battery_percent == FOCUS_UI_BATTERY_UNKNOWN) {
        snprintf(buf, sizeof(buf), "--");
    } else {
        snprintf(buf, sizeof(buf), "%d%%", battery_percent);
    }
    lv_label_set_text(ui->lbl_battery, buf);

    // ---- 倒计时 ----
    const uint32_t remaining_sec = focus_model_remaining_sec(m, now_ms);
    const int minutes = (int)(remaining_sec / 60u);
    const int seconds = (int)(remaining_sec % 60u);
    // 分也补零:"05:00" 而不是 "5:00"。这一行是整屏第二大的东西,位数跳一下
    // (9:59 → 10:00)整行就会左右挪一格;补零之后它永远五个字符宽,数字不再横跳。
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    lv_label_set_text(ui->lbl_time, buf);

    // ---- 主视觉:水果 / 结束卡 / 睡觉三帧,同一格换图 ----
    const focus_scene_t scene = focus_ui_scene(m, now_ms);
    switch (scene) {
        case FOCUS_SCENE_DONE:
            focus_sprite_view_show_pic(&ui->scene, FOCUS_PIC_DONE);
            break;
        case FOCUS_SCENE_SLEEP_0:
        case FOCUS_SCENE_SLEEP_1:
        case FOCUS_SCENE_SLEEP_2:
            focus_sprite_view_show_pic(&ui->scene,
                                       (uint8_t)(FOCUS_PIC_SLEEP_0 + (scene - FOCUS_SCENE_SLEEP_0)));
            break;
        case FOCUS_SCENE_FRUIT:
        default:
            // 果子啃到第几档跟着"已走过的比例"走,和剩余秒数同源 —— 这一格和倒计时
            // 说的因此永远是同一件事,不会一个停一个走。
            focus_sprite_view_show_fruit(&ui->scene, m->fruit,
                                         focus_fruit_permille_to_stage(
                                             focus_model_elapsed_permille(m, now_ms)));
            break;
    }

    // 结束卡那一屏换的是"这两行字摆在哪":倒计时让位,状态行上移到卡片底下。
    // 其余各屏都是建屏时的老位置,所以只在跨过这条线的那一次重排。
    if (scene != (focus_scene_t)ui->shown_scene) {
        ui->shown_scene = (uint8_t)scene;
        if (scene == FOCUS_SCENE_DONE) {
            lv_obj_add_flag(ui->lbl_time, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(ui->lbl_status, LV_ALIGN_TOP_MID, 0, DONE_STATUS_Y);
        } else {
            lv_obj_remove_flag(ui->lbl_time, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(ui->lbl_status, LV_ALIGN_TOP_MID, 0, STATUS_Y);
        }
    }

    // ---- 状态行与循环点阵 ----
    format_status(m, buf, sizeof(buf));
    lv_label_set_text(ui->lbl_status, buf);

    rebuild_dots(ui, m->cfg.rounds_per_long);
    for (uint8_t i = 0; i < ui->dot_count; i++) {
        const bool filled = i < m->cycle_done;
        lv_obj_set_style_bg_color(ui->dot[i], lv_color_hex(filled ? accent : COL_TRACK), 0);
        lv_obj_set_style_bg_opa(ui->dot[i], LV_OPA_COVER, 0);
    }

    // ---- 底部操作提示 ----
    lv_label_set_text(ui->lbl_hint, format_hint(m));

    // ---- 设置页 ----
    for (uint8_t i = 0; i < FOCUS_SETTING_COUNT; i++) {
        lv_obj_t *row = ui->set_row[i];
        const bool selected = (i == m->settings_row);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        switch ((focus_setting_t)i) {
            case FOCUS_SETTING_SHORT_BREAK:
                snprintf(buf, sizeof(buf), "%d 分钟", (int)m->cfg.short_break_min);
                break;
            case FOCUS_SETTING_LONG_BREAK:
                snprintf(buf, sizeof(buf), "%d 分钟", (int)m->cfg.long_break_min);
                break;
            case FOCUS_SETTING_ROUNDS:
                snprintf(buf, sizeof(buf), "%d 个", (int)m->cfg.rounds_per_long);
                break;
            case FOCUS_SETTING_CHIME:
                snprintf(buf, sizeof(buf), "%s", m->cfg.chime_on ? "开" : "关");
                break;
            case FOCUS_SETTING_RESET:
            default:
                // 动作行右边不写字。写什么都没法准确:写"是"像确认框,写"25 分钟"
                // 又会被当成"默认值是这个数"。留空反而最清楚 —— 它是这一页里唯一
                // 一个不参与"这一行的值是多少"的行。
                buf[0] = '\0';
                break;
        }
        lv_label_set_text(ui->set_val[i], buf);
        lv_obj_set_style_text_color(ui->set_val[i], lv_color_hex(selected ? accent : COL_DIM), 0);
    }

    // 光标停在动作行上时,底栏那句话要跟着换(见 SETTINGS_HINT_*)。
    const bool hint_is_reset = (m->settings_row == FOCUS_SETTING_RESET);
    if (hint_is_reset != ui->set_hint_is_reset) {
        lv_label_set_text(ui->lbl_set_hint,
                          hint_is_reset ? SETTINGS_HINT_RESET : SETTINGS_HINT_VALUE);
        ui->set_hint_is_reset = hint_is_reset;
    }
}
