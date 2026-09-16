#include "measurement_hw.h"

#include "tim.h"

uint8_t MeasurementHw_SelfCalibrationStart(void)
{
    return (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) == HAL_OK) ? 1U : 0U;
}

void MeasurementHw_SelfCalibrationStop(void)
{
    (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}