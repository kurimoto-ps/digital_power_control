#include "adc_input.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>

static const struct adc_dt_spec feedback_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

int adc_input_init(void)
{
	if (!adc_is_ready_dt(&feedback_adc)) {
		return -ENODEV;
	}

	return adc_channel_setup_dt(&feedback_adc);
}

int adc_input_read(struct adc_input_sample *sample)
{
	uint32_t raw = 0U;
	int32_t millivolts;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int ret;

	if (sample == NULL) {
		return -EINVAL;
	}

	ret = adc_sequence_init_dt(&feedback_adc, &sequence);
	if (ret != 0) {
		return ret;
	}

	ret = adc_read_dt(&feedback_adc, &sequence);
	if (ret != 0) {
		return ret;
	}

	millivolts = (int32_t)raw;
	ret = adc_raw_to_millivolts_dt(&feedback_adc, &millivolts);
	if (ret != 0) {
		return ret;
	}

	sample->raw = raw;
	sample->millivolts = (millivolts < 0) ? 0U : (uint32_t)millivolts;
	return 0;
}
