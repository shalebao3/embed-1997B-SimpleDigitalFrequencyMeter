#include "interval_meter.h"

#include "measurement_hw.h"

/* TIM3 为 16 位计数器，一整圈包含 65536 个 timer tick。
 * 完整时间戳 = 累计溢出圈数 × 65536 + 当前 CCR1。
 */
#define TIM3_COUNTER_RANGE_TICKS 65536ULL

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

/* 原始捕获结果有效标志。
 * 状态值：
 * 0U：尚未从 measurement_hw 成功消费到一组完整的两个 CCR1 捕获值。
 * 1U：最近一组 CCR1、溢出圈数和完整时间戳均已更新，可以读取。
 */
static uint8_t interval_capture_valid = 0U;

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
    interval_capture_valid = 0U;

    return MeasurementHw_PeriodCaptureStart();
}

/**
 * @brief 轮询并消费最新一组 TIM3 DMA 原始捕获值。
 *
 * 当前阶段会把每个 CCR1 与对应的 TIM3 溢出圈数组合成完整时间戳，
 * 再计算两个上升沿之间经过的 delta_ticks；暂不换算成周期或频率。
 */
void IntervalMeter_Task(void)
{
    uint16_t first;
    uint16_t second;
    uint32_t first_overflow_count;
    uint32_t second_overflow_count;

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
        interval_capture_valid = 1U;
    }
    else
    {
        interval_delta_ticks = 0ULL;
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
