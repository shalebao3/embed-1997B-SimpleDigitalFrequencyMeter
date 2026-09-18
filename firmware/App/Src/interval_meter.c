#include "interval_meter.h"

#include "measurement_hw.h"
#include "main.h"

#define INTERVAL_RESULT_TIMEOUT_MS 3000U

/* TIM3 为 16 位计数器，一整圈包含 65536 个 timer tick。
 * 完整时间戳 = 累计溢出圈数 × 65536 + 当前 CCR1。
 */
#define TIM3_COUNTER_RANGE_TICKS 65536ULL

/* TIM3 当前计数频率为 72MHz（PSC=0）。
 * 1 tick = 1 / 72MHz s = 125 / 9 ns。
 * 使用约分后的整数比例避免在 STM32F103 上引入不必要的浮点运算。
 */
#define TIM3_TICK_NS_NUMERATOR 125ULL
#define TIM3_TICK_NS_DENOMINATOR 9ULL
#define TIM3_COUNTER_HZ 72000000ULL
#define MILLIHZ_PER_HZ 1000ULL

/* 最近一次从 measurement_hw 消费到的第一个 CCR1 原始捕获值。
 * 数值范围：0U~65535U，表示第一次上升沿到来时 TIM3 CNT 被硬件锁存到 CCR1 的原始 16 位计数值。
 * 注意：0U 既可能是初始化默认值，也可能是真实捕获值；是否有效必须结合 interval_capture_valid 判断。
 */
static uint16_t interval_capture_first = 0U;

/* 最近一次从 measurement_hw 消费到的第二个 CCR1 原始捕获值。
 * 数值范围：0U~65535U，表示第二次上升沿到来时 TIM3 CNT 被硬件锁存到 CCR1 的原始 16 位计数值。
 * 注意：0U 既可能是初始化默认值，也可能是真实捕获值；是否有效必须结合 interval_capture_valid 判断。
 */
static uint16_t interval_capture_second = 0U;

/* 最近一次完整原始捕获结果中，第一个 CCR1 捕获时对应的 TIM3 累计溢出圈数。
 * 数值含义：
 * 0U：捕获发生前 TIM3 尚未回绕，或模块尚未得到有效结果。
 * n：第一次捕获发生时 TIM3 已累计回绕 n 次。
 */
static uint32_t interval_first_overflow_count = 0U;

/* 最近一次完整原始捕获结果中，第二个 CCR1 捕获时对应的 TIM3 累计溢出圈数。
 * 数值含义：
 * 0U：捕获发生前 TIM3 尚未回绕，或模块尚未得到有效结果。
 * n：第二次捕获发生时 TIM3 已累计回绕 n 次。
 */
static uint32_t interval_second_overflow_count = 0U;

/* 第一次上升沿对应的软件扩展完整时间戳，单位为 TIM3 timer tick。
 * 计算方式：first_overflow_count × 65536 + first CCR1。
 * 该时间戳以本轮 TIM3 输入捕获启动并清零 CNT 的时刻为相对起点。
 */
static uint64_t interval_first_timestamp_ticks = 0ULL;

/* 第二次上升沿对应的软件扩展完整时间戳，单位为 TIM3 timer tick。
 * 计算方式：second_overflow_count × 65536 + second CCR1。
 * 该时间戳与 interval_first_timestamp_ticks 使用同一个相对起点。
 */
static uint64_t interval_second_timestamp_ticks = 0ULL;

/* 最近一组两个上升沿完整时间戳之间的差值，单位为 TIM3 timer tick。
 * 计算方式：delta_ticks = second_timestamp - first_timestamp。
 * 0ULL 表示尚未得到有效时间间隔，或当前结果无效。
 */
static uint64_t interval_delta_ticks = 0ULL;

/* 最近一组两个上升沿之间的周期，单位为 ns。
 * 由 interval_delta_ticks 按 TIM3 72MHz 计数时基换算得到。
 * 0ULL 表示尚未得到有效周期，或当前结果无效。
 */
static uint64_t interval_period_ns = 0ULL;

/* 最近一组两个上升沿之间对应的频率，单位为 mHz（毫赫兹）。
 * 直接由 interval_delta_ticks 和 TIM3 72MHz 计数时基换算得到，
 * 避免先把周期取整为 ns 后再求倒数造成额外舍入误差。
 * 0ULL 表示尚未得到有效频率，或当前结果无效。
 */
