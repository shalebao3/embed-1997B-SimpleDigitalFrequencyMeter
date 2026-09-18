#include "instrument_ui.h"

#include "frequency_auto.h"
#include "interval_meter.h"
#include "instrument_ui_port.h"
#include "main.h"
#include "pulse_width_meter.h"

#define INSTRUMENT_UI_REFRESH_MIN_MS 1000U
#define INSTRUMENT_UI_REFRESH_MAX_MS 10000U
#define INSTRUMENT_UI_REFRESH_SPAN_MS \
    (INSTRUMENT_UI_REFRESH_MAX_MS - INSTRUMENT_UI_REFRESH_MIN_MS)
#define INSTRUMENT_UI_ADC_MAX 4095U

static InstrumentUiMode instrument_ui_mode = INSTRUMENT_UI_MODE_FREQUENCY;
static uint32_t instrument_ui_refresh_period_ms = INSTRUMENT_UI_REFRESH_MIN_MS;
static uint32_t instrument_ui_last_refresh_tick_ms = 0U;
static uint8_t instrument_ui_force_refresh = 1U;
static InstrumentUiFrame instrument_ui_last_frame = {0};

/**
 * @brief 根据当前模式启停高开销测量路径。
 *
 * TIM3_CH2 下降沿中断只在脉宽模式启用，避免高频信号下持续产生 CC2 中断。
 */
static void InstrumentUi_ApplyModeRuntime(InstrumentUiMode mode)
{
    PulseWidthMeter_SetEnabled(
        (mode == INSTRUMENT_UI_MODE_PULSE_WIDTH) ? 1U : 0U);

    InstrumentUiPort_SetModeLed(mode);
}

/**
 * @brief 生成当前准备送往显示层的数据快照。
 */
static InstrumentUiFrame InstrumentUi_BuildFrame(void)
{
    InstrumentUiFrame frame = {0};

    frame.mode = instrument_ui_mode;
    frame.refresh_period_ms = instrument_ui_refresh_period_ms;

    /* 同时带上三类最近值，便于不同显示设备自由排版。 */
    frame.frequency_millihz = FrequencyAuto_GetFrequencyMilliHz();
    frame.period_ns = IntervalMeter_GetPeriodNs();
    frame.pulse_width_ns = PulseWidthMeter_GetWidthNs();

    switch (instrument_ui_mode)
    {
        case INSTRUMENT_UI_MODE_FREQUENCY:
            frame.valid = FrequencyAuto_IsValid();
            break;

        case INSTRUMENT_UI_MODE_PERIOD:
            frame.valid = IntervalMeter_IsCaptureValid();
            break;

        case INSTRUMENT_UI_MODE_PULSE_WIDTH:
            frame.valid = PulseWidthMeter_IsValid();
            break;

        default:
            frame.valid = 0U;
            break;
    }

    return frame;
}

void InstrumentUi_Init(void)
{
    instrument_ui_mode = INSTRUMENT_UI_MODE_FREQUENCY;
    instrument_ui_refresh_period_ms = INSTRUMENT_UI_REFRESH_MIN_MS;
    instrument_ui_last_refresh_tick_ms = HAL_GetTick();
    instrument_ui_force_refresh = 1U;
    instrument_ui_last_frame = InstrumentUi_BuildFrame();

    InstrumentUi_ApplyModeRuntime(instrument_ui_mode);
}

void InstrumentUi_Task(void)
{
    uint16_t adc_raw;
    uint32_t now_ms = HAL_GetTick();

    /* 硬件阶段若接入电位器 ADC，端口层返回新值后即可连续调节 1~10s。 */
    if (InstrumentUiPort_ReadRefreshAdcRaw(&adc_raw) != 0U)
    {
        InstrumentUi_SetRefreshAdcRaw(adc_raw);
    }

    if ((instrument_ui_force_refresh == 0U) &&
        ((uint32_t)(now_ms - instrument_ui_last_refresh_tick_ms) <
         instrument_ui_refresh_period_ms))
    {
        return;
    }

    instrument_ui_last_frame = InstrumentUi_BuildFrame();
    InstrumentUiPort_Render(&instrument_ui_last_frame);

    instrument_ui_last_refresh_tick_ms = now_ms;
    instrument_ui_force_refresh = 0U;
}

void InstrumentUi_SetMode(InstrumentUiMode mode)
{
    if ((mode != INSTRUMENT_UI_MODE_FREQUENCY) &&
        (mode != INSTRUMENT_UI_MODE_PERIOD) &&
        (mode != INSTRUMENT_UI_MODE_PULSE_WIDTH))
    {
        return;
    }

    if (instrument_ui_mode == mode)
    {
        return;
    }

    instrument_ui_mode = mode;
    instrument_ui_force_refresh = 1U;

    InstrumentUi_ApplyModeRuntime(mode);
}

void InstrumentUi_NextMode(void)
{
    switch (instrument_ui_mode)
    {
        case INSTRUMENT_UI_MODE_FREQUENCY:
            InstrumentUi_SetMode(INSTRUMENT_UI_MODE_PERIOD);
            break;

        case INSTRUMENT_UI_MODE_PERIOD:
            InstrumentUi_SetMode(INSTRUMENT_UI_MODE_PULSE_WIDTH);
            break;

        case INSTRUMENT_UI_MODE_PULSE_WIDTH:
        default:
            InstrumentUi_SetMode(INSTRUMENT_UI_MODE_FREQUENCY);
            break;
    }
}

InstrumentUiMode InstrumentUi_GetMode(void)
{
    return instrument_ui_mode;
}

void InstrumentUi_SetRefreshAdcRaw(uint16_t adc_raw)
{
    uint32_t raw = adc_raw;

    if (raw > INSTRUMENT_UI_ADC_MAX)
    {
        raw = INSTRUMENT_UI_ADC_MAX;
    }

    instrument_ui_refresh_period_ms =
        INSTRUMENT_UI_REFRESH_MIN_MS +
        ((raw * INSTRUMENT_UI_REFRESH_SPAN_MS) +
         (INSTRUMENT_UI_ADC_MAX / 2U)) /
        INSTRUMENT_UI_ADC_MAX;
}

void InstrumentUi_SetRefreshPeriodMs(uint32_t refresh_period_ms)
{
    if (refresh_period_ms < INSTRUMENT_UI_REFRESH_MIN_MS)
    {
        refresh_period_ms = INSTRUMENT_UI_REFRESH_MIN_MS;
    }
    else if (refresh_period_ms > INSTRUMENT_UI_REFRESH_MAX_MS)
    {
        refresh_period_ms = INSTRUMENT_UI_REFRESH_MAX_MS;
    }

    instrument_ui_refresh_period_ms = refresh_period_ms;
}

uint32_t InstrumentUi_GetRefreshPeriodMs(void)
{
    return instrument_ui_refresh_period_ms;
}

const InstrumentUiFrame *InstrumentUi_GetLastFrame(void)
{
    return &instrument_ui_last_frame;
}
