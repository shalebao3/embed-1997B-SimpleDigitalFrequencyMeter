#include "pulse_width_meter.h"

#include "measurement_hw.h"
#include "main.h"

#define PULSE_WIDTH_RESULT_TIMEOUT_MS 3000U

/* TIM3 为 16 位计数器；每次回绕代表额外经过 65536 个 timer tick。 */
#define TIM3_COUNTER_RANGE_TICKS 65536ULL

/* TIM3 当前 CNT 时钟为 72MHz（PSC=0），1 tick = 125/9 ns。 */
#define TIM3_TICK_NS_NUMERATOR 125ULL
#define TIM3_TICK_NS_DENOMINATOR 9ULL

/* 最近一次有效高电平脉宽，分别保存为 TIM3 tick 和 ns。 */
static uint64_t pulse_width_ticks = 0ULL;
static uint64_t pulse_width_ns = 0ULL;

/* 脉宽结果有效标志。
 * 0U：尚无有效“上升沿 → 下降沿”时间差。
 * 1U：pulse_width_ticks / pulse_width_ns 保存最近一次有效结果。
 */
static uint8_t pulse_width_valid = 0U;
static uint8_t pulse_width_enabled = 0U;
static uint32_t pulse_width_last_result_tick_ms = 0U;

/**
 * @brief 将 TIM3 溢出圈数和 16 位 CCR 组合成完整时间戳。
 */
static uint64_t PulseWidthMeter_BuildTimestamp(
    uint32_t overflow_count,
    uint16_t capture_value)
{
    return ((uint64_t)overflow_count * TIM3_COUNTER_RANGE_TICKS) +
           (uint64_t)capture_value;
}

/**
 * @brief 将 TIM3 timer tick 数换算为纳秒。
 */
static uint64_t PulseWidthMeter_TicksToNanoseconds(uint64_t ticks)
{
    return ((ticks * TIM3_TICK_NS_NUMERATOR) +
            (TIM3_TICK_NS_DENOMINATOR / 2ULL)) /
           TIM3_TICK_NS_DENOMINATOR;
}

/**
 * @brief 初始化高电平脉冲宽度测量模块。
 */
uint8_t PulseWidthMeter_Init(void)
{
    pulse_width_ticks = 0ULL;
    pulse_width_ns = 0ULL;
    pulse_width_valid = 0U;
    pulse_width_enabled = 0U;
    pulse_width_last_result_tick_ms = HAL_GetTick();

    if (MeasurementHw_PulseWidthCaptureStart() == 0U)
    {
        return 0U;
    }

    MeasurementHw_PulseWidthCaptureSetEnabled(0U);
    return 1U;
}

/**
 * @brief 启用或暂停 CH2 下降沿中断。
 *
 * 只在脉宽显示模式下启用，避免高频输入时产生不必要的大量 CC2 中断。
 */
void PulseWidthMeter_SetEnabled(uint8_t enabled)
{
    pulse_width_enabled = (enabled != 0U) ? 1U : 0U;
    pulse_width_valid = 0U;
    pulse_width_ticks = 0ULL;
    pulse_width_ns = 0ULL;
    pulse_width_last_result_tick_ms = HAL_GetTick();

    MeasurementHw_PulseWidthCaptureSetEnabled(pulse_width_enabled);
}

/**
 * @brief 轮询并消费最新一组“上升沿 → 下降沿”捕获结果。
 */
void PulseWidthMeter_Task(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint16_t rise_capture;
    uint16_t fall_capture;
    uint32_t rise_overflow_count;
    uint32_t fall_overflow_count;
    uint64_t rise_timestamp;
    uint64_t fall_timestamp;

    if (pulse_width_enabled == 0U)
    {
        return;
    }

    if ((pulse_width_valid != 0U) &&
        ((uint32_t)(now_ms - pulse_width_last_result_tick_ms) >
         PULSE_WIDTH_RESULT_TIMEOUT_MS))
    {
        pulse_width_ticks = 0ULL;
        pulse_width_ns = 0ULL;
        pulse_width_valid = 0U;
    }

    if (MeasurementHw_PulseWidthCaptureTakePair(
            &rise_capture,
            &rise_overflow_count,
            &fall_capture,
            &fall_overflow_count) == 0U)
    {
        return;
    }

    rise_timestamp =
        PulseWidthMeter_BuildTimestamp(rise_overflow_count, rise_capture);
    fall_timestamp =
        PulseWidthMeter_BuildTimestamp(fall_overflow_count, fall_capture);

    /* 正常高电平脉冲中，下降沿必须晚于对应的最近一次上升沿。 */
    if (fall_timestamp > rise_timestamp)
    {
        pulse_width_ticks = fall_timestamp - rise_timestamp;
        pulse_width_ns =
            PulseWidthMeter_TicksToNanoseconds(pulse_width_ticks);
        pulse_width_last_result_tick_ms = now_ms;
        pulse_width_valid = 1U;
    }
    else
    {
        pulse_width_ticks = 0ULL;
        pulse_width_ns = 0ULL;
        pulse_width_valid = 0U;
    }
}

uint8_t PulseWidthMeter_IsValid(void)
{
    return pulse_width_valid;
}

uint64_t PulseWidthMeter_GetWidthTicks(void)
{
    return pulse_width_ticks;
}

uint64_t PulseWidthMeter_GetWidthNs(void)
{
    return pulse_width_ns;
}