static uint64_t interval_frequency_millihz = 0ULL;

/* 原始捕获结果有效标志。
 * 状态值：
 * 0U：尚未从 measurement_hw 成功消费到一组完整的两个 CCR1 捕获值。
 * 1U：最近一组 CCR1、溢出圈数和完整时间戳均已更新，可以读取。
 */
static uint8_t interval_capture_valid = 0U;

/* 最近一次完成有效周期测量的系统毫秒时刻。
 * 1Hz 输入下 DMA 长度为 2，一组结果约每 2 秒更新一次，因此超时取 3 秒。
 */
static uint32_t interval_last_result_tick_ms = 0U;

/**
 * @brief 将 TIM3 的软件溢出圈数和 16 位 CCR1 组合成完整时间戳。
 *
 * TIM3 每回绕一次代表额外经过 65536 个 timer tick，因此：
 * timestamp = overflow_count × 65536 + capture_value。
 *
 * @param overflow_count 捕获时刻累计发生的 TIM3 CNT 回绕次数。
 * @param capture_value 捕获时刻硬件锁存到 CCR1 的 16 位 CNT 值。
 * @return 以 TIM3 timer tick 为单位的软件扩展完整时间戳。
 */
static uint64_t IntervalMeter_BuildTimestamp(
    uint32_t overflow_count,
    uint16_t capture_value)
{
    return ((uint64_t)overflow_count * TIM3_COUNTER_RANGE_TICKS) +
           (uint64_t)capture_value;
}

/**
 * @brief 将 TIM3 timer tick 数换算为纳秒周期。
 *
 * 当前 TIM3 CNT 时钟为 72MHz，因此 1 tick = 125/9 ns。
 * 这里在除法前加上分母的一半进行四舍五入，得到最接近的整数纳秒值。
 *
 * @param ticks 两个上升沿之间经过的 TIM3 timer tick 数。
 * @return 对应的周期，单位为 ns。
 */
static uint64_t IntervalMeter_TicksToNanoseconds(uint64_t ticks)
{
    return ((ticks * TIM3_TICK_NS_NUMERATOR) +
            (TIM3_TICK_NS_DENOMINATOR / 2ULL)) /
           TIM3_TICK_NS_DENOMINATOR;
}

/**
 * @brief 根据 TIM3 timer tick 数计算输入信号频率。
 *
 * 当前 TIM3 CNT 时钟为 72MHz，因此：
 * frequency_hz = 72000000 / ticks。
 * 为保留小数精度，结果以 mHz（毫赫兹）表示，并在整数除法前加 ticks/2 四舍五入。
 *
 * @param ticks 两个相邻上升沿之间经过的 TIM3 timer tick 数。
 * @return 对应频率，单位为 mHz；ticks 为 0 时返回 0。
 */
static uint64_t IntervalMeter_TicksToMilliHertz(uint64_t ticks)
{
    if (ticks == 0ULL)
    {
        return 0ULL;
    }

    return ((TIM3_COUNTER_HZ * MILLIHZ_PER_HZ) + (ticks / 2ULL)) /
           ticks;
}

/**
 * @brief 初始化时间间隔测量模块并启动 TIM3_CH1 DMA 输入捕获。
 * @return 初始化成功返回 1，否则返回 0。
 */
uint8_t IntervalMeter_Init(void)
{
    interval_capture_first = 0U;
    interval_capture_second = 0U;
    interval_first_overflow_count = 0U;
    interval_second_overflow_count = 0U;
    interval_first_timestamp_ticks = 0ULL;
    interval_second_timestamp_ticks = 0ULL;
    interval_delta_ticks = 0ULL;
    interval_period_ns = 0ULL;
    interval_frequency_millihz = 0ULL;
    interval_capture_valid = 0U;
    interval_last_result_tick_ms = HAL_GetTick();

    return MeasurementHw_PeriodCaptureStart();
}

/**
 * @brief 轮询并消费最新一组 TIM3 DMA 原始捕获值。
 *
 * 当前阶段会把每个 CCR1 与对应的 TIM3 溢出圈数组合成完整时间戳，
 * 再计算两个上升沿之间经过的 delta_ticks，并换算得到周期 ns 和频率 mHz。
 */
