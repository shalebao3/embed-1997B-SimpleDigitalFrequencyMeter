#ifndef SELF_CALIBRATION_H
#define SELF_CALIBRATION_H

#include <stdint.h>

/**
 * @brief Start the self-calibration process.
 * @return 1 if the calibration started successfully, 0 otherwise.
 */
uint8_t SelfCalibration_Start(void);

/**
 * @brief Stop the self-calibration process.
 */
void SelfCalibration_Stop(void);

#endif
