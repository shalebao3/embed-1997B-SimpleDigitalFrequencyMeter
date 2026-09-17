#ifndef MEASUREMENT_HW_H
#define MEASUREMENT_HW_H

#include <stdint.h>

/**
 * @brief 启动 1MHz 自校时标输出。
 * @return 启动成功返回 1，启动失败返回 0。
 */
uint8_t MeasurementHw_SelfCalibrationStart(void);

/**
 * @brief 停止 1MHz 自校时标输出。
 */
void MeasurementHw_SelfCalibrationStop(void);

/**
 * @brief 启动一轮高频闸门计数测量。
 *
 * TIM2 通过 PA0/ETR 对外部脉冲计数，TIM4 负责保持 1 秒硬件闸门；
 * TIM2 的更新中断用于扩展 16 位 CNT 的计数范围。
 *
 * @return TIM2 和 TIM4 均启动成功返回 1，否则返回 0。
 */
uint8_t MeasurementHw_FrequencyCounterStart(void);

/**
 * @brief 判断当前一轮闸门计数是否已经完成。
 * @return 已有新的锁存结果返回 1，否则返回 0。
 */
uint8_t MeasurementHw_FrequencyCounterIsReady(void);

/**
 * @brief 获取 1 秒闸门结束时锁存的外部脉冲总数。
 *
 * 当前闸门固定为 1 秒，因此返回的总脉冲数在数值上直接等于
 * 被测信号的频率值，单位为 Hz。
 *
 * @return 当前一轮闸门窗口内统计到的外部脉冲总数。
 */
uint32_t MeasurementHw_FrequencyCounterGetCount(void);

#endif
