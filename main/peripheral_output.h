#ifndef PERIPHERAL_OUTPUT_H
#define PERIPHERAL_OUTPUT_H

#include "peripheral.h"

// Analog sensors.
//
// The reading is taken through the ADC one shot driver. The raw value and
// the calibrated voltage are both reported.
//
// LEDs are not driven from here. A single LED is a plain output pin, so the
// gpio_write tool covers it. Only an RGB LED would need more than one pin,
// and the model can drive the three channels with gpio_write as well.

// Read an analog sensor and report the raw value and the voltage in mV.
esp_err_t peripheral_analog_read(const peripheral_config_t *config,
                                 int *raw_out,
                                 int *millivolts_out);

// The maximum value of the ADC range of the target, used to scale readings.
int peripheral_analog_max(void);

#endif // PERIPHERAL_OUTPUT_H
