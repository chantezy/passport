// main/focus_model.c —— 见 focus_model.h 的说明。
// 只依赖 <stddef.h> 的 NULL(纯 freestanding 头),不碰 LVGL / ESP-IDF,
// 这样 host 测试可以直接把它编进来跑。
#include <stddef.h>

#include "focus_model.h"

const uint8_t FOCUS_SHORT_BREAK_OPTIONS_MIN[FOCUS_SHORT_BREAK_OPTIONS_COUNT] = {3, 5, 10};
const uint8_t FOCUS_LONG_BREAK_OPTIONS_MIN[FOCUS_LONG_BREAK_OPTIONS_COUNT] = {10, 15, 30};
const uint8_t FOCUS_ROUNDS_OPTIONS[FOCUS_ROUNDS_OPTIONS_COUNT] = {2, 4, 6};

// fruit 之所以能直接加进 focus_persist_t 而不作废旧存档,全靠它落在原来的填充字节上、
// 且没把后面的字段顶走。这三条把它变成编译期约束:以后有人往中间插字段,或者
// 调整字段顺序,编译就会失败,而不是悄悄写出一份老固件读歪的存档。
// 诊断信息一律用 ASCII:本文件在 check_font_coverage.py 的扫描名单里(阶段名在这),
// 注释里的中文会被剥掉,但 _Static_assert 的消息是字符串字面量,会被当成界面文案
// 去字库里找 —— 找不到就判定成"少字形"。仓库里既有的 _Static_assert 消息也是英文。
_Static_assert(sizeof(focus_persist_t) == 24,
               "focus_persist_t must stay 24 bytes or saved settings are discarded");
_Static_assert(offsetof(focus_persist_t, fruit) == 14,
               "fruit must occupy the old padding byte");
_Static_assert(offsetof(focus_persist_t, remaining_sec) == 16,
               "remaining_sec offset must not move");

// ---------------------------------------------------------------------------
// 内部小工具
// ---------------------------------------------------------------------------

static uint32_t phase_total_ms(const focus_model_t *m)
{
    switch (m->phase) {
        case FOCUS_PHASE_SHORT_BREAK:
            return (uint32_t)m->cfg.short_break_min * FOCUS_MINUTE_MS;
        case FOCUS_PHASE_LONG_BREAK:
            return (uint32_t)m->cfg.long_break_min * FOCUS_MINUTE_MS;
        case FOCUS_PHASE_FOCUS:
        default:
            return (uint32_t)m->cfg.focus_min * FOCUS_MINUTE_MS;
    }
}

// 把 v 夹到 [0, total]。total 为 0 时返回 0。
static uint32_t clamp_to_total(uint32_t v, uint32_t total)
{
    return v > total ? total : v;
}

static uint8_t cycle_option(uint8_t current, const uint8_t *options, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        if (options[i] == current) {
            return options[(i + 1) % count];
        }
    }
    return options[0];
}

static uint8_t clamp_u8(uint8_t v, uint8_t lo, uint8_t hi)
{
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : v;
}

// remaining_ms 在非 RUNNING 态才有意义;这几个入口统一用它收口,
// 免得"改了时长却忘了同步剩余时间"散落在各处。
static void sync_remaining_to_phase(focus_model_t *m)
{
    if (m->run != FOCUS_RUN_RUNNING) {
        m->remaining_ms = phase_total_ms(m);
    }
}

static void enter_phase(focus_model_t *m, focus_phase_t phase, focus_run_t run)
{
    m->phase = phase;
    m->run = run;
    m->deadline_ms = 0;
    m->remaining_ms = phase_total_ms(m);
}

