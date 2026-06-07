#ifndef ADC_INPUT_H_
#define ADC_INPUT_H_

#include <stdint.h>

#define ADC_INPUT_FULL_SCALE_MV 3300U
#define ADC_INPUT_MAX_RAW 65535U

struct adc_input_sample {
	uint32_t raw;
	uint32_t millivolts;
};

uint32_t adc_input_raw_to_millivolts(uint32_t raw);

#endif
