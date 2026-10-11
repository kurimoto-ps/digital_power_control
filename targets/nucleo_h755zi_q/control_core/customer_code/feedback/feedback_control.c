#include "feedback_control.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define FEEDBACK_SAMPLE_RATE_HZ 10000LL
#define DUTY_PERCENT_MAX 100LL
#define Q16_ONE 65536LL
#define DUTY_Q16_MAX (DUTY_PERCENT_MAX * Q16_ONE)

/* Conservative starting gains for two cascaded 4.7 kOhm / 1 uF RC sections. */
#define PI_KP_MILLI 500LL
#define PI_KI_PER_SECOND_MILLI 50000LL

static int64_t integrator_q16;
static uint32_t target_percent = 50U;
static struct k_spinlock pi_lock;

static int64_t clamp_duty_q16(int64_t duty_q16)
{
	return CLAMP(duty_q16, 0LL, DUTY_Q16_MAX);
}

void feedback_control_reset(uint32_t initial_duty_percent)
{
	k_spinlock_key_t key = k_spin_lock(&pi_lock);

	integrator_q16 = (int64_t)MIN(initial_duty_percent, 100U) * Q16_ONE;
	k_spin_unlock(&pi_lock, key);
}

void feedback_control_set_target_percent(uint32_t requested_target_percent)
{
	k_spinlock_key_t key = k_spin_lock(&pi_lock);

	target_percent = MIN(requested_target_percent, 100U);
	k_spin_unlock(&pi_lock, key);
}

uint32_t feedback_control_get_target_percent(void)
{
	k_spinlock_key_t key = k_spin_lock(&pi_lock);
	uint32_t current_target_percent = target_percent;

	k_spin_unlock(&pi_lock, key);
	return current_target_percent;
}

uint32_t feedback_control_pi_step(const struct adc_input_sample *sample)
{
	k_spinlock_key_t key = k_spin_lock(&pi_lock);
	int64_t target_raw = ((int64_t)target_percent * ADC_INPUT_MAX_RAW) / 100LL;
	int64_t error_raw = target_raw - MIN(sample->raw, ADC_INPUT_MAX_RAW);
	int64_t error_percent_q16 = (error_raw * DUTY_PERCENT_MAX * Q16_ONE) /
				    ADC_INPUT_MAX_RAW;
	int64_t proportional_q16 = (error_percent_q16 * PI_KP_MILLI) / 1000LL;
	int64_t integrator_delta_q16 =
		(error_percent_q16 * PI_KI_PER_SECOND_MILLI) /
		(1000LL * FEEDBACK_SAMPLE_RATE_HZ);
	int64_t unsaturated_q16 = proportional_q16 + integrator_q16;

	/* Do not integrate farther into saturation. */
	if (!((unsaturated_q16 >= DUTY_Q16_MAX && integrator_delta_q16 > 0LL) ||
	      (unsaturated_q16 <= 0LL && integrator_delta_q16 < 0LL))) {
		integrator_q16 = clamp_duty_q16(integrator_q16 + integrator_delta_q16);
		unsaturated_q16 = proportional_q16 + integrator_q16;
	}

	uint32_t duty_percent = (uint32_t)((clamp_duty_q16(unsaturated_q16) +
				       (Q16_ONE / 2LL)) / Q16_ONE);

	k_spin_unlock(&pi_lock, key);
	return duty_percent;
}
