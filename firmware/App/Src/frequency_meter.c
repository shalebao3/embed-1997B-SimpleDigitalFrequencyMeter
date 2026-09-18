#include "frequency_meter.h"

#include "measurement_hw.h"

/* 最近一次已经完成并确认有效的频率测量结果，单位：Hz。
 * 数值含义：
 * 0U：初始状态下表示尚未得到频率结果；若 frequency_valid == 1U，则表示有效测量结果为 0Hz。
 * >0U：最近一次测得的频率值。
 */
static uint32_t frequency_hz = 0U;

/* 频率结果有效标志。
 * 状态值：
 * 0U：尚未得到至少一组完整的频率测量结果，frequency_hz 当前不能作为有效结果使用。
 * 1U：至少已经完成一组频率测量，frequency_hz 保存最近一次有效结果。
 */
static uint8_t frequency_valid = 0U;

/**
 * @brief 初始化高频闸门计数频率计，并启动第一轮测量。
 * @return 第一轮测量启动成功返回 1，启动失败返回 0。
 */
uint8_t FrequencyMeter_Init(void)
{
    frequency_hz = 0U;
    frequency_valid = 0U;

    return MeasurementHw_FrequencyCounterStart();
}

/**
 * @brief 轮询频率测量状态；一轮完成后保存最新频率结果，并立即启动下一轮测量。
 */
void FrequencyMeter_Task(void)
{
    if (MeasurementHw_FrequencyCounterIsReady() == 0U)
    {
        return;
    }

    frequency_hz = MeasurementHw_FrequencyCounterGetCount();
    frequency_valid = 1U;

    (void)MeasurementHw_FrequencyCounterStart();
}

/**
 * @brief 获取最近一次已经完成的频率测量结果。
 * @return 最近一次测得的频率，单位 Hz。
 */
uint32_t FrequencyMeter_GetFrequencyHz(void)
{
    return frequency_hz;
}

/**
 * @brief 判断当前是否已经存在至少一组有效的频率测量结果。
 * @return 已有有效结果返回 1，否则返回 0。
 */
uint8_t FrequencyMeter_IsValid(void)
{
    return frequency_valid;
}