// 专注走完 -> 决定接下来是短休还是长休。
//
// 这里不换水果:水果只在专注阶段露脸,而接下来这一屏画的是结束卡、再下一屏画的是
// 睡觉三帧 —— 现在抽出来的那一只到"下一次专注"开始前就被重新抽掉,白抽一次。
// 更要紧的是**不能**在这里抽:那样"不和上一只重复"比的是上一只休息的水果,而不是
// 用户刚刚吃掉的那一只,连着两轮吃到同一种水果就成了可能。
static void complete_focus(focus_model_t *m)
{
    m->focus_total++;
    m->cycle_done++;
    if (m->cycle_done >= m->cfg.rounds_per_long) {
        enter_phase(m, FOCUS_PHASE_LONG_BREAK, FOCUS_RUN_DONE);
    } else {
        enter_phase(m, FOCUS_PHASE_SHORT_BREAK, FOCUS_RUN_DONE);
    }
}

// 休息走完 -> 长休结束时本轮循环归零,回到待开始的专注。
//
// 跳过这段休息(timer_long_press)走的也是它:对后面的番茄来说,"休息走完"和"休息被
// 跳过"没有区别 —— 跳过不是作废,已经走完的那几个番茄仍然算数,点阵照留。
//
// 下一只水果在这里定下来,而不是等用户按下"开始"的那一刻。顺序很关键:
//   · 这一屏(待开始的专注)已经画出了水果 —— 若等到按下才开始抽,屏幕上那只完整的
//     水果会当着用户的面变成另一只,像"按了开始把水果按坏了";
//   · 此刻换是看不见的:上一屏画的是结束卡或睡觉三帧,两张都不画水果,这一屏才第一次
//     露出水果。
// 于是屏幕上那只水果从出现到被吃完,始终是同一只;"换个水果"只发生在没人看的那一瞬。
static void complete_break(focus_model_t *m)
{
    if (m->phase == FOCUS_PHASE_LONG_BREAK) {
        m->cycle_done = 0;
    }
    enter_phase(m, FOCUS_PHASE_FOCUS, FOCUS_RUN_IDLE);
    m->fruit_stale = true;
}

// ---------------------------------------------------------------------------
// 初始化与重置
// ---------------------------------------------------------------------------

void focus_config_defaults(focus_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    *cfg = (focus_config_t){
        .focus_min = FOCUS_DEFAULT_FOCUS_MIN,
        .short_break_min = FOCUS_DEFAULT_SHORT_BREAK_MIN,
        .long_break_min = FOCUS_DEFAULT_LONG_BREAK_MIN,
        .rounds_per_long = FOCUS_DEFAULT_ROUNDS_PER_LONG,
        .chime_on = true,
    };
}

void focus_model_init(focus_model_t *m)
{
    if (m == NULL) {
        return;
    }
    *m = (focus_model_t){0};
    focus_config_defaults(&m->cfg);
    m->view = FOCUS_VIEW_HOME;
    enter_phase(m, FOCUS_PHASE_FOCUS, FOCUS_RUN_IDLE);
}

uint8_t focus_model_step_minutes(uint8_t current, bool up)
{
    // 先把越界的 current 夹回范围内再走一步:坏存档或以后改了边界时,
    // 加减一次就能回到合法区间,而不是先算出 250 再夹成 90。
    const uint8_t base = clamp_u8(current, FOCUS_FOCUS_MIN_MIN, FOCUS_FOCUS_MIN_MAX);
    if (up) {
        const int next = base + FOCUS_FOCUS_MIN_STEP;
        return (uint8_t)(next > FOCUS_FOCUS_MIN_MAX ? FOCUS_FOCUS_MIN_MAX : next);
    }
    return base >= FOCUS_FOCUS_MIN_MIN + FOCUS_FOCUS_MIN_STEP
               ? (uint8_t)(base - FOCUS_FOCUS_MIN_STEP)
               : FOCUS_FOCUS_MIN_MIN;
}

