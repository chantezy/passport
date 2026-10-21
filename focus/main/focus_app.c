// main/focus_app.c —— 见 focus_app.h 的分工说明。
#include "focus_app.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "focus_chime.h"
#include "focus_fruit_math.h"
#include "focus_model.h"
#include "focus_store.h"
#include "focus_ui.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "focus";

#define INPUT_QUEUE_DEPTH 8
#define TICK_PERIOD_MS 100

// 按下 - / + 的那颗按钮图压暗多久。200ms 落在一次短按(500ms 才算长按)之内:
// 快按一下、手都抬了,那一暗还在,不会出现"按了却没看见";长按则早就进了别的
// 语义,这一点亮与不亮都无所谓。取 100 的整数倍,好让它整拍地亮、整拍地灭。
#define FLASH_MS 200u

// 深睡按键唤醒后的守卫兜底时长(见 key_guard.h)。按键卡住时最多挡这么久。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)

// 省电:这一版**没有**自动调暗/熄屏/休眠。
//
// 原来停手 30 秒调暗、60 秒熄屏、10 分钟自动深睡。现在全都去掉了 —— 番茄钟的
// 全部价值就是"抬头一眼能看见还剩多久",屏幕自己在倒计时中间暗掉正好把这个价值
// 抹掉。代价是背光全程 100%:这台设备是插着用的计时器,这个取舍是有意的。
//
// 所以这里没有背光状态要跟踪,也没有"熄屏后第一次按键该吞掉"的问题(见 git 历史
// 里那份 swallow 状态机)。深睡只剩一个入口:首页/设置页上长按上键。
#define POWER_POLL_MS 200u
#define BATTERY_POLL_MS 5000u

#define POWER_TASK_STACK 4096
#define POWER_TASK_PRIO 3

static focus_model_t s_model;
static focus_ui_t s_ui;
static QueueHandle_t s_input_queue;
static lv_timer_t *s_tick_timer;
static key_guard_t s_key_guard;
static bool s_started;
static volatile bool s_input_ready;
static volatile bool s_sleep_request;

// 跨任务共享的少量标量:都是单字节/单字读写,不需要更重的同步手段。
static volatile int s_battery = FOCUS_UI_BATTERY_UNKNOWN;

// 只在 LVGL 任务里读写的显示缓存,避免每 100ms 无谓地重排文字。scene 是主视觉那一格
// 现在画的是哪一张——它也是休息中那三帧动画的节拍,换帧全靠它这一个值的变化。
static uint32_t s_shown_sec = 0xFFFFFFFFu;
static focus_scene_t s_shown_scene = FOCUS_SCENE_FRUIT;
static int s_shown_battery = -1000;
static uint8_t s_shown_flash = 0;

// 按下 - / + 时的即时反馈:对应那颗按钮图暗一下,手还没抬起就能看见"按到了"。
//
// 事件在按键组件的 esp_timer 任务上,画图在 LVGL 任务上,所以跨任务只传两个单字节
// 标志,截止时刻由 LVGL 任务这一侧自己算 —— 让两个任务同时读写一个 int64 是另一
// 码事,没必要为省一个变量去趟那个浑水。
// 下标 0 是减、1 是加;它们和 focus_ui.h 里那两个位的对应关系就写在这张表里,
// 别处不要再各记一遍。
enum { FLASH_MINUS = 0, FLASH_PLUS = 1, FLASH_COUNT = 2 };
static const uint8_t FLASH_BIT[FLASH_COUNT] = { FOCUS_UI_FLASH_MINUS, FOCUS_UI_FLASH_PLUS };
static volatile bool s_flash_pending[FLASH_COUNT];
static int64_t s_flash_until_ms[FLASH_COUNT];

// ---------------------------------------------------------------------------
// 按键
// ---------------------------------------------------------------------------

static bool translate(bsp_btn_t btn, bsp_btn_ev_t ev, focus_key_t *out)
{
    switch (ev) {
        case BSP_BTN_CLICK:
            if (btn == BSP_BTN_UP) { *out = FOCUS_KEY_UP; return true; }
            if (btn == BSP_BTN_DOWN) { *out = FOCUS_KEY_DOWN; return true; }
            if (btn == BSP_BTN_OK) { *out = FOCUS_KEY_OK; return true; }
            return false;
        case BSP_BTN_LONG:
            if (btn == BSP_BTN_UP) { *out = FOCUS_KEY_UP_LONG; return true; }
            if (btn == BSP_BTN_OK) { *out = FOCUS_KEY_OK_LONG; return true; }
            return false;
        default:
            // 按下、抬起、双击都不参与语义。BSP 仍然会上报双击(它要照顾别的应用),
            // 这里直接当没看见:同一件事有两种按法,底栏那一行就写不下了。
            return false;
    }
}

