#ifndef FREQUENCY_METER_H
#define FREQUENCY_METER_H

#include <stdint.h>

/**
 * @brief 初始化高频闸门计数测频模块，并启动第一轮频率测量。
 * @return 启动成功返回 1，启动失败返回 0。
 */
uint8_t FrequencyMeter_Init(void);

/**
 * @brief 轮询频率测量状态；当前一轮完成后保存结果并启动下一轮测量。
 */
void FrequencyMeter_Task(void);

/**
 * @brief 获取最近一次已经完成的频率测量结果。
 *
 * 当前硬件闸门固定为 1 秒，因此闸门期间锁存的总脉冲数在数值上
 * 直接等于频率值，单位为 Hz。
 *
 * @return 最近一次测得的频率，单位 Hz。
 */
uint32_t FrequencyMeter_GetFrequencyHz(void);

/**
 * @brief 判断是否已经至少完成过一次有效的频率测量。
 * @return 已有有效测量结果返回 1，否则返回 0。
 */
uint8_t FrequencyMeter_IsValid(void);

#endif
