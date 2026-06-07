#ifndef ADC_INPUT_H_
#define ADC_INPUT_H_

#include <stdint.h>

#define ADC_INPUT_FULL_SCALE_MV 3300U
#define ADC_INPUT_MAX_RAW 65535U

struct adc_input_sample {
	uint32_t raw;
	uint32_t millivolts;
};

int adc_input_init(void);
int adc_input_read(struct adc_input_sample *sample);

#endif
