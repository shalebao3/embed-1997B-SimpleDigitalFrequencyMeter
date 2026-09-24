#ifndef INSTRUMENT_UI_H
#define INSTRUMENT_UI_H

#include <stdint.h>

typedef enum
{
    INSTRUMENT_UI_MODE_FREQUENCY = 0,
    INSTRUMENT_UI_MODE_PERIOD,
    INSTRUMENT_UI_MODE_PULSE_WIDTH
} InstrumentUiMode;

typedef struct
{
    InstrumentUiMode mode;
    uint8_t valid;
    uint64_t frequency_millihz;
    uint64_t period_ns;
    uint64_t pulse_width_ns;
    uint32_t refresh_period_ms;
} InstrumentUiFrame;

/**
 * @brief 初始化仪器 UI 状态，默认进入频率显示模式。
 */
void InstrumentUi_Init(void);

/**
 * @brief 执行显示刷新、ADC 刷新周期读取和模式结果整理。
 */
void InstrumentUi_Task(void);

/**
 * @brief 设置当前显示 / LED 模式。
 */
void InstrumentUi_SetMode(InstrumentUiMode mode);

/**
 * @brief 按“频率 → 周期 → 脉宽”顺序切换到下一模式。
 */
void InstrumentUi_NextMode(void);

InstrumentUiMode InstrumentUi_GetMode(void);

/**
 * @brief 将 12 位 ADC 原始值连续映射到 1~10 秒刷新周期。
 * @param adc_raw 0~4095。
 */
void InstrumentUi_SetRefreshAdcRaw(uint16_t adc_raw);

/**
 * @brief 直接设置刷新周期；超出范围时自动限制在 1000~10000ms。
 */
void InstrumentUi_SetRefreshPeriodMs(uint32_t refresh_period_ms);

uint32_t InstrumentUi_GetRefreshPeriodMs(void);

/**
 * @brief 获取最近一次已经送往显示端口的数据帧。
 */
const InstrumentUiFrame *InstrumentUi_GetLastFrame(void);

#endif
