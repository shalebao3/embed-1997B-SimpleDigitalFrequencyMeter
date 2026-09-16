#ifndef MEASUREMENT_HW_H
#define MEASUREMENT_HW_H

#include <stdint.h>

/**
 * @brief Start the self-calibration process.
 * @return 1 if the calibration started successfully, 0 otherwise.
 */
uint8_t MeasurementHw_SelfCalibrationStart(void);
/**
 * @brief Stop the self-calibration process.
 */
void MeasurementHw_SelfCalibrationStop(void);

#endif
