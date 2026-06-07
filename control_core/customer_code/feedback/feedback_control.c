#include "feedback_control.h"

#include "adc_input.h"

#include <zephyr/sys/util.h>

uint32_t feedback_control_duty_from_adc(uint32_t adc_raw)
{
	adc_raw = MIN(adc_raw, ADC_INPUT_MAX_RAW);
	return (uint32_t)(((uint64_t)adc_raw * 100U + (ADC_INPUT_MAX_RAW / 2U)) /
			  ADC_INPUT_MAX_RAW);
}
