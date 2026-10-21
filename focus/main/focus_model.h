// main/focus_model.h —— 番茄钟的纯逻辑状态机。
//
// 本文件不引用 LVGL、ESP-IDF 或 FreeRTOS:时间以毫秒入参给出,状态全部放在
// struct 里,因此可以用主机编译器直接跑单元测试(tests/test_focus_model.c)。
// 界面、NVS、按键分发都在各自的分层里,不要往这里塞。
//
// 时间模型:倒计时用「绝对截止时刻」表示,而不是每个周期自减。自减会在任务
// 调度抖动、丢帧、休眠唤醒后累积误差;截止时刻只跟单调时钟的差值有关。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 为了知道一共有几种水果(存档里的序号由模型负责夹住)。这个头文件同样是纯逻辑的,
// 不含 LVGL / ESP-IDF,不会把主机测试拖上依赖。
#include "focus_fruit_math.h"

// ---------------------------------------------------------------------------
// 可选项表(设置页用 OK 循环切换这些值)
// ---------------------------------------------------------------------------

#define FOCUS_SHORT_BREAK_OPTIONS_COUNT 3
#define FOCUS_LONG_BREAK_OPTIONS_COUNT 3
#define FOCUS_ROUNDS_OPTIONS_COUNT 3

extern const uint8_t FOCUS_SHORT_BREAK_OPTIONS_MIN[FOCUS_SHORT_BREAK_OPTIONS_COUNT];
extern const uint8_t FOCUS_LONG_BREAK_OPTIONS_MIN[FOCUS_LONG_BREAK_OPTIONS_COUNT];
extern const uint8_t FOCUS_ROUNDS_OPTIONS[FOCUS_ROUNDS_OPTIONS_COUNT];

// 专注时长不走"预设表 + 循环切换",而是首页上的加减:上键 +5、下键 -5。
// 单独的档位表在只有三档时好用,但用户要的是"任意 5 的倍数",列表枚举不出来。
// 边界与 config_in_range() 里的导入校验是同一组数,两处必须一起改 ——
// 否则会出现"设置时允许、存档读回来却判为坏数据"的档位。
#define FOCUS_FOCUS_MIN_STEP 5
#define FOCUS_FOCUS_MIN_MIN 5
#define FOCUS_FOCUS_MIN_MAX 90

// rounds_per_long 的上限 = FOCUS_ROUNDS_OPTIONS 里的最大值。界面按这个上限定长
// 循环点阵,导入校验也用它卡范围 —— 两处不复用同一个常量就会在坏数据上越界。
#define FOCUS_MAX_ROUNDS 6

#define FOCUS_MINUTE_MS 60000u

// 默认配置:25 分钟专注 / 5 分钟短休 / 15 分钟长休,每 4 个专注一次长休息。
// 25/5/15 就是番茄钟的经典配比,不是随便挑的整数 —— 这一版按规则来。
#define FOCUS_DEFAULT_FOCUS_MIN 25
#define FOCUS_DEFAULT_SHORT_BREAK_MIN 5
#define FOCUS_DEFAULT_LONG_BREAK_MIN 15
#define FOCUS_DEFAULT_ROUNDS_PER_LONG 4

// 落盘格式的魔数与版本。版本不匹配时整份状态作废回默认值,不做迁移。
#define FOCUS_PERSIST_MAGIC 0x504F4D32u  // 'POM2'
#define FOCUS_PERSIST_VERSION 1

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------

typedef enum {
    FOCUS_PHASE_FOCUS = 0,
    FOCUS_PHASE_SHORT_BREAK,
    FOCUS_PHASE_LONG_BREAK,
} focus_phase_t;

// 计时引擎的运行态。与界面页(FOCUS_VIEW_*)正交:RUNNING 时也可以停在
// 计时页之外的页面上,倒计时照走。
//
// 这里**没有"暂停"**。番茄钟的规则是一个番茄不可分割:一段专注被打断就不算数,只能
// 从头再来,所以计时中没有"先停一下、待会儿接着走"这回事 —— 计时页上的 OK 短按因此
// 什么也不做,要停下来只有长按,而长按在专注与休息上做的是两件事:专注中是放弃(这一
// 轮作废),休息中是跳过这段休息(已经走完的番茄照算,见 timer_long_press)。掉电与
// 深睡唤醒按同一条规则处理:绝对截止时刻跨不过一次重启,于是正在走的那一轮同样作废
// (见 focus_model_import)。规则和实现是同一件事的两面,动一边就得回头看另一边。
typedef enum {
    FOCUS_RUN_IDLE = 0,    // 未开始,等待 OK
    FOCUS_RUN_RUNNING,     // 倒计时进行中
    // 2 号空着:它曾经是"暂停"。存档里的 run 是一个整数,已经刷过的那版固件会在设备
    // 里留下写着 2 的存档;让 DONE 继续停在 3 上,读老存档时它就不会被认成另一个状态。
    // 读到 2 的处理在 focus_model_import —— 那等于"这一轮被打断过"。
    FOCUS_RUN_DONE = 3,    // 本阶段走完,等待 OK 进入下一阶段
} focus_run_t;

