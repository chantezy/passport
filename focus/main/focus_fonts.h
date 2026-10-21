// main/focus_fonts.h
// 本应用自带的位图字体。LVGL 内置的 montserrat 只有拉丁字形,内置的 han_sans 只有
// 16px 且只覆盖约 1187 个常用汉字,都撑不起本应用的界面,所以自带两份:
//
//   focus_text_20  —— 中文标签与提示(含 ASCII),用于除两处大数字外的所有文字。
//                     由 tools/gen_font.py 从 Noto Sans SC Regular 裁出来生成,
//                     只含 assets/fonts/charset_focus.txt 里列的字。
//   focus_pixel_35 —— 只有数字与冒号,用于两处大数字:计时页那行倒计时("05:00")
//                     和首页的专注时长("%d")。
//
// 像素字体不是从任何字体文件裁的:它由 tools/gen_pixel_font.py 直接从一个手写的
// 5×7 点阵放大而成(7×5=35),没有来源字体的许可问题。行高就是点阵本身,行框里
// 除了墨迹没有别的空白,所以界面上按"行框顶"定位就等于按"墨迹顶"定位。
//
// 两处大数字**共用一份字体**而不是各来一份 28px/35px:它们本来就是同一种东西画在
// 两个页面上,分成两份字号只会让"这两处该一样大"这件事多一个会走偏的地方。一份
// 字体为多出来的冒号多花几百字节,换来的是改一处两处一起对。
//
// 生成命令写在各自 .c 文件的头部注释里。focus_text_20 的来源与许可见 assets/README.md。
#pragma once

#include "lvgl.h"

extern const lv_font_t focus_text_20;
extern const lv_font_t focus_pixel_35;