void focus_model_reset_to_idle(focus_model_t *m)
{
    if (m == NULL) {
        return;
    }
    // 保留 cfg 与累计完成数(focus_total):这两样都不属于"这一轮"。
    // 循环进度(cycle_done)这里也原样留着,由调用方决定它的去留 —— 用户主动放弃时
    // 由 timer_long_press 清零,开机时读到"当时正在走"的存档则保留:firmware 被打断的
    // 那一轮本来就没数进去,而在此之前真正走完的番茄仍然是真的。
    // 落回首页,不是计时页 —— 刚否掉这一轮,下一步就是重新挑时长再开始,把人留在
    // 计时页上还得再长按一次才回得去。首页正是"选时长、按 OK 开始"的地方。
    m->view = FOCUS_VIEW_HOME;
    m->settings_row = 0;
    enter_phase(m, FOCUS_PHASE_FOCUS, FOCUS_RUN_IDLE);
    // 放弃本轮也算"下一段专注要开始了",顺手换一只 —— 和 complete_break 同一个道理。
    // 这一步同样看不见:落点是首页,那一页不画水果。不换的话,反复放弃再开始的人会
    // 一直吃着同一只水果,而"每轮换一只"正是这只水果存在的意思。
    m->fruit_stale = true;
}

// ---------------------------------------------------------------------------
// 时间查询
// ---------------------------------------------------------------------------

uint32_t focus_model_total_ms(const focus_model_t *m)
{
    return m == NULL ? 0 : phase_total_ms(m);
}

uint32_t focus_model_remaining_ms(const focus_model_t *m, int64_t now_ms)
{
    if (m == NULL) {
        return 0;
    }
    if (m->run != FOCUS_RUN_RUNNING) {
        return clamp_to_total(m->remaining_ms, phase_total_ms(m));
    }
    const int64_t left = m->deadline_ms - now_ms;
    if (left <= 0) {
        return 0;
    }
    return clamp_to_total((uint32_t)left, phase_total_ms(m));
}

uint32_t focus_model_remaining_sec(const focus_model_t *m, int64_t now_ms)
{
    // 向上取整:刚点开始时显示满值,读到最后 1 秒内才显示 00:00。
    return (focus_model_remaining_ms(m, now_ms) + 999u) / 1000u;
}

int focus_model_elapsed_permille(const focus_model_t *m, int64_t now_ms)
{
    const uint32_t total = focus_model_total_ms(m);
    if (total == 0) {
        return 0;
    }
    const uint32_t left = focus_model_remaining_ms(m, now_ms);
    const uint32_t elapsed = total > left ? total - left : 0;
    // total 最大 90 分钟 = 5_400_000,乘 1000 不会溢出 uint32。
    return (int)((elapsed * 1000u) / total);
}

// ---------------------------------------------------------------------------
// 推进
// ---------------------------------------------------------------------------

uint32_t focus_model_tick(focus_model_t *m, int64_t now_ms)
{
    if (m == NULL) {
        return FOCUS_FLAG_NONE;
    }

    if (m->run != FOCUS_RUN_RUNNING || now_ms < m->deadline_ms) {
        return FOCUS_FLAG_NONE;
    }

    if (m->phase == FOCUS_PHASE_FOCUS) {
        complete_focus(m);
    } else {
        complete_break(m);
    }
    return FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME;
}

// ---------------------------------------------------------------------------
// 按键
// ---------------------------------------------------------------------------