// 界面页。HOME 是根:在那里选时长、按 OK 才开始,倒计时跑完后回到这里重选。
// 没有"确认放弃"这一页:放弃是一次长按按下去就生效的动作 —— 弹一层问一遍,
// 换来的是一屏要读的字和一次多按,而放弃本来就要按住才能触发(见 timer_long_press)。
typedef enum {
    FOCUS_VIEW_HOME = 0,
    FOCUS_VIEW_TIMER,
    FOCUS_VIEW_SETTINGS,
} focus_view_t;

// 设置页的行。顺序即上下移动的顺序。
// 不在这一页里改专注时长 —— 那是首页上加减键的事,两处都能改同一个值只会让人
// 分不清哪一处说了算(见 focus_ui.c 的 build_home_screen)。
typedef enum {
    FOCUS_SETTING_SHORT_BREAK = 0,
    FOCUS_SETTING_LONG_BREAK,
    FOCUS_SETTING_ROUNDS,
    FOCUS_SETTING_CHIME,
    // 最后一行是动作,不是取值:上面四行是"把某个值改成下一个档",这一行按一下
    // 就把那四项连同首页的专注时长一起改回出厂值(见 focus_config_defaults)。
    // 放最后是因为它是这一页里唯一会一次改掉多件事的操作,离上面四行远一点。
    FOCUS_SETTING_RESET,
    FOCUS_SETTING_COUNT,
} focus_setting_t;

typedef struct {
    uint8_t focus_min;
    uint8_t short_break_min;
    uint8_t long_break_min;
    uint8_t rounds_per_long;
    bool chime_on;
} focus_config_t;

typedef struct {
    focus_config_t cfg;

    focus_phase_t phase;
    focus_run_t run;
    focus_view_t view;

    // run == FOCUS_RUN_RUNNING 时只有 deadline_ms 有意义;
    // 其余运行态用 remaining_ms。两者不会同时有效,避免"该信哪个"的歧义。
    int64_t deadline_ms;
    uint32_t remaining_ms;

    uint8_t cycle_done;    // 本轮循环已完成的专注个数,0..rounds_per_long
    uint32_t focus_total;  // 累计完成的专注个数(仅计数,不含统计图表)

    // 设置页
    uint8_t settings_row;

    // 计时页上正在被吃掉的那只水果(0..FOCUS_FRUIT_COUNT-1)。模型不懂形状,
    // 只负责"哪一只"这件事。它随存档落盘,用处是让"不和刚吃掉的那只重复"这条规矩
    // 跨得过一次重启:重开之后应用层拿它当"上一只",抽出来的仍然是另一种。
    uint8_t fruit;

    // "该换一种水果了"的瞬时信号,不落盘。
    //
    // 置位的地方有两处,都在**下一段专注出现之前**:休息走完(complete_break)、
    // 这一轮作废(reset_to_idle)。按下"开始"不置位 —— 那一屏上水果已经画在屏幕上了,
    // 此刻换会让它在用户眼皮底下变成另一只。
    bool fruit_stale;
} focus_model_t;

// 应用层把原始按键事件翻译成这些语义键后交给模型。
//
// 只有三个物理键,长按是第二个维度。底栏的提示行一律写成"单击 X · 长按 Y",
// 两句都指 OK 键,所以长按也跟着短按走:首页长按进设置,设置页长按回首页,
// 计时页长按按当前状态分发(专注中放弃、休息中跳过,见 timer_long_press)。
// 深睡不在这套说法里,它挂在长按上键上(见 focus_model_input 的入口分发)。
// OK 双击已经不用了 —— 同一件事有两种按法,提示行就得写两行,而底栏只留了一行。
typedef enum {
    FOCUS_KEY_UP = 0,
    FOCUS_KEY_DOWN,
    FOCUS_KEY_OK,
    FOCUS_KEY_UP_LONG,     // 上键长按:首页/设置页 -> 深睡,计时页未开始时 -> 设置页
    // 计时页上的 OK 长按。名字按"哪根键、在哪一页"取,不按"做什么"取 —— 这一个键在
    // 三个状态下做三件事(专注中放弃本轮 / 休息中跳过这段休息 / 未开始回首页),
    // 提示行写的就是当前那一种。
    FOCUS_KEY_TIMER_LONG,
    FOCUS_KEY_OK_LONG,     // OK 长按:首页 -> 设置页,设置页 -> 首页
} focus_key_t;

// 模型输出的动作位。应用层按位执行:重绘、落盘、响铃、进深睡。
#define FOCUS_FLAG_NONE 0u
#define FOCUS_FLAG_REDRAW (1u << 0)  // 界面需要重绘
#define FOCUS_FLAG_SAVE (1u << 1)    // 状态需要落盘
#define FOCUS_FLAG_CHIME (1u << 2)   // 一个阶段刚走完,提示音
#define FOCUS_FLAG_SLEEP (1u << 3)   // 请求进入深睡