// 运行在 button 组件的共享 esp_timer 任务上:只能做有界的、不阻塞的事。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;

    // 深睡按键唤醒会让芯片在"唤醒键还按着"时重启,组件随后把这次已经按下的
    // 电平判成长按并立刻触发一次动作。守卫在它松手前吞掉所有事件。
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }

    // 按下这一刻是唯一一个"还没抬手就能看见"的事件,按钮的即时反馈挂在这里。
    // 它和下面的语义是两回事:反馈只说明"这颗键通了电",语义由抬起之后的短按/
    // 长按决定,所以这里只置个标志,不改变任何动作。
    if (ev == BSP_BTN_PRESS) {
        if (btn == BSP_BTN_DOWN) {
            s_flash_pending[FLASH_MINUS] = true;
        } else if (btn == BSP_BTN_UP) {
            s_flash_pending[FLASH_PLUS] = true;
        }
    }

    focus_key_t key;
    if (s_input_ready && s_input_queue != NULL && translate(btn, ev, &key)) {
        (void)xQueueSend(s_input_queue, &key, 0);
    }
}

// ---------------------------------------------------------------------------
// 心跳:消化按键、推进模型、刷新界面、落盘、响铃(跑在 LVGL 任务里)
// ---------------------------------------------------------------------------

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    bool redraw = false;
    bool save = false;

    focus_key_t key;
    while (s_input_queue != NULL && xQueueReceive(s_input_queue, &key, 0) == pdTRUE) {
        const uint32_t flags = focus_model_input(&s_model, key, now_ms);
        if (flags & FOCUS_FLAG_SLEEP) {
            s_sleep_request = true;
        }
        if (flags & FOCUS_FLAG_SAVE) {
            save = true;
        }
        redraw = true;
    }

    const uint32_t flags = focus_model_tick(&s_model, now_ms);
    if (flags & FOCUS_FLAG_REDRAW) {
        redraw = true;
    }
    if (flags & FOCUS_FLAG_SAVE) {
        save = true;
    }
    if (flags & FOCUS_FLAG_CHIME) {
        // 一个阶段走完。没有自动熄屏之后屏幕一定亮着,直接响就行。
        if (s_model.cfg.chime_on) {
            focus_chime_play();
        }
    }

    // 换水果:每轮专注抽一只新的,且不和刚吃掉的那只重复。信号由模型在"休息走完
    // 或跳过"与"放弃本轮"两处置位 —— 都是下一段专注露出水面**之前**的那一刻,所以屏幕上
    // 那只水果从出现到被吃完始终是同一只,抽签发生在没人看的那一帧。
    if (s_model.fruit_stale) {
        const uint8_t previous = s_model.fruit;
        const uint8_t picked = focus_fruit_pick(previous, esp_random());
        focus_model_set_fruit(&s_model, picked);
        redraw = true;
        save = true;  // 立刻落盘:抽签的"上一只"要活到下次开机,否则重启后可能又抽到同一种
        // 按键自身没有日志,这一行是"每轮专注确实换了一只、且没和刚吃掉的那只重复"
        // 在真机上唯一的证据。它落在休息刚走完(或被跳过)/刚放弃的那一刻,不是按下开始时。
        ESP_LOGI(TAG, "下一轮水果 #%u %s(刚吃掉的是 #%u)", (unsigned)picked,
                 focus_fruit_name(picked), (unsigned)previous);
    }

    // 按钮的按下反馈:这一拍收到标志就点亮,之后按截止时刻自己灭。它也是一件
    // "变了就得重绘"的事 —— 灭的那一拍如果别处都没变化,不把它算进去按钮就会
    // 一直亮着。
    uint8_t flash = 0;
    for (int i = 0; i < FLASH_COUNT; i++) {
        if (s_flash_pending[i]) {
            s_flash_pending[i] = false;
            s_flash_until_ms[i] = now_ms + FLASH_MS;
        }
        if (now_ms < s_flash_until_ms[i]) {
            flash |= FLASH_BIT[i];
        }
    }

    // 只要剩余秒数没变、电量没变、主视觉没换、也没有按键,就不必重排文字。
    // scene 这一项就是休息中那三帧的动画:它按已走过的时长算出这一刻该画哪一张
    // (见 focus_ui_scene),值一变就重绘,所以不需要为动画另开一个定时器。
    const uint32_t sec = focus_model_remaining_sec(&s_model, now_ms);
    const focus_scene_t scene = focus_ui_scene(&s_model, now_ms);
    const int battery = s_battery;
    if (redraw || sec != s_shown_sec || scene != s_shown_scene || battery != s_shown_battery ||
        flash != s_shown_flash) {
        focus_ui_refresh(&s_ui, &s_model, now_ms, battery, flash);
        s_shown_sec = sec;
        s_shown_scene = scene;
        s_shown_battery = battery;
        s_shown_flash = flash;
    }

    if (save) {
        // 落盘只发生在离散事件上(开始/放弃/跳过/改设置/阶段切换),不是每 100ms 一次。
        (void)focus_store_save(&s_model, now_ms);
    }
}

