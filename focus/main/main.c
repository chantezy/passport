// main/main.c —— 番茄钟固件入口:板级上电顺序 + 应用启动。
//
// 本文件只负责"把板子点亮",不再承载业务:
//   I2C 总线 → ST7789 面板 + LVGL → 背光 → 交给 main/focus_app.c。
// 界面、状态机、落盘、功耗策略分别住在 focus_ui.c / focus_model.c /
// focus_store.c / focus_app.c;可复用的板级逻辑在 components/bsp。
//
// 按键语义(全应用统一,详见 focus_app.c 的 on_key 与 focus_model.c 的入口分发):
//   上/下 短按   首页上调整专注时长(上 +5 / 下 -5);设置页里移动选中行
//   上    长按   首页/设置页 = 进入深睡;计时页未开始时 = 进设置页
//   确定  短按   首页 = 开始;计时页 = 开始 / 进入下一阶段(计时中按了不做任何事)
//   确定  长按   首页 = 进设置页;设置页 = 回首页;计时页 = 专注中放弃本轮 / 休息中跳过
// 计时页上没有"暂停":番茄钟的一个番茄不可分割,被打断就不算数,所以计时中只有
// "等它走完"和"作废重来"两条路(见 focus_model.h 的 focus_run_t)。放弃是一次长按
// 按下去就生效,没有二次确认。
// 休息上的长按是"跳过这段休息",不是放弃:已经走完的那几个番茄照算,落点是计时页上
// 那个待开始的番茄钟,而不是首页 —— 跳过休息的人只是不想休息,没有要改时长。见
// focus_model.c 的 timer_long_press。
// 双击不再参与:同一件事有两种按法,底栏那一行提示就写不下了。
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"  // 初始化失败时打印 LCD 引脚号
#include "focus_app.h"

#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "FoloToy AI Passport 番茄钟启动");

    // 最先做:按键唤醒时那颗键还按着,守卫越早武装越好(见 focus_app.h)。
    focus_app_guard_wakeup();

    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是本应用的唯一界面,失败就没有可用的产品形态 —— 打清楚日志后退出,
    // 不做"串口降级"(那只会让本文件复杂一倍,却给不出一个能看的番茄钟)。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // 应用内部的失败已经按外设分级降级(见 focus_app.c),这里只需如实记下来。
    const esp_err_t err = focus_app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "应用启动未完全成功: %s", esp_err_to_name(err));
    }
}
