#include "measurement_hw.h"

#include "tim.h"

/* TIM2 是 16 位计数器，CNT 从 0 计到 65535 后回绕，因此每次溢出代表 65536 个脉冲。 */
#define TIM2_COUNTER_RANGE 65536UL

#define TIM3_CAPTURE_DMA_LENGTH 2U

/* TIM2 在当前 1 秒闸门测量期间发生的溢出次数，用于把 16 位 CNT 扩展为更大的总脉冲计数。 */
static volatile uint32_t frequency_counter_overflow_count = 0U;

/* TIM4 闸门结束时锁存的最终总脉冲数；1 秒闸门下该数值可直接对应频率 Hz。 */
static volatile uint32_t frequency_counter_latched_count = 0U;

/* 测量结果就绪标志：0 表示结果尚未完成或已被消费，1 表示已有新的锁存结果可读取。 */
static volatile uint8_t frequency_counter_ready = 0U;

/* 高频闸门计数运行状态：0 表示未测量，1 表示 TIM2/TIM4 正在进行一轮测量。 */
static volatile uint8_t frequency_counter_running = 0U;

/* TIM3_CH1 输入捕获 DMA 缓冲区。
 * 每次 PA6 出现上升沿，TIM3 会把 CNT 锁存到 CCR1，
 * DMA 再把 CCR1 自动搬到该数组。
 */
static volatile uint16_t tim3_capture_dma_buffer[TIM3_CAPTURE_DMA_LENGTH] = {0U, 0U};

/* 最近一次完整 DMA 采集得到的第一个 CCR1 时间戳。 */
static volatile uint16_t tim3_capture_first = 0U;

/* 最近一次完整 DMA 采集得到的第二个 CCR1 时间戳。 */
static volatile uint16_t tim3_capture_second = 0U;

/* 两个输入捕获值是否已经采集完成。 */
static volatile uint8_t tim3_capture_pair_ready = 0U;

/**
 * @brief 启动 1MHz 自校时标输出。
 * @return TIM1 PWM 启动成功返回 1，启动失败返回 0。
 */
uint8_t MeasurementHw_SelfCalibrationStart(void)
{
    return (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) == HAL_OK) ? 1U : 0U;
}

/**
 * @brief 停止 1MHz 自校时标输出。
 */
void MeasurementHw_SelfCalibrationStop(void)
{
    (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}

/**
 * @brief 启动一轮 TIM2 + TIM4 高频闸门计数测量。
 *
 * 启动前会清零 TIM2/TIM4 的 CNT 和更新标志，并重置本轮 TIM2 溢出计数。
 * TIM2 先进入外部脉冲计数状态，随后启动 TIM4 的 1 秒单脉冲闸门。
 *
 * @return 本轮测量成功启动返回 1；已经在测量或任一定时器启动失败时返回 0。
 */
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

/**
 * @brief 判断当前一轮高频闸门计数是否已经完成。
 * @return 已有新的锁存结果返回 1，否则返回 0。
 */
uint8_t MeasurementHw_FrequencyCounterIsReady(void)
{
    return frequency_counter_ready;
}

/**
 * @brief 获取最近一次 TIM4 闸门结束时锁存的外部脉冲总数。
 * @return 最近一次 1 秒闸门内统计到的外部脉冲总数。
 */
uint32_t MeasurementHw_FrequencyCounterGetCount(void)
{
    return frequency_counter_latched_count;
}


/**
 * @brief 判断 TIM3 DMA 是否已经完成一组两个 CCR1 值的采集。
 * @return 已经有一组新的捕获值返回 1，否则返回 0。
 */
uint8_t MeasurementHw_PeriodCaptureIsReady(void)
{
    return tim3_capture_pair_ready;
}

/**
 * @brief 获取最近一次 DMA 完成后锁存的第一个 CCR1 捕获值。
 * @return 第一个 CCR1 时间戳。
 */
uint16_t MeasurementHw_PeriodCaptureGetFirst(void)
{
    return tim3_capture_first;
}

/**
 * @brief 获取最近一次 DMA 完成后锁存的第二个 CCR1 捕获值。
 * @return 第二个 CCR1 时间戳。
 */
uint16_t MeasurementHw_PeriodCaptureGetSecond(void)
{
    return tim3_capture_second;
}

/**
 * @brief 原子地读取并消费最近一组 TIM3 DMA 捕获值。
 *
 * 为避免 DMA 完成回调恰好在读取过程中更新锁存值，这里只在极短时间内
 * 临时关闭中断，完成 first/second 的复制并清除 ready 标志。
 *
 * @param first 用于接收第一个 CCR1 捕获值的地址。
 * @param second 用于接收第二个 CCR1 捕获值的地址。
 * @return 成功消费到一组新数据返回 1；当前没有新数据或参数为空返回 0。
 */
uint8_t MeasurementHw_PeriodCaptureTakePair(uint16_t *first, uint16_t *second)
{
    uint32_t primask;

    if ((first == NULL) || (second == NULL))
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if (tim3_capture_pair_ready == 0U)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return 0U;
    }

    *first = tim3_capture_first;
    *second = tim3_capture_second;
    tim3_capture_pair_ready = 0U;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return 1U;
}

/**
 * @brief 启动 TIM3_CH1 输入捕获 DMA。
 *
 * PA6 每出现一个上升沿，TIM3 硬件会把当前 CNT 锁存到 CCR1，
 * DMA1 Channel 6 再把 CCR1 自动搬入 tim3_capture_dma_buffer。
 *
 * 当前 DMA 使用 Circular 模式，每两个捕获值组成一组。
 *
 * @return 启动成功返回 1，否则返回 0。
 */
uint8_t MeasurementHw_PeriodCaptureStart(void)
{
    tim3_capture_first = 0U;
    tim3_capture_second = 0U;
    tim3_capture_pair_ready = 0U;

    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_CC1);
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);

    if (HAL_TIM_IC_Start_DMA(
            &htim3,
            TIM_CHANNEL_1,
            (uint32_t *)tim3_capture_dma_buffer,
            TIM3_CAPTURE_DMA_LENGTH) != HAL_OK)
    {
        return 0U;
    }

    return 1U;
}

/**
 * @brief HAL 定时器周期到达回调，用于处理 TIM2 溢出扩展和 TIM4 闸门结束事件。
 *
 * TIM2 进入该回调时累计一次 16 位 CNT 溢出；TIM4 进入该回调时关闭本轮计数、
 * 读取 TIM2 当前 CNT，并结合溢出次数锁存最终脉冲总数。
 *
 * @param htim 触发本次周期到达回调的定时器句柄。
 */
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

/**
 * @brief TIM 输入捕获 DMA 完成回调。
 *
 * TIM3_CH1 的 DMA 每收集两个 CCR1 值后进入该回调，
 * 将 DMA 缓冲区中的两个捕获时间戳锁存出来，供上层读取。
 *
 * @param htim 触发本次回调的定时器句柄。
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if ((htim->Instance == TIM3) &&
        (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1))
    {
        tim3_capture_first = tim3_capture_dma_buffer[0];
        tim3_capture_second = tim3_capture_dma_buffer[1];

        tim3_capture_pair_ready = 1U;
    }
}