// ---------------------------------------------------------------------------
// 深睡
// ---------------------------------------------------------------------------

static void deep_sleep_step(const char *what, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "深睡准备步骤失败(%s): %s", what, esp_err_to_name(err));
    }
}

// 交回外设并入睡。正常不返回;被拒绝(按键被按住)时返回并已复位请求标志。
static void enter_deep_sleep(void)
{
    // 三键共用的 GPIO0 就是深睡唤醒脚。按住时低电平唤醒条件在入睡瞬间就成立,
    // 设备会立刻醒回来 —— 趁按键还活着,先按电压挡一次。
    const int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) {
        ESP_LOGW(TAG, "按键被按住(%d mV),本次休眠取消", mv);
        s_sleep_request = false;
        return;
    }

    ESP_LOGI(TAG, "进入深睡,按任意键唤醒");
    bsp_display_backlight(0);

    // 深睡等同重启,绝对截止时刻跨不过去 —— 先把状态折算成剩余秒数写下去。
    (void)focus_store_save(&s_model, esp_timer_get_time() / 1000);
    // 提示音任务必须先安静下来:I2S 引脚马上就要交回,不能再有人往里写。
    focus_chime_quiesce();

    // 以下每一步交回的外设都不可能在本次运行中恢复(见各 BSP 头文件的契约),
    // 所以失败只能重启 —— 回到界面也只会得到一个收不到按键、也不刷屏的死界面。
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) {
        ESP_LOGE(TAG, "按键交回失败,重启恢复外设");
        esp_restart();
    }
    // 任一键都把这脚拉到低电平,故按低电平武装。第一个参数是【位掩码】,不是引脚号。
    deep_sleep_step("GPIO wake arm",
                    esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO,
                                                      ESP_GPIO_WAKEUP_GPIO_LOW));

    // CW2017 与 ES8311 共用 I2C,必须先完成电量计写入/回读。
    deep_sleep_step("CW2017 suspend", bsp_battery_sleep());
    deep_sleep_step("ES8311 suspend", bsp_audio_sleep());
    // 即使 codec 寄存器操作失败,也继续停时钟并释放引脚。
    deep_sleep_step("I2S pin release", bsp_audio_prepare_deep_sleep());
    deep_sleep_step("shared I2C pin release", bsp_i2c_prepare_deep_sleep());

    // 等当前这一帧刷完再关屏,否则 ST7789 会在总线上被抽掉。
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "无法停止 LVGL 刷屏,重启恢复外设");
        esp_restart();
    }
    deep_sleep_step("ST7789 suspend", bsp_display_prepare_deep_sleep());

    esp_deep_sleep_start();
    // 走到这里说明没睡成,而外设已经交回,只能重启。
    ESP_LOGE(TAG, "esp_deep_sleep_start 意外返回,重启恢复外设");
    esp_restart();
}

// ---------------------------------------------------------------------------
// 功耗任务
// ---------------------------------------------------------------------------

static void power_task(void *arg)
{
    (void)arg;
    uint32_t since_battery_ms = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POWER_POLL_MS));

        if (s_sleep_request) {
            enter_deep_sleep();  // 被拒绝时已自行复位标志并返回
        }

        since_battery_ms += POWER_POLL_MS;
        if (since_battery_ms >= BATTERY_POLL_MS) {
            since_battery_ms = 0;
            s_battery = bsp_battery_soc();
        }
    }
}

// ---------------------------------------------------------------------------
// 启动
// ---------------------------------------------------------------------------

void focus_app_guard_wakeup(void)
{
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & ~(1u << ESP_SLEEP_WAKEUP_UNDEFINED)) {
        ESP_LOGI(TAG, "休眠唤醒原因位图: 0x%02x", (unsigned)wake_causes);
    }
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }
}

