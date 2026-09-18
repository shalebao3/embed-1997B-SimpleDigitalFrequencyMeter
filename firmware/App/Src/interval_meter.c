#include "interval_meter.h"

#include "measurement_hw.h"

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

/* 原始捕获结果有效标志。
 * 状态值：
 * 0U：尚未从 measurement_hw 成功消费到一组完整的两个 CCR1 捕获值。
 * 1U：interval_capture_first / interval_capture_second 中保存着最近一组可用的原始捕获值。
 */
static uint8_t interval_capture_valid = 0U;

/**
 * @brief 初始化时间间隔测量模块并启动 TIM3_CH1 DMA 输入捕获。
 * @return 初始化成功返回 1，否则返回 0。
 */
uint8_t IntervalMeter_Init(void)
{
    interval_capture_first = 0U;
    interval_capture_second = 0U;
    interval_capture_valid = 0U;

    return MeasurementHw_PeriodCaptureStart();
}

/**
 * @brief 轮询并消费最新一组 TIM3 DMA 原始捕获值。
 *
 * 当前阶段只保存 DMA 捕获到的两个 CCR1 原始时间戳，
 * 暂不进行 TIM3 溢出扩展和周期计算。
 */
void IntervalMeter_Task(void)
{
    uint16_t first;
    uint16_t second;

    if (MeasurementHw_PeriodCaptureTakePair(&first, &second) == 0U)
    {
        return;
    }

    interval_capture_first = first;
    interval_capture_second = second;
    interval_capture_valid = 1U;
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
