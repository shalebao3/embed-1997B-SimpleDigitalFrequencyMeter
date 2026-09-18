#ifndef INSTRUMENT_UI_PORT_H
#define INSTRUMENT_UI_PORT_H

#include <stdint.h>

#include "instrument_ui.h"

/**
 * @brief 把当前 UI 数据帧输出到具体显示设备。
 *
 * 当前默认实现为空操作；后续接 OLED / LCD / 数码管时只替换本端口实现，
 * 不需要修改测量算法和 UI 调度逻辑。
 */
void InstrumentUiPort_Render(const InstrumentUiFrame *frame);

/**
 * @brief 根据当前测量模式驱动三种不同颜色的模式 LED。
 *
 * 当前默认实现为空操作；具体颜色和 GPIO 绑定留到硬件阶段决定。
 */
void InstrumentUiPort_SetModeLed(InstrumentUiMode mode);

/**
 * @brief 尝试读取用于控制 1~10s 显示刷新时间的 12 位 ADC 原始值。
 *
 * @param adc_raw 成功时写入 0~4095。
 * @return 当前有可用 ADC 数据返回 1；没有配置 ADC / 暂无新值返回 0。
 */
uint8_t InstrumentUiPort_ReadRefreshAdcRaw(uint16_t *adc_raw);

#endif
