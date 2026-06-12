#ifndef FEEDBACK_CONTROL_H_
#define FEEDBACK_CONTROL_H_

#include "adc_input.h"

#include <stdint.h>

void feedback_control_reset(uint32_t initial_duty_percent);
void feedback_control_set_target_percent(uint32_t target_percent);
uint32_t feedback_control_get_target_percent(void);
uint32_t feedback_control_pi_step(const struct adc_input_sample *sample);

#endif
