#include "measurement_hw.h"

#include "tim.h"

/* TIM2 是 16 位计数器，CNT 从 0 计到 65535 后回绕，因此每次溢出代表 65536 个脉冲。 */
#define TIM2_COUNTER_RANGE 65536UL

/* TIM3 DMA 每组采集 2 个 CCR1 值：
 * [0] 保存第一次上升沿捕获值，[1] 保存第二次上升沿捕获值。
 */
#define TIM3_CAPTURE_DMA_LENGTH 2U

/* 用半个 16 位计数范围判断捕获值位于回绕点的哪一侧。
 * 该阈值只用于解决“输入捕获”和“Update 回绕”几乎同时发生时的软件归属判断。
 */
#define TIM3_CAPTURE_OVERFLOW_HALF_RANGE 32768U

/* TIM2 在当前 1 秒闸门测量期间发生的溢出次数，用于把 16 位 CNT 扩展为更大的总脉冲计数。
 * 数值含义：
 * 0U：本轮测量尚未发生 TIM2 溢出，或新一轮测量刚开始。
 * n：本轮测量已发生 n 次 TIM2 16 位回绕；每增加 1 代表额外累计 65536 个外部脉冲。
 */
static volatile uint32_t frequency_counter_overflow_count = 0U;

/* TIM4 闸门结束时锁存的最终总脉冲数；1 秒闸门下该数值可直接对应频率 Hz。
 * 数值含义：
 * 0U：初始化状态，或 1 秒闸门内没有检测到有效外部脉冲。
 * >0U：最近一次完整 1 秒闸门内统计到的外部脉冲总数。
 */
static volatile uint32_t frequency_counter_latched_count = 0U;

/* 高频闸门计数结果就绪标志。
 * 状态值：
 * 0U：本轮结果尚未完成，或上一轮结果已经被上层开始下一轮测量时消费。
 * 1U：TIM4 闸门已经结束，frequency_counter_latched_count 中有一组新的完整结果可读取。
 */
static volatile uint8_t frequency_counter_ready = 0U;

/* TIM2 + TIM4 高频闸门计数运行状态。
 * 状态值：
 * 0U：当前没有正在执行的高频闸门计数测量，可以启动新一轮。
 * 1U：TIM2/TIM4 正在执行一轮测量，此时禁止重复启动。
 */
static volatile uint8_t frequency_counter_running = 0U;

/* TIM3_CH1 输入捕获 DMA 缓冲区。
 * 数组元素含义：
 * [0]：第一次 PA6 上升沿到来时，TIM3 硬件锁存到 CCR1 的 16 位 CNT 值。
 * [1]：第二次 PA6 上升沿到来时，TIM3 硬件锁存到 CCR1 的 16 位 CNT 值。
 * 初始均为 0U；DMA 运行后由硬件自动覆盖。
 */
static volatile uint16_t tim3_capture_dma_buffer[TIM3_CAPTURE_DMA_LENGTH] = {0U, 0U};

/* 最近一次完整 DMA 采集得到的第一个 CCR1 原始时间戳。
 * 数值范围：0U~65535U，表示第一次上升沿对应的 TIM3 CCR1 原始 16 位计数值。
 * 注意：0U 也可能是真实捕获值；是否存在一组新数据必须结合 tim3_capture_pair_ready 判断。
 */
static volatile uint16_t tim3_capture_first = 0U;

/* 最近一次完整 DMA 采集得到的第二个 CCR1 原始时间戳。
 * 数值范围：0U~65535U，表示第二次上升沿对应的 TIM3 CCR1 原始 16 位计数值。
 * 注意：0U 也可能是真实捕获值；是否存在一组新数据必须结合 tim3_capture_pair_ready 判断。
 */
static volatile uint16_t tim3_capture_second = 0U;

/* TIM3 DMA 一组两个 CCR1 捕获值的就绪标志。
 * 状态值：
 * 0U：尚未完成一组两个捕获值，或该组数据已经被 MeasurementHw_PeriodCaptureTakePair() 消费。
 * 1U：tim3_capture_first / tim3_capture_second 中有一组新的完整捕获值等待上层读取。
 */
static volatile uint8_t tim3_capture_pair_ready = 0U;

/* TIM3 从本轮输入捕获启动以来累计发生的 CNT 回绕次数。
 * 状态值：
 * 0U：尚未发生 65535 → 0，或本轮捕获刚启动。
 * n：已经发生 n 次回绕；每增加 1 代表额外经过 65536 个 timer tick。
 */
static volatile uint32_t tim3_overflow_count = 0U;

/* 当前 DMA 这一组中，第一个 CCR1 捕获对应的临时溢出圈数。
 * Half Complete 时写入，Complete 时再锁存到 tim3_first_overflow_count。
 * 这样 Circular DMA 开始下一组后，不会覆盖上一组已经完成的数据。
 */
static volatile uint32_t tim3_pending_first_overflow_count = 0U;

/* 最近一组完整 DMA 捕获中，第一个 CCR1 对应的 TIM3 累计溢出次数。 */
static volatile uint32_t tim3_first_overflow_count = 0U;

/* 最近一组完整 DMA 捕获中，第二个 CCR1 对应的 TIM3 累计溢出次数。 */
static volatile uint32_t tim3_second_overflow_count = 0U;

