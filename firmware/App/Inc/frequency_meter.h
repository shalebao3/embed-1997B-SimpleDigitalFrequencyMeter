#ifndef FREQUENCY_METER_H
#define FREQUENCY_METER_H

#include <stdint.h>

/**
 * @brief Initialize the high-frequency gate-count measurement loop.
 * @return 1 if the first measurement started successfully, 0 otherwise.
 */
uint8_t FrequencyMeter_Init(void);

/**
 * @brief Poll the measurement state and start the next gate when one finishes.
 */
void FrequencyMeter_Task(void);

/**
 * @brief Return the latest measured frequency.
 *
 * The current hardware gate is fixed at one second, so the latched pulse count
 * is numerically equal to frequency in hertz.
 *
 * @return Latest frequency in hertz.
 */
uint32_t FrequencyMeter_GetFrequencyHz(void);

/**
 * @brief Check whether at least one complete measurement is available.
 * @return 1 after the first gate has completed, 0 before that.
 */
uint8_t FrequencyMeter_IsValid(void);

#endif
