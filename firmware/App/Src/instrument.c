#include "instrument.h"

#include "main.h"
#include "self_calibration.h"

void Instrument_Init(void)
{
    /* Initialize the self-calibration process */
    if (!SelfCalibration_Start())
    {
        Error_Handler();
    }
}

void Instrument_Task(void)
{
}