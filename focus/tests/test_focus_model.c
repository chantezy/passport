// 番茄钟状态机的 host 测试:不依赖 SDK / LVGL,直接编 main/focus_model.c 跑纯逻辑。
//
// 时间全部用假的 now_ms 传入,所以这些用例是确定性的:不用 sleep、不受机器快慢影响。
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "focus_model.h"

#define MIN_MS (60 * 1000)

// 走到当前阶段的截止时刻并返回 tick 的动作位。
static uint32_t run_to_deadline(focus_model_t *m, int64_t now_ms)
{
    const int64_t deadline = m->deadline_ms;
    assert(deadline > now_ms);              // 调用点应该还在倒计时中间
    assert(focus_model_tick(m, deadline - 1) == FOCUS_FLAG_NONE);  // 差 1ms 不算走完
    return focus_model_tick(m, deadline);
}

int main(void) {
    focus_model_t m;

    // ---- 初始状态:停在首页,默认 25 分钟(番茄钟的经典时长)----
    focus_model_init(&m);
    assert(m.view == FOCUS_VIEW_HOME);
    assert(m.phase == FOCUS_PHASE_FOCUS);
    assert(m.run == FOCUS_RUN_IDLE);
    assert(m.cfg.focus_min == 25 && m.cfg.short_break_min == 5 && m.cfg.long_break_min == 15);
    assert(m.cfg.rounds_per_long == 4 && m.cfg.chime_on);
    assert(m.cycle_done == 0 && m.focus_total == 0);
    assert(focus_model_total_ms(&m) == 25u * MIN_MS);
    assert(focus_model_remaining_ms(&m, 0) == 25u * MIN_MS);
    assert(focus_model_remaining_sec(&m, 0) == 25u * 60u);
    assert(focus_model_elapsed_permille(&m, 0) == 0);

    // ---- 首页上的加减:±5,夹在 [5, 90] 内 ----
    assert(focus_model_step_minutes(25, true) == 30);
    assert(focus_model_step_minutes(25, false) == 20);
    assert(focus_model_step_minutes(FOCUS_FOCUS_MIN_MAX, true) == FOCUS_FOCUS_MIN_MAX);
    assert(focus_model_step_minutes(FOCUS_FOCUS_MIN_MIN, false) == FOCUS_FOCUS_MIN_MIN);
    // 越界输入先夹回范围再走一步:坏存档(200)按一下减号不会变成 195,也不会绕回 250。
    assert(focus_model_step_minutes(200, true) == FOCUS_FOCUS_MIN_MAX);
    assert(focus_model_step_minutes(200, false) == FOCUS_FOCUS_MIN_MAX - FOCUS_FOCUS_MIN_STEP);
    assert(focus_model_step_minutes(0, false) == FOCUS_FOCUS_MIN_MIN);

    // 加一步立刻反映到剩余时间上(没开始之前 remaining 就是"待开始的那个时长")。
    assert(focus_model_input(&m, FOCUS_KEY_UP, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.focus_min == 30);
    assert(focus_model_remaining_ms(&m, 0) == 30u * MIN_MS);
    assert(focus_model_input(&m, FOCUS_KEY_DOWN, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.focus_min == 25);
    assert(focus_model_remaining_ms(&m, 0) == 25u * MIN_MS);

    // ---- 开始,然后一直走到结束:番茄钟没有暂停 ----
    // 首页上按 OK = 切到计时页并开始。
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.view == FOCUS_VIEW_TIMER);
    assert(m.run == FOCUS_RUN_RUNNING && m.deadline_ms == 25LL * MIN_MS);

    // 走到第 6 分钟:已过 6/25 = 240‰。
    assert(focus_model_tick(&m, 6 * MIN_MS) == FOCUS_FLAG_NONE);
    assert(focus_model_remaining_sec(&m, 6 * MIN_MS) == 19u * 60u);
    assert(focus_model_elapsed_permille(&m, 6 * MIN_MS) == 240);

    // 计时中按 OK 什么也不做:一个番茄不可分割,被打断就不算数,所以没有"先停一下、
    // 待会儿接着走"这条路。退回 NONE(连重绘都不要),模型一个字段都不动 —— 这里的
    // 断言点正是"什么都没变",包括那个绝对截止时刻仍然钉在 25 分钟上。
    const int64_t mid_at = 6 * MIN_MS;
    const int64_t deadline_before = m.deadline_ms;
    assert(focus_model_input(&m, FOCUS_KEY_OK, mid_at) == FOCUS_FLAG_NONE);
    assert(focus_model_input(&m, FOCUS_KEY_UP, mid_at) == FOCUS_FLAG_NONE);
    assert(focus_model_input(&m, FOCUS_KEY_DOWN, mid_at) == FOCUS_FLAG_NONE);
    assert(m.run == FOCUS_RUN_RUNNING && m.deadline_ms == deadline_before);
    assert(focus_model_remaining_ms(&m, mid_at) == 19u * MIN_MS);
    assert(focus_model_remaining_ms(&m, 60 * MIN_MS) == 0);  // 时间照走,没人能把它停住

    // 倒计时最后一毫秒向上取整成 1 秒,不会提前显示 00:00。
    const int64_t resume_at = 6 * MIN_MS;
    const int64_t focus_end = m.deadline_ms;
    assert(focus_model_remaining_sec(&m, focus_end - 1) == 1);
    assert(focus_model_remaining_sec(&m, focus_end) == 0);

    // ---- 专注走完 -> 短休息 ----
    assert(run_to_deadline(&m, resume_at) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.phase == FOCUS_PHASE_SHORT_BREAK && m.run == FOCUS_RUN_DONE);
    assert(m.focus_total == 1 && m.cycle_done == 1);
    assert(focus_model_remaining_ms(&m, focus_end) == 5u * MIN_MS);
    assert(focus_model_elapsed_permille(&m, focus_end) == 0);  // 新阶段从 0 开始

    // 休息完成计数不加,回到待开始的专注。
    assert(focus_model_input(&m, FOCUS_KEY_OK, focus_end) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, focus_end) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.phase == FOCUS_PHASE_FOCUS && m.run == FOCUS_RUN_IDLE);
    assert(m.focus_total == 1 && m.cycle_done == 1);

    // ---- 每 rounds_per_long 个专注换一次长休息 ----
    focus_model_init(&m);
    m.cfg.rounds_per_long = 2;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.phase == FOCUS_PHASE_SHORT_BREAK && m.cycle_done == 1);

    // 让这次短休走完,再开始第二个专注 —— 通向长休的路因此是"休息真的走完"。
    // 长按跳过这段短休走的是同一条路(见下面"跳过休息"那一组),这里走这一条是为了
    // 顺带盯住"休息走完"本身。
    const int64_t t1 = m.deadline_ms;
    assert(focus_model_input(&m, FOCUS_KEY_OK, t1) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, t1) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.phase == FOCUS_PHASE_FOCUS && m.run == FOCUS_RUN_IDLE && m.cycle_done == 1);
    assert(focus_model_input(&m, FOCUS_KEY_OK, t1) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, t1) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.phase == FOCUS_PHASE_LONG_BREAK && m.cycle_done == 2 && m.focus_total == 2);
    assert(focus_model_remaining_ms(&m, 0) == 15u * MIN_MS);

    // 长休息结束后本轮循环归零,计数保留。
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.phase == FOCUS_PHASE_FOCUS && m.run == FOCUS_RUN_IDLE);
    assert(m.cycle_done == 0 && m.focus_total == 2);

    // ---- 放弃本轮:长按即弃,没有二次确认 ----
    focus_model_init(&m);
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    const int64_t t2 = 3 * MIN_MS;
    assert(focus_model_tick(&m, t2) == FOCUS_FLAG_NONE);

    // 计时中长按,直接放弃并回首页:原来这里先弹一屏"放弃本轮?",现在不弹了 ——
    // 长按本身就是那道门槛(计时页上短按什么也不做,手滑按一下不会弃掉任何东西),
    // 再问一遍只是让人多按一次。这一份状态本来就没攒下任何完成数,放弃之后循环
    // 进度当然是干净的:0 个点。
    assert(focus_model_input(&m, FOCUS_KEY_TIMER_LONG, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.view == FOCUS_VIEW_HOME && m.run == FOCUS_RUN_IDLE);
    assert(m.phase == FOCUS_PHASE_FOCUS && m.focus_total == 0 && m.cycle_done == 0);
    assert(focus_model_remaining_ms(&m, t2) == 25u * MIN_MS);

    // 放弃之后再开一轮、再放弃一次:连做两遍也是干净的(上一轮的残余没有留下任何
    // 能让下一轮"接着走"的东西)。这一条替代了原来那个"暂停中长按"的用例 ——
    // 中间那个可停留的状态已经不存在了。
    assert(focus_model_input(&m, FOCUS_KEY_OK, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.run == FOCUS_RUN_RUNNING && m.deadline_ms == t2 + 25LL * MIN_MS);
    assert(focus_model_input(&m, FOCUS_KEY_TIMER_LONG, t2 + 60 * 1000) ==
           (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.view == FOCUS_VIEW_HOME && m.run == FOCUS_RUN_IDLE && m.focus_total == 0);
    assert(focus_model_remaining_ms(&m, t2) == 25u * MIN_MS);

    // ---- 跳过休息:长按即跳,点阵照留 ----
    // 刚走完、还没开休息时长按:跳过这次休息。落点是计时页上那个待开始的番茄钟,
    // 不是首页 —— 首页是"重新挑时长"的地方,而跳过休息的人只是不想休息,没有要改
    // 时长(见 timer_long_press)。
    focus_model_init(&m);
    assert(focus_model_input(&m, FOCUS_KEY_OK, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.run == FOCUS_RUN_DONE && m.phase == FOCUS_PHASE_SHORT_BREAK);
    assert(m.cycle_done == 1);  // 这个番茄是真走完了的,点阵上亮一个
    assert(!m.fruit_stale);     // 结束卡那一屏不画水果,此刻还轮不到换
    assert(focus_model_input(&m, FOCUS_KEY_TIMER_LONG, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.phase == FOCUS_PHASE_FOCUS && m.run == FOCUS_RUN_IDLE);
    assert(m.view == FOCUS_VIEW_TIMER);               // 留在计时页:下一个番茄钟已经摆好
    assert(m.cycle_done == 1 && m.focus_total == 1);  // 跳过不是放弃,进度不清零
    assert(m.fruit_stale);                            // 下一屏要画水果了,换水果在这里置位
    assert(focus_model_remaining_ms(&m, t2) == 25u * MIN_MS);

    // 休息已经在走的时候长按,同样是跳过:落点、进度、换水果与上面完全一样。
    // 两种状态一个是"待开始的一次休息"、一个是"正在走的一次休息",对后面的番茄
    // 没有任何区别,所以同一条路。
    focus_model_init(&m);
    assert(focus_model_input(&m, FOCUS_KEY_OK, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(focus_model_input(&m, FOCUS_KEY_OK, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.phase == FOCUS_PHASE_SHORT_BREAK && m.run == FOCUS_RUN_RUNNING);
    focus_model_set_fruit(&m, 2);  // 上一屏画完后,应用层这样把换水果信号清掉
    assert(m.fruit == 2 && !m.fruit_stale);
    // 这次休息真走了 1 分钟,跳过时那点时间不做任何补偿 —— 只在换完水果之后才长按。
    assert(focus_model_input(&m, FOCUS_KEY_TIMER_LONG, t2 + 60 * 1000) ==
           (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.phase == FOCUS_PHASE_FOCUS && m.run == FOCUS_RUN_IDLE && m.view == FOCUS_VIEW_TIMER);
    assert(m.cycle_done == 1 && m.focus_total == 1 && m.fruit == 2 && m.fruit_stale);

    // 跳过长休:本轮循环归零 —— 长休的意思就是"这一轮到此为止",走完它和跳过它
    // 都该开新的一轮,否则下一段专注走完会立刻又换成长休。
    focus_model_init(&m);
    m.cfg.rounds_per_long = 2;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(focus_model_input(&m, FOCUS_KEY_TIMER_LONG, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.phase == FOCUS_PHASE_FOCUS && m.cycle_done == 1);  // 跳过第一次短休
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.phase == FOCUS_PHASE_LONG_BREAK && m.run == FOCUS_RUN_DONE && m.cycle_done == 2);
    assert(focus_model_input(&m, FOCUS_KEY_TIMER_LONG, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.phase == FOCUS_PHASE_FOCUS && m.run == FOCUS_RUN_IDLE && m.view == FOCUS_VIEW_TIMER);
    assert(m.cycle_done == 0 && m.focus_total == 2);

    // 攒了几个点之后再放弃:整排点一起灭,不留"上一轮攒下的"。
    // 这一条正是用户报的那个现象:放弃之后回到首页、再进计时页,点阵里还亮着一个。
    // 注意放弃只能发生在专注里(休息上的长按是跳过,不清点阵),所以要攒够点再开
    // 下一段专注:点阵上的点是在"专注走完"那一刻加的,专注正在走的时候点已经亮了。
    focus_model_init(&m);
    for (int i = 0; i < 2; i++) {
        assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
        assert(run_to_deadline(&m, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
        assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
        assert(run_to_deadline(&m, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    }
    assert(m.cycle_done == 2 && m.focus_total == 2);
    assert(m.phase == FOCUS_PHASE_FOCUS && m.run == FOCUS_RUN_IDLE);
    // 第三个专注开起来,这时点阵上那两个点还亮着 —— 正是在这一刻放弃,才看得见
    // "残余有没有被留下"。
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.run == FOCUS_RUN_RUNNING && m.cycle_done == 2);
    assert(focus_model_input(&m, FOCUS_KEY_TIMER_LONG, 60 * 1000) ==
           (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cycle_done == 0 && m.focus_total == 2);
    assert(m.view == FOCUS_VIEW_HOME && m.run == FOCUS_RUN_IDLE && m.phase == FOCUS_PHASE_FOCUS);

    // ---- 长按 OK 在计时页上就是提示行里那件事 ----
    // 长按哪根键由提示行决定,而计时页的提示行把长按写在 OK 上,所以 OK 长按等价于
    // 上面那个手势,一个动作只有一个按法。
    assert(focus_model_input(&m, FOCUS_KEY_OK, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.view == FOCUS_VIEW_TIMER && m.run == FOCUS_RUN_RUNNING);
    assert(focus_model_input(&m, FOCUS_KEY_OK_LONG, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.view == FOCUS_VIEW_HOME && m.run == FOCUS_RUN_IDLE);

    // 首页与设置页上,长按 OK 就是提示行里写的那个动作,短按和长按落在同一个键上;
    // 深睡挂到长按上键,模型一个字段都不改(应用层据此进深睡)。
    assert(focus_model_input(&m, FOCUS_KEY_OK, t2) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    const focus_run_t run_before = m.run;
    m.view = FOCUS_VIEW_HOME;
    assert(focus_model_input(&m, FOCUS_KEY_OK_LONG, t2) == FOCUS_FLAG_REDRAW);
    assert(m.view == FOCUS_VIEW_SETTINGS && m.settings_row == FOCUS_SETTING_SHORT_BREAK);
    assert(m.run == run_before);
    assert(focus_model_input(&m, FOCUS_KEY_OK_LONG, t2) == FOCUS_FLAG_REDRAW);
    assert(m.view == FOCUS_VIEW_HOME);
    assert(focus_model_input(&m, FOCUS_KEY_UP_LONG, t2) == FOCUS_FLAG_SLEEP);
    assert(m.view == FOCUS_VIEW_HOME && m.run == run_before);
    m.view = FOCUS_VIEW_SETTINGS;
    assert(focus_model_input(&m, FOCUS_KEY_UP_LONG, t2) == FOCUS_FLAG_SLEEP);
    assert(m.view == FOCUS_VIEW_SETTINGS && m.run == run_before);
    m.view = FOCUS_VIEW_TIMER;

    // ---- 设置页 ----
    focus_model_init(&m);
    // 首页上长按 OK 进设置页,选中行停在第一行(短休息)。
    assert(focus_model_input(&m, FOCUS_KEY_OK_LONG, 0) == FOCUS_FLAG_REDRAW);
    assert(m.view == FOCUS_VIEW_SETTINGS && m.settings_row == FOCUS_SETTING_SHORT_BREAK);

    // 行的上下移动与环绕。
    assert(focus_model_input(&m, FOCUS_KEY_UP, 0) == FOCUS_FLAG_REDRAW);
    assert(m.settings_row == FOCUS_SETTING_COUNT - 1);
    assert(focus_model_input(&m, FOCUS_KEY_DOWN, 0) == FOCUS_FLAG_REDRAW);
    assert(m.settings_row == FOCUS_SETTING_SHORT_BREAK);

    // OK 修改:短休息 5 -> 10 -> 3,再绕回 5(选项表是 {3,5,10})。
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.short_break_min == 10);
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.short_break_min == 3);
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.short_break_min == 5);

    // 改的是"眼下停着的那个阶段"的时长时,剩余时间要跟着走 —— 否则调完短休回到
    // 界面还是老数字。这里把模型摆成"停在短休上"再改。
    m.phase = FOCUS_PHASE_SHORT_BREAK;
    m.run = FOCUS_RUN_IDLE;
    m.remaining_ms = 999999;  // 坏值:下面这次操作应该把它拉齐到新的阶段总时长
    m.settings_row = FOCUS_SETTING_SHORT_BREAK;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.short_break_min == 10);
    assert(focus_model_remaining_ms(&m, 0) == 10u * MIN_MS);

    // 提示音开关只有两态。
    m.settings_row = FOCUS_SETTING_CHIME;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(!m.cfg.chime_on);
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.chime_on);

    // 把每轮数调小,已完成的轮次要跟着夹回范围内,免得进度点画出界。
    m.settings_row = FOCUS_SETTING_ROUNDS;
    m.cycle_done = 6;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.rounds_per_long == 6 && m.cycle_done == 6);  // 4 -> 6,刚好不越界
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.rounds_per_long == 2);
    assert(m.cycle_done == 2);  // 已完成轮次夹回新的上限

    // 长按 OK 回首页 —— 设置页是从首页进来的,不会落到没见过的页面上。
    assert(focus_model_input(&m, FOCUS_KEY_OK_LONG, 0) == FOCUS_FLAG_REDRAW);
    assert(m.view == FOCUS_VIEW_HOME);

    // 首页上按 OK 开始(这时停在短休阶段上,开始的就是短休)。
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.view == FOCUS_VIEW_TIMER && m.run == FOCUS_RUN_RUNNING);

    // 跑起来以后长按上键不进设置页(免得把倒计时藏到看不见的地方);
    // 上下键也不改时长 —— 它们改的会是"当前这一轮",和首页上改的不是一件事。
    assert(focus_model_input(&m, FOCUS_KEY_UP_LONG, 0) == FOCUS_FLAG_NONE);
    assert(focus_model_input(&m, FOCUS_KEY_UP, 0) == FOCUS_FLAG_NONE);
    assert(focus_model_input(&m, FOCUS_KEY_DOWN, 0) == FOCUS_FLAG_NONE);
    assert(m.cfg.focus_min == 25 && m.cfg.short_break_min == 10);
    const int64_t running_deadline = m.deadline_ms;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == FOCUS_FLAG_NONE);  // 计时中没有暂停
    assert(m.run == FOCUS_RUN_RUNNING && m.deadline_ms == running_deadline);

    // ---- 计时页停在"未开始"上:长按上键进设置页,长按 OK 回首页 ----
    // 这个状态在一轮休息走完之后会出现(complete_break 把 run 放回 IDLE)。底栏那句
    // "单击开始 · 长按返回"写的就是它。
    focus_model_init(&m);
    m.view = FOCUS_VIEW_TIMER;
    assert(focus_model_input(&m, FOCUS_KEY_UP_LONG, 0) == FOCUS_FLAG_REDRAW);
    assert(m.view == FOCUS_VIEW_SETTINGS);
    // 进了设置页以后,长按上键变成深睡(提示行写的是长按 OK 回首页)。
    assert(focus_model_input(&m, FOCUS_KEY_UP_LONG, 0) == FOCUS_FLAG_SLEEP);
    assert(m.view == FOCUS_VIEW_SETTINGS);
    assert(focus_model_input(&m, FOCUS_KEY_OK_LONG, 0) == FOCUS_FLAG_REDRAW);  // 回首页
    assert(m.view == FOCUS_VIEW_HOME);

    m.view = FOCUS_VIEW_TIMER;
    assert(focus_model_input(&m, FOCUS_KEY_TIMER_LONG, 0) == FOCUS_FLAG_REDRAW);
    assert(m.view == FOCUS_VIEW_HOME && m.run == FOCUS_RUN_IDLE);

    // 休息刚走完(run == DONE 且停在专注阶段)时长按也是回首页:这一轮循环到头了,
    // 没有下一段要走,所以这里和"没开始"看着一样 —— 但底下不一样:模型里 run 还是
    // DONE,得先落回未开始再回首页,否则首页那颗 OK 会认不出该从哪一段开始。
    m.view = FOCUS_VIEW_TIMER;
    m.run = FOCUS_RUN_DONE;
    m.phase = FOCUS_PHASE_FOCUS;
    assert(focus_model_input(&m, FOCUS_KEY_TIMER_LONG, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.view == FOCUS_VIEW_HOME && m.run == FOCUS_RUN_IDLE);
    assert(m.phase == FOCUS_PHASE_FOCUS);

    // ---- 恢复默认:设置页最后一行,全页唯一一个动作行 ----
    // 它必须排在最后。上下是环绕的,如果把它摆在中间,从第一行往上一步就落到它
    // 上面,而这一行按一下会一次改掉四项。
    assert((focus_setting_t)(FOCUS_SETTING_COUNT - 1) == FOCUS_SETTING_RESET);

    focus_model_init(&m);
    assert(focus_model_input(&m, FOCUS_KEY_OK_LONG, 0) == FOCUS_FLAG_REDRAW);  // 首页进设置页
    assert(m.view == FOCUS_VIEW_SETTINGS);
    m.settings_row = FOCUS_SETTING_RESET;
    assert(focus_model_input(&m, FOCUS_KEY_DOWN, 0) == FOCUS_FLAG_REDRAW);
    assert(m.settings_row == FOCUS_SETTING_SHORT_BREAK);  // 从它往下就是第一行

    // 逐项改乱之后按一下,五项一起回到出厂值 —— 包括首页那个专注时长。
    m.cfg.focus_min = 45;
    m.cfg.short_break_min = 3;
    m.cfg.long_break_min = 30;
    m.cfg.rounds_per_long = 2;
    m.cfg.chime_on = false;
    m.settings_row = FOCUS_SETTING_RESET;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.focus_min == FOCUS_DEFAULT_FOCUS_MIN);
    assert(m.cfg.short_break_min == FOCUS_DEFAULT_SHORT_BREAK_MIN);
    assert(m.cfg.long_break_min == FOCUS_DEFAULT_LONG_BREAK_MIN);
    assert(m.cfg.rounds_per_long == FOCUS_DEFAULT_ROUNDS_PER_LONG);
    assert(m.cfg.chime_on);
    assert(m.settings_row == FOCUS_SETTING_RESET);  // 光标留在原地,不跳走

    // "几个一循环"回到 4,已完成的轮次比它多就要一起夹回来,否则进度点会点满一排。
    m.cfg.rounds_per_long = 6;
    m.cycle_done = 6;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.rounds_per_long == FOCUS_DEFAULT_ROUNDS_PER_LONG);
    assert(m.cycle_done == FOCUS_DEFAULT_ROUNDS_PER_LONG);

    // 停在"未开始的专注"上按它,剩余时间跟着恢复后的时长走,而不是留着上一次的
    // 数字 —— 和上面单独改某一项时是同一件事。
    m.phase = FOCUS_PHASE_FOCUS;
    m.run = FOCUS_RUN_IDLE;
    m.cfg.focus_min = 45;
    m.remaining_ms = 999999;  // 坏值:这次操作应该把它拉齐
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.focus_min == FOCUS_DEFAULT_FOCUS_MIN);
    assert(focus_model_remaining_ms(&m, 0) == FOCUS_DEFAULT_FOCUS_MIN * MIN_MS);

    // 停在休息上同理:恢复后的短休是 5 分钟,剩余时间要按 5 分钟重算。
    m.phase = FOCUS_PHASE_SHORT_BREAK;
    m.run = FOCUS_RUN_IDLE;
    m.remaining_ms = 999999;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.cfg.short_break_min == FOCUS_DEFAULT_SHORT_BREAK_MIN);
    assert(focus_model_remaining_ms(&m, 0) == FOCUS_DEFAULT_SHORT_BREAK_MIN * MIN_MS);

    // ---- 掉电快照:导出 / 导入 ----
    focus_model_init(&m);
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(focus_model_tick(&m, MIN_MS) == FOCUS_FLAG_NONE);  // 刚走 1 分钟

    focus_persist_t snap;
    focus_model_export(&m, MIN_MS, &snap);
    assert(snap.magic == FOCUS_PERSIST_MAGIC && snap.version == FOCUS_PERSIST_VERSION);
    assert(snap.run == (uint8_t)FOCUS_RUN_RUNNING);
    assert(snap.phase == (uint8_t)FOCUS_PHASE_FOCUS);
    assert(snap.remaining_sec == 24u * 60u);
    assert(snap.focus_min == 25 && snap.short_break_min == 5 && snap.long_break_min == 15);
    assert(snap.rounds_per_long == 4 && snap.chime_on == 1);

    // 导入时 RUNNING **作废**:绝对截止时刻跨不过一次重启,而"被打断的一轮不算数"
    // 正是番茄钟本身那条规则,所以这里不假装还能接着走 —— 回首页、从头开始。留下的
    // 只有配置和已经数进去的完成数,那 24 分钟连同这一轮一起没了。
    focus_model_t restored;
    assert(focus_model_import(&restored, &snap));
    assert(restored.run == FOCUS_RUN_IDLE && restored.phase == FOCUS_PHASE_FOCUS);
    assert(restored.view == FOCUS_VIEW_HOME);
    assert(focus_model_remaining_ms(&restored, 0) == 25u * MIN_MS);  // 回到整整一个番茄
    assert(restored.cfg.focus_min == 25 && restored.cfg.chime_on);
    // 作废也会置位换水果信号,而这正是应用层在开机日志里认出"刚才那一轮被丢掉了"的
    // 唯一依据(见 main/focus_app.c 的启动日志):界面上看到的都是干净的 25:00,
    // 分不出是没接过还是接过又丢了。这条断言把那行日志钉在模型的一个事实上。
    assert(restored.fruit_stale);

    // 重启作废的只是"当时正在走"的那一轮,不牵连这一轮循环里**已经走完**的番茄:
    // 存档里带着 cycle_done == 2 时,导入之后那两个点还在。这和放弃的"清零"是
    // 有意区分开的两件事 —— 放弃是用户说"这一轮不干了",重启不是(见 timer_long_press)。
    focus_persist_t running_cycle = snap;
    running_cycle.cycle_done = 2;
    assert(focus_model_import(&restored, &running_cycle));
    assert(restored.run == FOCUS_RUN_IDLE && restored.cycle_done == 2);
    assert(restored.view == FOCUS_VIEW_HOME);

    // 存档里是"未开始"时落回首页:那里才能改时长、按 OK 才开始。
    focus_model_init(&m);
    m.view = FOCUS_VIEW_TIMER;  // 人为挪开,验证 import 会把它拨回首页
    focus_persist_t idle_snap;
    focus_model_export(&m, 0, &idle_snap);
    assert(focus_model_import(&m, &idle_snap));
    assert(m.view == FOCUS_VIEW_HOME && m.run == FOCUS_RUN_IDLE);
    assert(focus_model_remaining_ms(&m, 0) == 25u * MIN_MS);

    // 老固件写下的 run == 2(那个值曾经是"暂停",现在空着)读出来和 RUNNING 一样处理:
    // 这一轮作废,配置留着。这条断言盯的是枚举里那个洞 —— 谁要是把 DONE 挪回 2,
    // 一台装着老存档的设备就会把"被按停过的番茄"读成"这一段已经走完",凭空少休息一次。
    focus_persist_t legacy_snap = snap;
    legacy_snap.run = 2;
    focus_model_t legacy_restored;
    assert(focus_model_import(&legacy_restored, &legacy_snap));
    assert(FOCUS_RUN_DONE == 3);  // 洞必须留在 2 号位上
    assert(legacy_restored.run == FOCUS_RUN_IDLE && legacy_restored.view == FOCUS_VIEW_HOME);
    assert(legacy_restored.cfg.focus_min == 25 && legacy_restored.cfg.chime_on);

    // 一整个循环走完后落盘,恢复出来还接得上。
    focus_model_init(&m);
    m.cfg.rounds_per_long = 2;
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(run_to_deadline(&m, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    focus_persist_t mid;
    focus_model_export(&m, 0, &mid);
    assert(mid.phase == (uint8_t)FOCUS_PHASE_SHORT_BREAK);
    assert(mid.run == (uint8_t)FOCUS_RUN_DONE);
    assert(mid.cycle_done == 1 && mid.focus_total == 1);
    assert(focus_model_import(&restored, &mid));
    assert(restored.phase == FOCUS_PHASE_SHORT_BREAK && restored.run == FOCUS_RUN_DONE);
    assert(restored.cycle_done == 1 && restored.focus_total == 1);
    assert(focus_model_remaining_ms(&restored, 0) == 5u * MIN_MS);

    // 剩余秒数超过阶段总时长时夹回总时长(坏数据不能让进度环算错)。
    focus_persist_t over = mid;
    over.remaining_sec = 99999;
    assert(focus_model_import(&restored, &over));
    assert(focus_model_remaining_ms(&restored, 0) == 5u * MIN_MS);

    // 坏快照一律拒绝,并且不动已有状态。
    focus_model_init(&m);
    m.cfg.focus_min = 45;
    focus_persist_t bad = snap;
    bad.magic = 0;
    assert(!focus_model_import(&m, &bad));
    bad = snap;
    bad.version = (uint16_t)(FOCUS_PERSIST_VERSION + 1);
    assert(!focus_model_import(&m, &bad));
    bad = snap;
    bad.focus_min = 0;
    assert(!focus_model_import(&m, &bad));
    bad = snap;
    bad.short_break_min = 31;
    assert(!focus_model_import(&m, &bad));
    bad = snap;
    bad.rounds_per_long = FOCUS_MAX_ROUNDS + 1;
    assert(!focus_model_import(&m, &bad));
    bad = snap;
    bad.cycle_done = bad.rounds_per_long + 1;
    assert(!focus_model_import(&m, &bad));
    bad = snap;
    bad.phase = 3;
    assert(!focus_model_import(&m, &bad));
    // 上面那些坏字段否掉的是**整份存档**;run 不一样 —— 认不出的运行态只作废那一轮,
    // 用户设的时长要留下(见 config_in_range 的注释)。代价是这一条不再断言 false。
    bad = snap;
    bad.run = 4;
    assert(focus_model_import(&m, &bad));
    assert(m.cfg.focus_min == 25);  // 存档里的时长被收下了,没被那一轮连坐
    assert(m.run == FOCUS_RUN_IDLE && m.view == FOCUS_VIEW_HOME);

    // 被拒绝的导入没有碰过 m(拿一份坏存档再验一次,上面那几条跑完之后 m 已经变了)。
    focus_model_init(&m);
    m.cfg.focus_min = 45;
    bad = snap;
    bad.phase = 3;
    assert(!focus_model_import(&m, &bad));
    assert(m.cfg.focus_min == 45);
    assert(m.run == FOCUS_RUN_IDLE && m.view == FOCUS_VIEW_HOME);

    // ---- 水果:换水果的信号只在"下一段专注出现之前"置位 ----
    //
    // 这条不变量是用户直接提出来的:待开始那一屏(刚休息完)上已经画着一只水果,
    // 按下开始如果重抽,那只完好的果子会当着人的面变成另一只。所以抽签必须落在
    // "还没画出来"的那一刻 —— 休息走完或跳过(complete_break)与放弃本轮(reset_to_idle)。
    // 跳过休息走的也是 complete_break,所以它同样在"结束卡刚过去、水果还没露脸"时置位。
    focus_model_init(&m);
    assert(m.fruit == 0 && !m.fruit_stale);

    // 按下开始:水果不变,也不置位。
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.run == FOCUS_RUN_RUNNING && !m.fruit_stale && m.fruit == 0);

    focus_model_set_fruit(&m, 3);  // 应用层在"休息走完"那一刻写进来的
    assert(m.fruit == 3 && !m.fruit_stale);

    // 计时中乱按:水果不变,也不置位。这一条替代了原来的"暂停再继续"—— 中间那个
    // 可以停下来的状态没有了,但"计时中间按不出换水果"这条不变量仍然要盯着。
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == FOCUS_FLAG_NONE);
    assert(focus_model_input(&m, FOCUS_KEY_UP, 0) == FOCUS_FLAG_NONE);
    assert(focus_model_input(&m, FOCUS_KEY_DOWN, 0) == FOCUS_FLAG_NONE);
    assert(m.run == FOCUS_RUN_RUNNING && !m.fruit_stale && m.fruit == 3);

    // 专注走完停在 DONE:不置位 —— 这一屏画的是结束卡,水果根本不在画面上,
    // 现在抽等于白抽(下一屏是睡觉三帧,也看不到水果)。
    assert(run_to_deadline(&m, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.run == FOCUS_RUN_DONE && m.phase == FOCUS_PHASE_SHORT_BREAK);
    assert(!m.fruit_stale && m.fruit == 3);

    // 开始这次休息:还是不置位。
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.run == FOCUS_RUN_RUNNING && !m.fruit_stale && m.fruit == 3);

    // 休息走完:这一屏要重新画水果了 —— 换水果的信号就在这里置位。此时水果本身
    // 还是旧的那一只(3),应用层抽完才写进来,而这一瞬间还没人看见果子的形状。
    // (长按跳过这次休息走的就是这一步,信号同样在这里置位 —— 上面那一组已经断言过。)
    assert(run_to_deadline(&m, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE | FOCUS_FLAG_CHIME));
    assert(m.run == FOCUS_RUN_IDLE && m.phase == FOCUS_PHASE_FOCUS);
    assert(m.fruit_stale && m.fruit == 3);

    focus_model_set_fruit(&m, 2);  // 应用层写完就清掉信号
    assert(m.fruit == 2 && !m.fruit_stale);

    // 按下开始:水果一格不动 —— 用户看见的那只,就是要被吃掉的那只。
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) == (FOCUS_FLAG_REDRAW | FOCUS_FLAG_SAVE));
    assert(m.run == FOCUS_RUN_RUNNING && m.fruit == 2 && !m.fruit_stale);

    // 序号越界夹到 0 号,不会把越界值写进模型(进而写进存档)。
    focus_model_set_fruit(&m, 250);
    assert(m.fruit == 0);
    focus_model_set_fruit(&m, (uint8_t)FOCUS_FRUIT_COUNT);
    assert(m.fruit == 0);

    // 放弃本轮:落点是首页(不画水果),所以顺手换一只也不可见 —— 信号置位,
    // 水果本身留着旧的,等应用层抽完写进来。
    focus_model_init(&m);
    focus_model_set_fruit(&m, 4);
    assert(focus_model_input(&m, FOCUS_KEY_OK, 0) & FOCUS_FLAG_SAVE);
    focus_model_reset_to_idle(&m);
    assert(m.run == FOCUS_RUN_IDLE && m.fruit == 4 && m.fruit_stale);

    // ---- 水果:存档带着走,老存档读出来是 0 号 ----
    focus_model_init(&m);
    focus_model_set_fruit(&m, 2);
    focus_persist_t fruit_snap;
    focus_model_export(&m, 0, &fruit_snap);
    assert(fruit_snap.fruit == 2);
    assert(fruit_snap.remaining_sec == 25u * 60u);

    focus_model_t fruit_restored;
    assert(focus_model_import(&fruit_restored, &fruit_snap));
    assert(fruit_restored.fruit == 2 && !fruit_restored.fruit_stale);

    // 旧固件写的存档:那一个字节是填充,读出来是 0(香蕉),整份设置仍然有效。
    focus_persist_t legacy = fruit_snap;
    legacy.fruit = 0;
    assert(focus_model_import(&fruit_restored, &legacy));
    assert(fruit_restored.fruit == 0 && fruit_restored.cfg.focus_min == 25);

    // 水果序号坏掉只当 0 号处理,不据此否掉整份存档 —— 为它丢掉用户的时长设置不值得。
    focus_persist_t bad_fruit = fruit_snap;
    bad_fruit.fruit = 200;
    assert(focus_model_import(&fruit_restored, &bad_fruit));
    assert(fruit_restored.fruit == 0 && fruit_restored.cfg.focus_min == 25);

    // 存档布局:fruit 占的必须是原来那个填充字节,结构体大小也不能变。
    // 这里再断言一次(编译期还有 _Static_assert),因为这是"老存档不失效"的全部依据。
    assert(sizeof(focus_persist_t) == 24);
    assert(offsetof(focus_persist_t, fruit) == 14);
    assert(offsetof(focus_persist_t, remaining_sec) == 16);

    // ---- 阶段名与空指针 ----
    assert(focus_phase_name(FOCUS_PHASE_FOCUS)[0] != '\0');
    assert(focus_phase_name(FOCUS_PHASE_SHORT_BREAK)[0] != '\0');
    assert(focus_phase_name(FOCUS_PHASE_LONG_BREAK)[0] != '\0');

    assert(focus_model_total_ms(NULL) == 0);
    assert(focus_model_remaining_ms(NULL, 0) == 0);
    assert(focus_model_remaining_sec(NULL, 0) == 0);
    assert(focus_model_elapsed_permille(NULL, 0) == 0);
    assert(focus_model_tick(NULL, 0) == FOCUS_FLAG_NONE);
    assert(focus_model_input(NULL, FOCUS_KEY_OK, 0) == FOCUS_FLAG_NONE);
    assert(focus_model_import(NULL, &snap) == false);
    assert(!focus_model_import(&m, NULL));
    focus_model_export(NULL, 0, &snap);
    focus_model_init(NULL);
    focus_model_reset_to_idle(NULL);

    puts("focus model tests: PASS");
    return 0;
}
