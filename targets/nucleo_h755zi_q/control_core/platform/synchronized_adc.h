#ifndef SYNCHRONIZED_ADC_H_
#define SYNCHRONIZED_ADC_H_

#include "adc_input.h"

#include <zephyr/kernel.h>

int synchronized_adc_init(void);
int synchronized_adc_wait_sample(struct adc_input_sample *sample, k_timeout_t timeout);

#endif
