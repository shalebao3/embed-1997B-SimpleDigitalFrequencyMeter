#include "self_calibration.h"

#include "measurement_hw.h"

uint8_t SelfCalibration_Start(void)
{
    return MeasurementHw_SelfCalibrationStart();
}

void SelfCalibration_Stop(void)
{
    MeasurementHw_SelfCalibrationStop();
}