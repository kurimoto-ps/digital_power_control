#ifndef FEEDBACK_CONTROL_H_
#define FEEDBACK_CONTROL_H_

#include "adc_input.h"

#include <stdint.h>

uint32_t feedback_control_thru(const struct adc_input_sample *sample);

#endif