// 掉电/深睡后恢复用的扁平快照。字段顺序即 NVS blob 的字节顺序,改动要同时
// 提升 FOCUS_PERSIST_VERSION。
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t focus_min;
    uint8_t short_break_min;
    uint8_t long_break_min;
    uint8_t rounds_per_long;
    uint8_t chime_on;
    uint8_t phase;
    uint8_t run;
    uint8_t cycle_done;
    // fruit 落在结构体里本来就存在的那一个填充字节上(偏移 14,后面 15 也是填充,
    // remaining_sec 仍在偏移 16)。这样老存档读出来 fruit = 0(香蕉)而不是整份作废。
    // 三条 _Static_assert 在 focus_model.c 里看着这个假设。
    uint8_t fruit;
    uint32_t remaining_sec;  // 绝对截止时刻跨不过重启,只存剩余秒数
    uint32_t focus_total;
} focus_persist_t;

// ---------------------------------------------------------------------------
// 接口
// ---------------------------------------------------------------------------

// 首页上的一次加减:up 为真加一步,否则减一步,结果夹在
// [FOCUS_FOCUS_MIN_MIN, FOCUS_FOCUS_MIN_MAX] 内。current 越界时先夹回范围内
// 再走一步,所以坏存档进来的值也能被一次操作拉回合法区间。
// 抽成纯函数是为了让"减到下限还会不会变成 255"这类事能在主机上直接断言。
uint8_t focus_model_step_minutes(uint8_t current, bool up);

// 把配置写回出厂值:专注 25 分钟、短休 5 分钟、长休 15 分钟、每 4 个一循环、
// 提示音开。设置页的"恢复默认"与 focus_model_init 都走它 —— 出厂值只在这一个
// 地方写一遍,否则改了默认时长就会有一处漏改,而漏掉的那处只在用户按下恢复时
// 才看得出来。
void focus_config_defaults(focus_config_t *cfg);

// 用默认配置初始化,进入 IDLE / FOCUS_PHASE_FOCUS,停在首页;remaining_ms 填满
// 专注时长。
void focus_model_init(focus_model_t *m);

// 在保持当前配置的前提下回到"准备开始专注",界面落回首页(见 .c 里的理由)。
// 放弃本轮走的就是它;重启后读到"当时正在走"的存档也走它 —— 那一轮按规则作废,
// 落点和放弃一样,不另开一条路。循环进度点阵不在这里动:主动放弃时由 timer_long_press
// 额外清零(放弃的是整轮循环),重启作废时保留(之前真正走完的番茄仍然算数)。
void focus_model_reset_to_idle(focus_model_t *m);

// 推进时间。返回 FOCUS_FLAG_* 位掩码;阶段走完时带 FOCUS_FLAG_CHIME。
uint32_t focus_model_tick(focus_model_t *m, int64_t now_ms);

// 处理一个语义按键。返回 FOCUS_FLAG_* 位掩码。
uint32_t focus_model_input(focus_model_t *m, focus_key_t key, int64_t now_ms);

// 当前剩余的毫秒数(RUNNING 时按截止时刻算,其余取 remaining_ms)。
uint32_t focus_model_remaining_ms(const focus_model_t *m, int64_t now_ms);

// 当前阶段的总时长(毫秒)。
uint32_t focus_model_total_ms(const focus_model_t *m);

// 已走过的比例,0..1000。用来决定水果被啃到第几档。
int focus_model_elapsed_permille(const focus_model_t *m, int64_t now_ms);

// 剩余秒数,向上取整 —— 显示用。按默认的 25 分钟就是 25:00 起跳,走到 0 之前
// 不会提前显示 00:00。
uint32_t focus_model_remaining_sec(const focus_model_t *m, int64_t now_ms);

// 快照导出/导入。导出时 RUNNING 以剩余秒数落盘(绝对截止时刻跨不过一次重启)。
// 导入时 RUNNING —— 以及老固件写下的那个"暂停"(run == 2)—— 都当作这一轮被打断:
// 作废、回首页从头开始,只有配置与已完成计数留下来。见 focus_model_import。
void focus_model_export(const focus_model_t *m, int64_t now_ms, focus_persist_t *out);

// 校验并导入快照。magic/version/取值范围任一不合法时返回 false 且不改动 m。
bool focus_model_import(focus_model_t *m, const focus_persist_t *in);

// 设定当前水果(应用层用 focus_fruit_pick() 挑好之后写回),同时清掉换水果信号。
// 序号越界按 0 处理。界面从 m->fruit 读它该画哪一只,不需要另存一份。
void focus_model_set_fruit(focus_model_t *m, uint8_t fruit);

// 阶段名,计时页左上角显示的就是它。专注那一档是应用名 "focus",休息两档是中文
// (短休息 / 长休息)。返回静态字符串,调用方不负责释放。
const char *focus_phase_name(focus_phase_t phase);