/**
 * @brief 根据 CCR1 捕获值和 TIM3 当前 Update 状态，确定该捕获属于哪一圈。
 *
 * 正常情况下直接使用 tim3_overflow_count。
 * 如果 Update Flag 仍然挂起：
 * - captured_value 较小，说明更可能是“先回绕、后捕获”，该捕获应属于下一圈；
 * - captured_value 较大，说明更可能是“先捕获、后回绕”，不应把这次回绕算进去。
 *
 * 另外保留一个兜底判断：若 Update ISR 已经先处理完，UIF 已清零，但捕获值在高半区、
 * 当前 CNT 已进入低半区，则说明很可能是“捕获后刚刚回绕”，需要把已经累计的一圈扣回。
 *
 * @param captured_value DMA 搬运得到的 CCR1 原始 16 位捕获值。
 * @return 该捕获瞬间应对应的软件溢出圈数。
 */
static uint32_t MeasurementHw_TIM3CaptureOverflowSnapshot(uint16_t captured_value)
{
    uint32_t overflow_count = tim3_overflow_count;
    uint32_t update_pending = __HAL_TIM_GET_FLAG(&htim3, TIM_FLAG_UPDATE);
    uint16_t counter_now = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);

    if (update_pending != RESET)
    {
        if (captured_value < TIM3_CAPTURE_OVERFLOW_HALF_RANGE)
        {
            overflow_count++;
        }

        return overflow_count;
    }

    if ((captured_value >= TIM3_CAPTURE_OVERFLOW_HALF_RANGE) &&
        (counter_now < TIM3_CAPTURE_OVERFLOW_HALF_RANGE) &&
        (overflow_count > 0U))
    {
        overflow_count--;
    }

    return overflow_count;
}

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
 * @brief 原子地读取并消费最近一组 TIM3 DMA 捕获结果。
 *
 * 一组完整结果包含两个 CCR1 原始捕获值，以及两个捕获时刻各自对应的 TIM3 溢出圈数。
 * 为避免 DMA Complete 回调恰好在读取过程中更新其中任一字段，这里只在极短时间内
 * 临时关闭中断，一次性复制整组数据并清除 ready 标志。
 *
 * @param first 用于接收第一个 CCR1 原始捕获值的地址。
 * @param first_overflow_count 用于接收第一个捕获时刻累计溢出圈数的地址。
 * @param second 用于接收第二个 CCR1 原始捕获值的地址。
 * @param second_overflow_count 用于接收第二个捕获时刻累计溢出圈数的地址。
 * @return 成功消费到一组新数据返回 1；当前没有新数据或任一参数为空返回 0。
 */
uint8_t MeasurementHw_PeriodCaptureTakePair(
    uint16_t *first,
    uint32_t *first_overflow_count,
    uint16_t *second,
    uint32_t *second_overflow_count)
{
    uint32_t primask;

    if ((first == NULL) ||
        (first_overflow_count == NULL) ||
        (second == NULL) ||
        (second_overflow_count == NULL))
    {
        return 0U;
    }

    /* PRIMASK 是 ARM Cortex-M 内核里的一个寄存器，主要用来控制可屏蔽中断。 */
    /* PRIMASK = 0：普通中断允许响应；PRIMASK = 1：普通中断被屏蔽。 */
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
    *first_overflow_count = tim3_first_overflow_count;
    *second = tim3_capture_second;
    *second_overflow_count = tim3_second_overflow_count;
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

    tim3_overflow_count = 0U;
    tim3_pending_first_overflow_count = 0U;
    tim3_first_overflow_count = 0U;
    tim3_second_overflow_count = 0U;

    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_CC1);
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);

    /* TIM3_CH1 使用 DMA 搬运 CCR1；这里额外打开 Update 中断，只用于累计 CNT 回绕次数。 */
    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE);

    if (HAL_TIM_IC_Start_DMA(
            &htim3,
            TIM_CHANNEL_1,
            (uint32_t *)tim3_capture_dma_buffer,
            TIM3_CAPTURE_DMA_LENGTH) != HAL_OK)
    {
        __HAL_TIM_DISABLE_IT(&htim3, TIM_IT_UPDATE);
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

    if (htim->Instance == TIM3)
    {
        /* TIM3 CNT 每次从 65535 回绕到 0，软件高位累计 1 圈。 */
        tim3_overflow_count++;
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
 * @brief TIM 输入捕获 DMA 半传输完成回调。
 *
 * DMA 缓冲区长度为 2，因此第一个 CCR1 值搬入 buffer[0] 后触发 Half Complete。
 * 这里结合 CCR1 数值、Update Flag 和当前软件圈数，记录第一次捕获真正所属的圈数。
 * @param htim 触发本次回调的定时器句柄。
 */
void HAL_TIM_IC_CaptureHalfCpltCallback(TIM_HandleTypeDef *htim)
{
    if ((htim->Instance == TIM3) &&
        (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1))
    {
        tim3_pending_first_overflow_count =
            MeasurementHw_TIM3CaptureOverflowSnapshot(tim3_capture_dma_buffer[0]);
    }
}

/**
 * @brief TIM 输入捕获 DMA 完成回调。
 *
 * TIM3_CH1 的 DMA 每收集两个 CCR1 值后进入该回调，
 * 将两个 CCR1 原始捕获值及其对应的溢出圈数一起锁存为一组完整结果。
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

        /* 将这一组的两个“圈数快照”与两个 CCR1 一起锁存。
         * first 的临时值来自 Half Complete；second 在 Complete 时现场判断。
         */
        tim3_first_overflow_count = tim3_pending_first_overflow_count;
        tim3_second_overflow_count =
            MeasurementHw_TIM3CaptureOverflowSnapshot(tim3_capture_dma_buffer[1]);

        tim3_capture_pair_ready = 1U;
    }
}