esp_err_t focus_app_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_started = true;

    focus_model_init(&s_model);
    if (focus_store_init() == ESP_OK) {
        // 没有存档、或存档损坏时 load 返回 false 且不动模型 —— 默认配置继续可用。
        if (!focus_store_load(&s_model)) {
            ESP_LOGI(TAG, "没有可用存档,按默认配置启动");
        } else if (s_model.fruit_stale) {
            // import 只在一种情况下置位这个信号:存档里那一轮正在走(RUNNING,或者老
            // 固件的"暂停")—— 绝对截止时刻跨不过一次重启,这一轮按番茄钟的规矩作废,
            // 落点是首页从头开始。这行日志是"重启确实把它丢掉了"在真机上唯一的证据:
            // 界面上看到的是干净的 25:00,分不出是"没接过"还是"接过又丢了"。
            ESP_LOGI(TAG, "存档里那一轮正在走:重启接不上,按规则作废,回首页从头开始");
        }
    }

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(focus_key_t));
    if (s_input_queue == NULL) {
        ESP_LOGE(TAG, "建按键队列失败");
        return ESP_ERR_NO_MEM;
    }

    // 音频与电量计属于"有更好、没有也能跑":失败只降级。
    if (bsp_audio_init() == ESP_OK) {
        (void)focus_chime_init();
    } else {
        ESP_LOGW(TAG, "音频初始化失败,本次运行没有提示音");
    }
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计未应答,顶栏显示 --");
    }
    s_battery = bsp_battery_soc();

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "取 LVGL 锁失败,无法建界面");
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_TIMEOUT;
    }
    focus_ui_create(&s_ui);
    focus_ui_refresh(&s_ui, &s_model, esp_timer_get_time() / 1000, s_battery, 0);
    // 心跳定时器跑在 LVGL 任务里:回调中已持 LVGL 锁,可以直接操作对象。
    s_tick_timer = lv_timer_create(tick_cb, TICK_PERIOD_MS, NULL);
    // 界面建完后报一次 LVGL 堆水位:池子被撑爆时 LVGL 只会静默少画东西,
    // 在设备上排查"少了个标签"时这一行是唯一的直接线索。
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    // 小池子不做碎片整理,一次整屏重绘要的是【最大连续块】而不是总空闲,所以
    // 峰值用量(max_used)和最大空闲块一起报 —— 判断整屏还塞不塞得下就靠它们。
    ESP_LOGI(TAG, "LVGL 堆:已用 %u 字节,空闲 %u,峰值 %u,最大空闲块 %u",
             (unsigned)mon.total_size - (unsigned)mon.free_size, (unsigned)mon.free_size,
             (unsigned)mon.max_used, (unsigned)mon.free_biggest_size);
    bsp_lvgl_unlock();

    if (s_tick_timer == NULL) {
        ESP_LOGE(TAG, "建心跳定时器失败,界面不会刷新");
    }
    // 功耗任务现在只剩两件事:执行手动休眠请求、轮询电量。
    if (xTaskCreate(power_task, "focus_power", POWER_TASK_STACK, NULL, POWER_TASK_PRIO, NULL) !=
        pdPASS) {
        ESP_LOGW(TAG, "建功耗任务失败,本次运行电量不会更新,也不能手动休眠");
    }

    const esp_err_t err = bsp_button_init(on_key, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    if (s_key_guard.armed) {
        // 唤醒键若在按键驱动启动前就松了手,组件从头到尾没看到按下,永远不发 release,
        // 守卫会一直拦到兜底。初始化完成后按真实电压同步一次:已松手立即撤防。
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手,提前撤防");
        }
    }
    s_input_ready = true;

    // 报出水果序号:断电/深睡恢复时能从这里看出存档里的水果是被读回来了,
    // 还是初始化成了 0 号(两种情况的界面不同,但都需要这一行才能判断)。
    ESP_LOGI(TAG, "番茄钟就绪:专注 %u 分钟 / 短休 %u 分钟 / 长休 %u 分钟 / 每 %u 个一循环"
                  " / 水果 #%u %s",
             (unsigned)s_model.cfg.focus_min, (unsigned)s_model.cfg.short_break_min,
             (unsigned)s_model.cfg.long_break_min, (unsigned)s_model.cfg.rounds_per_long,
             (unsigned)s_model.fruit, focus_fruit_name(s_model.fruit));
    return ESP_OK;
}
