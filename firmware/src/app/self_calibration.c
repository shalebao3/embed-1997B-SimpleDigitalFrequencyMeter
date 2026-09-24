#include "self_calibration.h"

#include "measurement_hw.h"

/**
 * @brief 启动自校功能，通过硬件测量层开启 1MHz 自校时标输出。
 * @return 自校时标启动成功返回 1，启动失败返回 0。
 */
uint8_t SelfCalibration_Start(void)
{
    return MeasurementHw_SelfCalibrationStart();
}

/**
 * @brief 停止自校功能，并通过硬件测量层关闭 1MHz 自校时标输出。
 */
void SelfCalibration_Stop(void)
{
    MeasurementHw_SelfCalibrationStop();
}
