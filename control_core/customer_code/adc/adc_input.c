#include "adc_input.h"

uint32_t adc_input_raw_to_millivolts(uint32_t raw)
{
	if (raw > ADC_INPUT_MAX_RAW) {
		raw = ADC_INPUT_MAX_RAW;
	}
	return (uint32_t)(((uint64_t)raw * ADC_INPUT_FULL_SCALE_MV +
			   (ADC_INPUT_MAX_RAW / 2U)) /
			  ADC_INPUT_MAX_RAW);
}