static uint32_t input_settings(focus_model_t *m, focus_key_t key)
{
    switch (key) {
        case FOCUS_KEY_OK_LONG:
            // 提示行写的就是这个键:"单击修改 · 长按返回"。长按和短按落在同一个键上,
            // 否则屏幕上写着一件事、手上要按另一件事(见 focus_model_input 的分发)。
            // 设置页只从首页进得来(计时页未开始时也能进),所以回首页不会有
            // "回到一个没见过的页面"的突兀感。
            m->view = FOCUS_VIEW_HOME;
            return FOCUS_FLAG_REDRAW;
        case FOCUS_KEY_UP:
            m->settings_row = (uint8_t)((m->settings_row + FOCUS_SETTING_COUNT - 1) % FOCUS_SETTING_COUNT);
            return FOCUS_FLAG_REDRAW;
        case FOCUS_KEY_DOWN:
            m->settings_row = (uint8_t)((m->settings_row + 1) % FOCUS_SETTING_COUNT);
            return FOCUS_FLAG_REDRAW;
        case FOCUS_KEY_OK: {
            switch ((focus_setting_t)m->settings_row) {
                case FOCUS_SETTING_SHORT_BREAK:
                    m->cfg.short_break_min = cycle_option(m->cfg.short_break_min,
                                                          FOCUS_SHORT_BREAK_OPTIONS_MIN,
                                                          FOCUS_SHORT_BREAK_OPTIONS_COUNT);
                    break;
                case FOCUS_SETTING_LONG_BREAK:
                    m->cfg.long_break_min = cycle_option(m->cfg.long_break_min,
                                                         FOCUS_LONG_BREAK_OPTIONS_MIN,
                                                         FOCUS_LONG_BREAK_OPTIONS_COUNT);
                    break;
                case FOCUS_SETTING_ROUNDS:
                    m->cfg.rounds_per_long = cycle_option(m->cfg.rounds_per_long, FOCUS_ROUNDS_OPTIONS,
                                                          FOCUS_ROUNDS_OPTIONS_COUNT);
                    if (m->cycle_done > m->cfg.rounds_per_long) {
                        m->cycle_done = m->cfg.rounds_per_long;
                    }
                    break;
                case FOCUS_SETTING_CHIME:
                    m->cfg.chime_on = !m->cfg.chime_on;
                    break;
                case FOCUS_SETTING_RESET:
                    // 一次改回全部五项,连首页那个专注时长一起 —— "恢复默认"如果
                    // 漏掉时长,用户按下之后还得自己想起去首页减回来。
                    // 不做二次确认:这一页上单击本来就是"改",而改错只是再按几下
                    // 加号就能回来;为这一行单独加一层弹窗,比按错一次的代价更大。
                    focus_config_defaults(&m->cfg);
                    // 循环进度跟着新的"几个一循环"收一下,否则 cycle_done 会大于
                    // rounds_per_long,点阵按 rounds_per_long 画、进度却按 cycle_done
                    // 填,会点满一整排。
                    if (m->cycle_done > m->cfg.rounds_per_long) {
                        m->cycle_done = m->cfg.rounds_per_long;
                    }
                    break;
                case FOCUS_SETTING_COUNT:
                default:
                    return FOCUS_FLAG_NONE;
            }
            sync_remaining_to_phase(m);
            return FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE;
        }
        default:
            // 设置页里除上下选择、OK 修改、OK 长按返回之外的按键不做事;
            // 长按上键的深睡在 focus_model_input 的入口就已经返回了。
            return FOCUS_FLAG_NONE;
    }
}

// 开始当前阶段。首页上按 OK、计时页上按 OK 都走这里 —— 两处的差别只是页面,
// 计时的起点是同一个。
static uint32_t start_phase(focus_model_t *m, int64_t now_ms)
{
    m->deadline_ms = now_ms + (int64_t)phase_total_ms(m);
    m->remaining_ms = 0;
    m->run = FOCUS_RUN_RUNNING;
    // 这里**不**换水果:屏幕上那只水果从它出现的那一刻起就已经定了(见 complete_break
    // 与 focus_model_reset_to_idle),按下开始只是让它开始被吃。抽签放在这一步会让
    // 待开始那一屏上完整的果子在按下的一瞬间变成另一只。
    return FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE;
}

// 首页:选时长、看标题、按 OK 才开始。
static uint32_t input_home(focus_model_t *m, focus_key_t key, int64_t now_ms)
{
    switch (key) {
        case FOCUS_KEY_UP:
            m->cfg.focus_min = focus_model_step_minutes(m->cfg.focus_min, true);
            sync_remaining_to_phase(m);
            return FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE;
        case FOCUS_KEY_DOWN:
            m->cfg.focus_min = focus_model_step_minutes(m->cfg.focus_min, false);
            sync_remaining_to_phase(m);
            return FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE;
        case FOCUS_KEY_OK:
            if (m->run != FOCUS_RUN_IDLE && m->run != FOCUS_RUN_DONE) {
                // 理论上走不到:首页只在未开始时显示。留一条拒绝路径比默默改状态好。
                return FOCUS_FLAG_NONE;
            }
            m->view = FOCUS_VIEW_TIMER;
            return start_phase(m, now_ms);
        case FOCUS_KEY_OK_LONG:
            // "单击开始 · 长按设置" —— 两件事都在这一个键上,和提示行一一对应。
            m->view = FOCUS_VIEW_SETTINGS;
            m->settings_row = FOCUS_SETTING_SHORT_BREAK;
            return FOCUS_FLAG_REDRAW;
        default:
            return FOCUS_FLAG_NONE;
    }
}

