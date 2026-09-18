#include "frequency_auto.h"

#include "frequency_meter.h"
#include "interval_meter.h"

/* 第一版软件切换阈值。
 *
 * 1 秒闸门法在 2kHz 时 ±1 计数约为 0.05%，已经满足基本要求；
 * 周期法在 5kHz 时一个周期约 14400 个 TIM3 tick，也有充分分辨率。
 *
 * 使用 2kHz / 5kHz 两个不同阈值形成滞回，避免输入频率在边界附近时
 * 在两种测量方法之间频繁来回切换。最终阈值应在实机测试后再校准。
 */
#define FREQUENCY_AUTO_GATE_TO_PERIOD_HZ 2000ULL
#define FREQUENCY_AUTO_PERIOD_TO_GATE_HZ 5000ULL
#define MILLIHZ_PER_HZ 1000ULL

static uint64_t frequency_auto_millihz = 0ULL;
static uint8_t frequency_auto_valid = 0U;
static FrequencyAutoMethod frequency_auto_method = FREQUENCY_AUTO_METHOD_NONE;

void FrequencyAuto_Init(void)
{
    frequency_auto_millihz = 0ULL;
    frequency_auto_valid = 0U;
    frequency_auto_method = FREQUENCY_AUTO_METHOD_NONE;
}

void FrequencyAuto_Task(void)
{
    uint8_t gate_valid = FrequencyMeter_IsValid();
    uint8_t period_valid = IntervalMeter_IsCaptureValid();
    uint64_t gate_millihz = 0ULL;
    uint64_t period_millihz = 0ULL;

    if (gate_valid != 0U)
    {
        gate_millihz =
            (uint64_t)FrequencyMeter_GetFrequencyHz() * MILLIHZ_PER_HZ;
    }

    if (period_valid != 0U)
    {
        period_millihz = IntervalMeter_GetFrequencyMilliHz();
    }

    if ((gate_valid == 0U) && (period_valid == 0U))
    {
        frequency_auto_valid = 0U;
        frequency_auto_method = FREQUENCY_AUTO_METHOD_NONE;
        frequency_auto_millihz = 0ULL;
        return;
    }

    if (frequency_auto_method == FREQUENCY_AUTO_METHOD_NONE)
    {
        /* 启动阶段优先使用闸门法作为全频段粗测。
         * 如果已经确认处于低频区且周期法有结果，再切换到周期法。
         */
        if (gate_valid != 0U)
        {
            if ((FrequencyMeter_GetFrequencyHz() <=
                 FREQUENCY_AUTO_GATE_TO_PERIOD_HZ) &&
                (period_valid != 0U) &&
                (period_millihz > 0ULL))
            {
                frequency_auto_method = FREQUENCY_AUTO_METHOD_PERIOD;
            }
            else
            {
                frequency_auto_method = FREQUENCY_AUTO_METHOD_GATE;
            }
        }
        else
        {
            frequency_auto_method = FREQUENCY_AUTO_METHOD_PERIOD;
        }
    }

    if (frequency_auto_method == FREQUENCY_AUTO_METHOD_GATE)
    {
        if ((gate_valid != 0U) &&
            (FrequencyMeter_GetFrequencyHz() <=
             FREQUENCY_AUTO_GATE_TO_PERIOD_HZ) &&
            (period_valid != 0U) &&
            (period_millihz > 0ULL))
        {
            frequency_auto_method = FREQUENCY_AUTO_METHOD_PERIOD;
        }
    }
    else if (frequency_auto_method == FREQUENCY_AUTO_METHOD_PERIOD)
    {
        if (((period_valid == 0U) ||
             (period_millihz >=
              (FREQUENCY_AUTO_PERIOD_TO_GATE_HZ * MILLIHZ_PER_HZ))) &&
            (gate_valid != 0U))
        {
            frequency_auto_method = FREQUENCY_AUTO_METHOD_GATE;
        }
    }

    if ((frequency_auto_method == FREQUENCY_AUTO_METHOD_PERIOD) &&
        (period_valid != 0U))
    {
        frequency_auto_millihz = period_millihz;
        frequency_auto_valid = 1U;
        return;
    }

    if ((frequency_auto_method == FREQUENCY_AUTO_METHOD_GATE) &&
        (gate_valid != 0U))
    {
        frequency_auto_millihz = gate_millihz;
        frequency_auto_valid = 1U;
        return;
    }

    /* 当前选定方法暂时没有有效结果时，允许使用另一种已经有效的方法兜底。 */
    if (period_valid != 0U)
    {
        frequency_auto_method = FREQUENCY_AUTO_METHOD_PERIOD;
        frequency_auto_millihz = period_millihz;
        frequency_auto_valid = 1U;
    }
    else if (gate_valid != 0U)
    {
        frequency_auto_method = FREQUENCY_AUTO_METHOD_GATE;
        frequency_auto_millihz = gate_millihz;
        frequency_auto_valid = 1U;
    }
    else
    {
        frequency_auto_method = FREQUENCY_AUTO_METHOD_NONE;
        frequency_auto_millihz = 0ULL;
        frequency_auto_valid = 0U;
    }
}

uint8_t FrequencyAuto_IsValid(void)
{
    return frequency_auto_valid;
}

uint64_t FrequencyAuto_GetFrequencyMilliHz(void)
{
    return frequency_auto_millihz;
}

FrequencyAutoMethod FrequencyAuto_GetMethod(void)
{
    return frequency_auto_method;
}