void IntervalMeter_Task(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint16_t first;
    uint16_t second;
    uint32_t first_overflow_count;
    uint32_t second_overflow_count;

    if ((interval_capture_valid != 0U) &&
        ((uint32_t)(now_ms - interval_last_result_tick_ms) >
         INTERVAL_RESULT_TIMEOUT_MS))
    {
        interval_delta_ticks = 0ULL;
        interval_period_ns = 0ULL;
        interval_frequency_millihz = 0ULL;
        interval_capture_valid = 0U;
    }

    if (MeasurementHw_PeriodCaptureTakePair(
            &first,
            &first_overflow_count,
            &second,
            &second_overflow_count) == 0U)
    {
        return;
    }

    interval_capture_first = first;
    interval_capture_second = second;
    interval_first_overflow_count = first_overflow_count;
    interval_second_overflow_count = second_overflow_count;

    interval_first_timestamp_ticks =
        IntervalMeter_BuildTimestamp(first_overflow_count, first);
    interval_second_timestamp_ticks =
        IntervalMeter_BuildTimestamp(second_overflow_count, second);

    /* 第二个上升沿应当晚于第一个上升沿。
     * 若时间戳顺序异常，则本组结果不作为有效时间间隔继续向上层提供。
     */
    if (interval_second_timestamp_ticks > interval_first_timestamp_ticks)
    {
        interval_delta_ticks =
            interval_second_timestamp_ticks - interval_first_timestamp_ticks;
        interval_period_ns =
            IntervalMeter_TicksToNanoseconds(interval_delta_ticks);
        interval_frequency_millihz =
            IntervalMeter_TicksToMilliHertz(interval_delta_ticks);
        interval_last_result_tick_ms = now_ms;
        interval_capture_valid = 1U;
    }
    else
    {
        interval_delta_ticks = 0ULL;
        interval_period_ns = 0ULL;
        interval_frequency_millihz = 0ULL;
        interval_capture_valid = 0U;
    }
}

/**
 * @brief 判断是否至少已经收到一组完整的原始捕获值。
 * @return 已收到返回 1，否则返回 0。
 */
uint8_t IntervalMeter_IsCaptureValid(void)
{
    return interval_capture_valid;
}

/**
 * @brief 获取最近一次保存的第一个 CCR1 原始捕获值。
 */
uint16_t IntervalMeter_GetFirstCapture(void)
{
    return interval_capture_first;
}

/**
 * @brief 获取最近一次保存的第二个 CCR1 原始捕获值。
 */
uint16_t IntervalMeter_GetSecondCapture(void)
{
    return interval_capture_second;
}

/**
 * @brief 获取最近一次保存的第一个 CCR1 捕获时对应的 TIM3 累计溢出圈数。
 */
uint32_t IntervalMeter_GetFirstOverflowCount(void)
{
    return interval_first_overflow_count;
}

/**
 * @brief 获取最近一次保存的第二个 CCR1 捕获时对应的 TIM3 累计溢出圈数。
 */
uint32_t IntervalMeter_GetSecondOverflowCount(void)
{
    return interval_second_overflow_count;
}

/**
 * @brief 获取最近一次保存的第一个上升沿完整时间戳。
 * @return 相对于本轮 TIM3 输入捕获启动时刻的 timer tick 总数。
 */
uint64_t IntervalMeter_GetFirstTimestampTicks(void)
{
    return interval_first_timestamp_ticks;
}

/**
 * @brief 获取最近一次保存的第二个上升沿完整时间戳。
 * @return 相对于本轮 TIM3 输入捕获启动时刻的 timer tick 总数。
 */
uint64_t IntervalMeter_GetSecondTimestampTicks(void)
{
    return interval_second_timestamp_ticks;
}

/**
 * @brief 获取最近一组两个上升沿之间经过的 TIM3 timer tick 数。
 * @return delta_ticks = second_timestamp - first_timestamp；
 *         尚无有效结果时返回 0。
 */
uint64_t IntervalMeter_GetDeltaTicks(void)
{
    return interval_delta_ticks;
}

/**
 * @brief 获取最近一组两个上升沿之间的周期。
 * @return 周期，单位为 ns；尚无有效结果时返回 0。
 */
uint64_t IntervalMeter_GetPeriodNs(void)
{
    return interval_period_ns;
}

/**
 * @brief 获取最近一组两个上升沿对应的周期法频率。
 * @return 频率，单位为 mHz；尚无有效结果时返回 0。
 */
uint64_t IntervalMeter_GetFrequencyMilliHz(void)
{
    return interval_frequency_millihz;
}
