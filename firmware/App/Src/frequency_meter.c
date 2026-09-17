#include "frequency_meter.h"

#include "measurement_hw.h"

/* 最近一次已经完成并确认有效的频率测量结果，单位：Hz。 */
static uint32_t frequency_hz = 0U;

/* 频率结果有效标志：0 表示尚无有效结果，1 表示 frequency_hz 可以使用。 */
static uint8_t frequency_valid = 0U;

uint8_t FrequencyMeter_Init(void)
{
    frequency_hz = 0U;
    frequency_valid = 0U;

    return MeasurementHw_FrequencyCounterStart();
}

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

uint32_t FrequencyMeter_GetFrequencyHz(void)
{
    return frequency_hz;
}

uint8_t FrequencyMeter_IsValid(void)
{
    return frequency_valid;
}
