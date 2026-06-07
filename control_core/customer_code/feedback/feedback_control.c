#include "feedback_control.h"

#include <zephyr/sys/util.h>

uint32_t feedback_control_thru(const struct adc_input_sample *sample)
{
	uint32_t adc_raw = MIN(sample->raw, ADC_INPUT_MAX_RAW);

	return (uint32_t)(((uint64_t)adc_raw * 100U + (ADC_INPUT_MAX_RAW / 2U)) /
			  ADC_INPUT_MAX_RAW);
}
