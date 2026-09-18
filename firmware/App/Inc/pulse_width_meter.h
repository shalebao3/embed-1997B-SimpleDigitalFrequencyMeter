#ifndef PULSE_WIDTH_METER_H
#define PULSE_WIDTH_METER_H

#include <stdint.h>

/**
 * @brief 初始化高电平脉冲宽度测量模块。
 *
 * 底层使用 TIM3_CH1 捕获上升沿、TIM3_CH2 捕获下降沿。
 * @return 初始化成功返回 1，否则返回 0。
 */
uint8_t PulseWidthMeter_Init(void);

/**
 * @brief 轮询并消费最新一组“上升沿 → 下降沿”捕获结果。
 */
void PulseWidthMeter_Task(void);

/**
 * @brief 判断当前是否已有有效脉冲宽度结果。
 * @return 有效返回 1，否则返回 0。
 */
uint8_t PulseWidthMeter_IsValid(void);

/**
 * @brief 获取最近一次高电平脉冲宽度对应的 TIM3 tick 数。
 * @return 脉宽 tick 数；尚无有效结果时返回 0。
 */
uint64_t PulseWidthMeter_GetWidthTicks(void);

/**
 * @brief 获取最近一次高电平脉冲宽度。
 * @return 脉冲宽度，单位 ns；尚无有效结果时返回 0。
 */
uint64_t PulseWidthMeter_GetWidthNs(void);

#endif
