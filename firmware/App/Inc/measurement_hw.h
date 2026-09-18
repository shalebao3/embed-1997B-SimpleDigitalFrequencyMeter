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

/**
 * @brief 启动 TIM3_CH1 输入捕获 DMA。
 * @return 启动成功返回 1，否则返回 0。
 */
uint8_t MeasurementHw_PeriodCaptureStart(void);

/**
 * @brief 判断是否已经得到一组完整的 TIM3 DMA 捕获值。
 * @return 已有一组新的捕获值返回 1，否则返回 0。
 */
uint8_t MeasurementHw_PeriodCaptureIsReady(void);

/**
 * @brief 获取最近一次 DMA 完成后锁存的第一个 CCR1 捕获值。
 * @return 第一个 CCR1 时间戳。
 */
uint16_t MeasurementHw_PeriodCaptureGetFirst(void);

/**
 * @brief 获取最近一次 DMA 完成后锁存的第二个 CCR1 捕获值。
 * @return 第二个 CCR1 时间戳。
 */
uint16_t MeasurementHw_PeriodCaptureGetSecond(void);

/**
 * @brief 原子地读取并消费最近一组 TIM3 DMA 捕获结果。
 *
 * 一组完整结果包含两个 CCR1 原始捕获值，以及两个捕获时刻各自对应的 TIM3 溢出圈数。
 *
 * @param first 用于接收第一个 CCR1 原始捕获值的地址。
 * @param first_overflow_count 用于接收第一个捕获时刻累计溢出圈数的地址。
 * @param second 用于接收第二个 CCR1 原始捕获值的地址。
 * @param second_overflow_count 用于接收第二个捕获时刻累计溢出圈数的地址。
 * @return 成功消费到一组新数据返回 1；当前没有新数据或任一参数为空返回 0。
 */
uint8_t MeasurementHw_PeriodCaptureTakePair(
    uint16_t *first,
    uint32_t *first_overflow_count,
    uint16_t *second,
    uint32_t *second_overflow_count);

#endif
