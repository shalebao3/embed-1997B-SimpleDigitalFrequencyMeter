#include "instrument.h"

#include "frequency_meter.h"
#include "interval_meter.h"
#include "main.h"
#include "self_calibration.h"

/**
 * @brief 初始化仪器应用层，依次启动自校输出和高频频率测量模块。
 *
 * 任一模块初始化失败时进入 Error_Handler，避免系统在底层外设状态异常时继续运行。
 */
void Instrument_Init(void)
{
    /* 启动 1MHz 自校时标输出。 */
    if (!SelfCalibration_Start())
    {
        Error_Handler();
    }

    if (!FrequencyMeter_Init())
    {
        Error_Handler();
    }

    if (!IntervalMeter_Init())
    {
        Error_Handler();
    }
}

/**
 * @brief 执行仪器应用层轮询任务，由主循环持续调用。
 */
void Instrument_Task(void)
{
    FrequencyMeter_Task();
    IntervalMeter_Task();
}
