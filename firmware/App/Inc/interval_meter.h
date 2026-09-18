#ifndef INTERVAL_METER_H
#define INTERVAL_METER_H

#include <stdint.h>

/**
 * @brief 初始化时间间隔测量模块并启动 TIM3_CH1 DMA 输入捕获。
 * @return 初始化成功返回 1，否则返回 0。
 */
uint8_t IntervalMeter_Init(void);

/**
 * @brief 轮询并消费最新一组 TIM3 DMA 原始捕获值。
 *
 * 当前阶段只保存两个 CCR1 原始时间戳，不进行溢出扩展和周期计算。
 */
void IntervalMeter_Task(void);

/**
 * @brief 判断是否至少已经收到一组完整的原始捕获值。
 * @return 已收到返回 1，否则返回 0。
 */
uint8_t IntervalMeter_IsCaptureValid(void);

/**
 * @brief 获取最近一次保存的第一个 CCR1 原始捕获值。
 */
uint16_t IntervalMeter_GetFirstCapture(void);

/**
 * @brief 获取最近一次保存的第二个 CCR1 原始捕获值。
 */
uint16_t IntervalMeter_GetSecondCapture(void);

/**
 * @brief 获取最近一次保存的第一个 CCR1 捕获时对应的 TIM3 累计溢出圈数。
 */
uint32_t IntervalMeter_GetFirstOverflowCount(void);

/**
 * @brief 获取最近一次保存的第二个 CCR1 捕获时对应的 TIM3 累计溢出圈数。
 */
uint32_t IntervalMeter_GetSecondOverflowCount(void);

#endif
