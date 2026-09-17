#include "measurement_hw.h"

#include "tim.h"

#define TIM2_COUNTER_RANGE 65536UL

static volatile uint32_t frequency_counter_overflow_count = 0U;
static volatile uint32_t frequency_counter_latched_count = 0U;
static volatile uint8_t frequency_counter_ready = 0U;
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

    /* Only consume the previous ready result after the new measurement has
     * started successfully. This lets the upper layer retry if HAL start fails.
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
        uint32_t overflow_count = frequency_counter_overflow_count;
        uint32_t counter_value;

        /* TIM4 is configured in one-pulse mode. Its update event clears CEN,
         * so TRGO=ENABLE closes TIM2's hardware gate before the count is read.
         */
        counter_value = __HAL_TIM_GET_COUNTER(&htim2);

        /* TIM2 and TIM4 currently share the same NVIC priority. If TIM2 wrapped
         * exactly near the gate boundary, its update flag can still be pending
         * while this TIM4 callback runs. Account for that wrap before latching
         * the final result, then clear it so it cannot be counted twice later.
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
