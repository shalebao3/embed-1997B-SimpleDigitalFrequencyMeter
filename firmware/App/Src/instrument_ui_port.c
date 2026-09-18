#include "instrument_ui_port.h"

/**
 * 当前文件是硬件适配层的默认空实现。
 *
 * 以后确定 OLED / LCD / 数码管、LED GPIO 和 ADC 通道后，只需要替换这里，
 * App 层的测量、超时、模式和刷新调度逻辑无需重写。
 */

void InstrumentUiPort_Render(const InstrumentUiFrame *frame)
{
    (void)frame;
}

void InstrumentUiPort_SetModeLed(InstrumentUiMode mode)
{
    (void)mode;
}

uint8_t InstrumentUiPort_ReadRefreshAdcRaw(uint16_t *adc_raw)
{
    (void)adc_raw;
    return 0U;
}
