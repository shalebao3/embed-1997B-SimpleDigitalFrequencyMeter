#ifndef SELF_CALIBRATION_H
#define SELF_CALIBRATION_H

#include <stdint.h>

/**
 * @brief 启动自校功能，输出 1MHz 自校时标信号。
 * @return 启动成功返回 1，启动失败返回 0。
 */
uint8_t SelfCalibration_Start(void);

/**
 * @brief 停止自校功能，并关闭 1MHz 自校时标输出。
 */
void SelfCalibration_Stop(void);

#endif