// 跳过这段休息:直接按"休息已经走完"处理 —— 进入下一个待开始的专注,并抽下一只水果
// (见 complete_break)。等一会儿再让用户自己按开始,而不是替他把它开起来:跳过休息
// 的人想接着干活,但不一定想立刻开始。
//
// 这不是"放弃":跳过休息没有作废任何东西,已经走完的那几个番茄仍然算数,点阵照留
// (跳过长休则本轮归零,和长休真的走完是同一条规则),累计完成数也不动。
// 落点是计时页上那个待开始的番茄钟,不是首页 —— 首页是"重新挑时长"的地方,而用户
// 只是不想休息,没有要改时长;把人送回去等于每跳一次休息都要再过一遍选时长那一屏。
static uint32_t skip_break(focus_model_t *m)
{
    complete_break(m);
    return FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE;
}

// 计时页上的 OK 长按。同一个键在不同状态下做不同的事,提示行写的就是当前那一种:
//   专注进行中       —— 放弃本轮:这一轮循环到此为止,回首页重新开始;
//   休息(两种可能)  —— 跳过这段休息:直接进入下一个待开始的番茄钟。
// 休息那一支只看 phase 就够:阶段在专注刚走完那一刻就已经切到接下来那次休息上,
// 所以 phase 是休息时,run == DONE 说的是"这次休息还没开始",run == RUNNING 说的是
// "它正在走" —— 两种都是"跳过这段休息",不必分开写。
static uint32_t timer_long_press(focus_model_t *m)
{
    if (m->phase != FOCUS_PHASE_FOCUS) {
        return skip_break(m);
    }

    switch (m->run) {
        case FOCUS_RUN_RUNNING:
        case FOCUS_RUN_DONE:
            focus_model_reset_to_idle(m);
            // 放弃 = 这一轮循环到此为止,不是"这一段不走了、攒下的还留着"。
            //
            // 点阵上那几个点说的是"这一轮循环里已经走完了几个专注",而这一轮循环
            // 正是被放弃掉的那个:留着它们,用户回到首页再进来,看到的还是一副
            // "你已经攒了几个"的样子,像是这次放弃没生效 —— 要开新的一轮,就得是
            // 干净的第一格。所以这里连循环进度一起清掉。
            //
            // 这一点正是跳过休息与放弃的分界:想跳过休息、又想把攒下的番茄留住,
            // 走的是上面那条路(phase 是休息);只有"这一轮不干了"才落到这里。
            //
            // 累计完成数(focus_total)不在这里动:它是这台设备上的历史,不属于任何
            // 一轮循环,而且界面上根本没有地方显示它。
            m->cycle_done = 0;
            return FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE;
        case FOCUS_RUN_IDLE:
        default:
            // 还没开始,没有"本轮"可放弃 —— 回首页那一屏去。只拨 view 不动阶段:
            // 阶段本来就是专注,首页那颗 OK 要开的就是它。
            m->view = FOCUS_VIEW_HOME;
            return FOCUS_FLAG_REDRAW;
    }
}

