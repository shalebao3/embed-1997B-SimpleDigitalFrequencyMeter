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

/**
 * @brief Start one high-frequency gate-count measurement.
 *
 * TIM2 counts external pulses on PA0/ETR while TIM4 keeps the hardware gate
 * open for one second. TIM2 update interrupts extend the 16-bit counter.
 *
 * @return 1 if TIM2 and TIM4 started successfully, 0 otherwise.
 */
uint8_t MeasurementHw_FrequencyCounterStart(void);

/**
 * @brief Check whether the current gate-count measurement has finished.
 * @return 1 when a result is ready, 0 otherwise.
 */
uint8_t MeasurementHw_FrequencyCounterIsReady(void);

/**
 * @brief Read the pulse count latched at the end of the one-second gate.
 *
 * With the current one-second gate, the returned pulse count is numerically
 * equal to the measured frequency in hertz.
 *
 * @return Total number of external pulses counted during the gate window.
 */
uint32_t MeasurementHw_FrequencyCounterGetCount(void);

#endif
