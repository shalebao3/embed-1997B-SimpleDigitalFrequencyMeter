#include "measurement_hw.h"

#include "tim.h"

/* TIM2 是 16 位计数器，CNT 从 0 计到 65535 后回绕，因此每次溢出代表 65536 个脉冲。 */
#define TIM2_COUNTER_RANGE 65536UL

/* TIM2 在当前 1 秒闸门测量期间发生的溢出次数，用于把 16 位 CNT 扩展为更大的总脉冲计数。 */
static volatile uint32_t frequency_counter_overflow_count = 0U;

/* TIM4 闸门结束时锁存的最终总脉冲数；1 秒闸门下该数值可直接对应频率 Hz。 */
static volatile uint32_t frequency_counter_latched_count = 0U;

/* 测量结果就绪标志：0 表示结果尚未完成或已被消费，1 表示已有新的锁存结果可读取。 */
static volatile uint8_t frequency_counter_ready = 0U;

/* 高频闸门计数运行状态：0 表示未测量，1 表示 TIM2/TIM4 正在进行一轮测量。 */
static volatile uint8_t frequency_counter_running = 0U;

uint8_t MeasurementHw_SelfCalibrationStart(void)
{
    return (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) == HAL_OK) ? 1U : 0U;
}

void MeasurementHw_SelfCalibrationStop(void)
{
    (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}

uint8_t MeasurementHw_FrequencyCounterStart(void)
{
    if (frequency_counter_running != 0U)
    {
        return 0U;
    }

    frequency_counter_overflow_count = 0U;

    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    __HAL_TIM_CLEAR_FLAG(&htim4, TIM_FLAG_UPDATE);

    if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK)
    {
        return 0U;
    }

    frequency_counter_running = 1U;

    if (HAL_TIM_Base_Start_IT(&htim4) != HAL_OK)
    {
        frequency_counter_running = 0U;
        (void)HAL_TIM_Base_Stop_IT(&htim2);
        return 0U;
    }

    /* 新一轮测量成功启动后，才清除上一轮的结果就绪标志。
     * 如果 HAL 启动失败，上层仍然可以保留上一轮有效结果并继续重试。
     */
    frequency_counter_ready = 0U;

    return 1U;
}

uint8_t MeasurementHw_FrequencyCounterIsReady(void)
{
    return frequency_counter_ready;
}

uint32_t MeasurementHw_FrequencyCounterGetCount(void)
{
    return frequency_counter_latched_count;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        if (frequency_counter_running != 0U)
        {
            frequency_counter_overflow_count++;
        }
        return;
    }

    if (htim->Instance == TIM4)
    {
        /* 先复制已经由 TIM2 中断累计完成的溢出次数，后面再检查是否还有尚未处理的溢出标志。 */
        uint32_t overflow_count = frequency_counter_overflow_count;

        /* TIM4 关闭 1 秒闸门后，读取此刻 TIM2 CNT 中剩余的 16 位脉冲计数。 */
        uint32_t counter_value;

        /* TIM4 使用单脉冲模式。发生更新事件后 CEN 会被硬件清零，
         * TRGO=ENABLE 随之关闭 TIM2 的硬件 Gate，因此这里读取 CNT 时计数已经停止。
         */
        counter_value = __HAL_TIM_GET_COUNTER(&htim2);

        /* TIM2 和 TIM4 当前使用相同 NVIC 优先级。
         * 如果 TIM2 恰好在闸门结束附近发生回绕，TIM2 的 Update Flag 可能已经置位，
         * 但 TIM2_IRQHandler 还没来得及执行。这里补记这一次溢出并清除标志，
         * 防止少算 65536 个脉冲，同时避免之后 TIM2 中断再次重复累计。
         */
        if (__HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_UPDATE) != RESET)
        {
            overflow_count++;
            __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
        }

        (void)HAL_TIM_Base_Stop_IT(&htim2);

        frequency_counter_latched_count =
            (overflow_count * TIM2_COUNTER_RANGE) + counter_value;
        frequency_counter_running = 0U;
        frequency_counter_ready = 1U;
    }
}