static uint32_t input_timer(focus_model_t *m, focus_key_t key, int64_t now_ms)
{
    switch (key) {
        case FOCUS_KEY_UP_LONG:
            // 只在没有在跑的计时时进设置页,免得把倒计时藏到看不见的地方。
            if (m->run == FOCUS_RUN_IDLE) {
                m->view = FOCUS_VIEW_SETTINGS;
                m->settings_row = FOCUS_SETTING_SHORT_BREAK;
                return FOCUS_FLAG_REDRAW;
            }
            return FOCUS_FLAG_NONE;

        case FOCUS_KEY_TIMER_LONG:
            return timer_long_press(m);

        case FOCUS_KEY_OK:
            switch (m->run) {
                case FOCUS_RUN_IDLE:
                case FOCUS_RUN_DONE:
                    return start_phase(m, now_ms);
                case FOCUS_RUN_RUNNING:
                    // 正在走的时候按一下什么也不做。这里原来钉着"暂停",去掉它是因为
                    // 番茄钟的规则里没有暂停:一个番茄被打断就不算数,所以一段计时只该
                    // 有"让它走完"和"不走了"两种出路,而后一种本来就是长按 —— 专注上是
                    // 放弃,休息上是跳过(见 format_hint)。
                    return FOCUS_FLAG_NONE;
                default:
                    return FOCUS_FLAG_NONE;
            }

        case FOCUS_KEY_UP:
        case FOCUS_KEY_DOWN:
            // 计时页上上下键不做事:时长归首页管,这里改会让人以为改的是当前这一轮。
            return FOCUS_FLAG_NONE;

        default:
            return FOCUS_FLAG_NONE;
    }
}

uint32_t focus_model_input(focus_model_t *m, focus_key_t key, int64_t now_ms)
{
    if (m == NULL) {
        return FOCUS_FLAG_NONE;
    }
    // 长按哪根键,取决于这一页的提示行把"长按"写在哪个动作上。
    //
    // 首页与设置页的提示行是"单击开始 · 长按设置"/"单击修改 · 长按返回":两件事
    // 都指向 OK 键,所以这两页上长按 OK 就是提示里那件事(由 input_home /
    // input_settings 各自实现),深睡挪到长按上键 —— 那根键在这两页上没有任何
    // 短按含义,拿走它的长按不会和屏幕上的字打架。深睡本身不写进提示行:断电恢复
    // 已经做过了(见 focus_model_import),这是个"知道就用"的手势,不是要教的步骤。
    //
    // 计时页上,长按 OK 换成 FOCUS_KEY_TIMER_LONG —— 它到那一页才由 timer_long_press
    // 按当前状态决定是放弃还是跳过。
    if (key == FOCUS_KEY_UP_LONG &&
        (m->view == FOCUS_VIEW_HOME || m->view == FOCUS_VIEW_SETTINGS)) {
        return FOCUS_FLAG_SLEEP;
    }
    if (key == FOCUS_KEY_OK_LONG && m->view == FOCUS_VIEW_TIMER) {
        key = FOCUS_KEY_TIMER_LONG;
    }

    switch (m->view) {
        case FOCUS_VIEW_HOME:
            return input_home(m, key, now_ms);
        case FOCUS_VIEW_SETTINGS:
            return input_settings(m, key);
        case FOCUS_VIEW_TIMER:
        default:
            return input_timer(m, key, now_ms);
    }
}

// ---------------------------------------------------------------------------
// 快照
// ---------------------------------------------------------------------------

void focus_model_export(const focus_model_t *m, int64_t now_ms, focus_persist_t *out)
{
    if (m == NULL || out == NULL) {
        return;
    }
    const uint32_t remaining_ms = focus_model_remaining_ms(m, now_ms);
    *out = (focus_persist_t){0};
    out->magic = FOCUS_PERSIST_MAGIC;
    out->version = FOCUS_PERSIST_VERSION;
    out->focus_min = m->cfg.focus_min;
    out->short_break_min = m->cfg.short_break_min;
    out->long_break_min = m->cfg.long_break_min;
    out->rounds_per_long = m->cfg.rounds_per_long;
    out->chime_on = m->cfg.chime_on ? 1u : 0u;
    out->phase = (uint8_t)m->phase;
    out->run = (uint8_t)m->run;
    out->cycle_done = m->cycle_done;
    out->fruit = m->fruit;
    out->remaining_sec = (remaining_ms + 999u) / 1000u;
    out->focus_total = m->focus_total;
}

