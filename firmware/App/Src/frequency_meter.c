#include "frequency_meter.h"

#include "measurement_hw.h"

static uint32_t frequency_hz = 0U;
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
