#ifndef FREQUENCY_AUTO_H
#define FREQUENCY_AUTO_H

#include <stdint.h>

typedef enum
{
    FREQUENCY_AUTO_METHOD_NONE = 0,
    FREQUENCY_AUTO_METHOD_GATE,
    FREQUENCY_AUTO_METHOD_PERIOD
} FrequencyAutoMethod;

/**
 * @brief 初始化高低频自动选择模块。
 */
void FrequencyAuto_Init(void);

/**
 * @brief 根据闸门法与周期法最新结果自动选择当前频率结果。
 */
void FrequencyAuto_Task(void);

/**
 * @brief 判断当前是否已有有效自动测频结果。
 * @return 有效返回 1，否则返回 0。
 */
uint8_t FrequencyAuto_IsValid(void);

/**
 * @brief 获取自动选择后的最终频率。
 * @return 频率，单位 mHz；尚无有效结果时返回 0。
 */
uint64_t FrequencyAuto_GetFrequencyMilliHz(void);

/**
 * @brief 获取当前正在采用的测频方法。
 */
FrequencyAutoMethod FrequencyAuto_GetMethod(void);

#endif