static bool config_in_range(const focus_persist_t *p)
{
    if (p->focus_min < 5 || p->focus_min > 90) return false;
    if (p->short_break_min < 1 || p->short_break_min > 30) return false;
    if (p->long_break_min < 5 || p->long_break_min > 60) return false;
    if (p->rounds_per_long < 1 || p->rounds_per_long > FOCUS_MAX_ROUNDS) return false;
    if (p->phase > (uint8_t)FOCUS_PHASE_LONG_BREAK) return false;
    // run **不**在这里查。它不在"配置"这一组里:一个认不出的运行态最多说明那一轮
    // 没法接着走(本来也接不下去),而这里返回 false 会让整份存档作废 —— 为一个
    // 运行态丢掉用户的时长设置,代价不对等。认不出的 run 由 import 当作"被打断"处理。
    if (p->cycle_done > p->rounds_per_long) return false;
    return true;
}

bool focus_model_import(focus_model_t *m, const focus_persist_t *in)
{
    if (m == NULL || in == NULL) {
        return false;
    }
    if (in->magic != FOCUS_PERSIST_MAGIC || in->version != FOCUS_PERSIST_VERSION) {
        return false;
    }
    if (!config_in_range(in)) {
        return false;
    }

    focus_model_init(m);
    m->cfg.focus_min = in->focus_min;
    m->cfg.short_break_min = in->short_break_min;
    m->cfg.long_break_min = in->long_break_min;
    m->cfg.rounds_per_long = in->rounds_per_long;
    m->cfg.chime_on = in->chime_on != 0;
    m->phase = (focus_phase_t)in->phase;
    m->cycle_done = in->cycle_done;
    m->focus_total = in->focus_total;
    // 水果序号坏了只当 0 号处理,不据此否掉整份存档 —— 它只是画哪只水果,
    // 为它丢掉用户的时长设置不值得。老存档这里是填充字节,读出来就是 0。
    m->fruit = in->fruit < FOCUS_FRUIT_COUNT ? in->fruit : 0;

    // 只有两种运行态跨得过一次重启:
    //   IDLE —— 还没开始,存档里的时长与计数照收;
    //   DONE —— 那一段已经走完、提示音也响过了,回来接着看"该开休息了"是对的。
    // 正在走的那一轮(RUNNING)作废。绝对截止时刻跨不过一次重启,存下来的只剩一个
    // 剩余秒数,照着它接着走等于替用户把被打断的那一半补上 —— 而"被打断的番茄不算数"
    // 正是去掉暂停要落的那条规则。作废走的是放弃那条路,连落点都一样:首页重新开始。
    // 老固件写下的"暂停"(run == 2,现在这个号空着)和任何认不出的值都落在这里。
    switch ((focus_run_t)in->run) {
        case FOCUS_RUN_IDLE:
        case FOCUS_RUN_DONE:
            m->run = (focus_run_t)in->run;
            m->remaining_ms = clamp_to_total(in->remaining_sec * 1000u, phase_total_ms(m));
            m->deadline_ms = 0;
            // 停着的(未开始)回首页 —— 那里才是"选时长、按 OK 开始"的地方,落在计时页上
            // 只会看到一只完整的水果和一行"单击开始",却找不到改时长的地方。
            m->view = (m->run == FOCUS_RUN_IDLE) ? FOCUS_VIEW_HOME : FOCUS_VIEW_TIMER;
            break;
        default:
            focus_model_reset_to_idle(m);
            break;
    }
    return true;
}

void focus_model_set_fruit(focus_model_t *m, uint8_t fruit)
{
    if (m == NULL) {
        return;
    }
    m->fruit = fruit < FOCUS_FRUIT_COUNT ? fruit : 0;
    m->fruit_stale = false;  // 换过了,信号清掉
}

const char *focus_phase_name(focus_phase_t phase)
{
    switch (phase) {
        // 专注那一档用应用自己的名字,不用中文 —— 计时页左上角显示的就是它,
        // 和首页那个像素字标题是同一个词。休息两档仍是中文:那两个词没有对应的
        // 品牌写法,也没有别的画面元素在说这件事。
        case FOCUS_PHASE_SHORT_BREAK: return "短休息";
        case FOCUS_PHASE_LONG_BREAK:  return "长休息";
        case FOCUS_PHASE_FOCUS:
        default:                     return "focus";
    }
}
