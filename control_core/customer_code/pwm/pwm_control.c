#include "pwm_control.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <stm32h7xx_ll_tim.h>

#define MIN_FREQUENCY_HZ 20U
#define MAX_FREQUENCY_HZ 20000U
#define MAX_DEADTIME_NS 4000U

#define PWM_TIMER_NODE DT_NODELABEL(timers1)
#define PWM_TIMER ((TIM_TypeDef *)DT_REG_ADDR(PWM_TIMER_NODE))
#define PWM_TIMER_PRESCALER DT_PROP(PWM_TIMER_NODE, st_prescaler)

static const struct pwm_dt_spec pwm_high = PWM_DT_SPEC_GET(DT_ALIAS(pwm_high));
static const struct pwm_dt_spec pwm_low = PWM_DT_SPEC_GET(DT_ALIAS(pwm_low));
static struct pwm_control_state current_state;
static struct k_mutex pwm_lock;

static int validate(uint32_t frequency_hz, uint32_t duty_percent,
		    uint32_t deadtime_ns)
{
	if ((frequency_hz < MIN_FREQUENCY_HZ) ||
	    (frequency_hz > MAX_FREQUENCY_HZ) ||
	    (duty_percent > 100U) || (deadtime_ns > MAX_DEADTIME_NS)) {
		return -EINVAL;
	}

	return 0;
}

static int set_deadtime(uint32_t deadtime_ns)
{
	uint64_t counter_clock_hz;
	uint64_t timer_clock_hz;
	uint32_t deadtime_register;
	int ret;

	ret = pwm_get_cycles_per_sec(pwm_high.dev, pwm_high.channel,
				     &counter_clock_hz);
	if (ret != 0) {
		return ret;
	}

	timer_clock_hz = counter_clock_hz * (PWM_TIMER_PRESCALER + 1U);
	deadtime_register = __LL_TIM_CALC_DEADTIME(
		timer_clock_hz, LL_TIM_GetClockDivision(PWM_TIMER), deadtime_ns);
	if ((deadtime_ns != 0U) && (deadtime_register == 0U)) {
		return -ERANGE;
	}

	LL_TIM_OC_SetDeadTime(PWM_TIMER, deadtime_register);
	return 0;
}

int pwm_control_init(void)
{
	k_mutex_init(&pwm_lock);
	if (!pwm_is_ready_dt(&pwm_high) || !pwm_is_ready_dt(&pwm_low)) {
		return -ENODEV;
	}
	return pwm_control_off();
}

int pwm_control_set(uint32_t frequency_hz, uint32_t duty_percent,
		    uint32_t deadtime_ns)
{
	uint32_t period_ns;
	uint32_t pulse_ns;
	int ret;

	ret = validate(frequency_hz, duty_percent, deadtime_ns);
	if (ret != 0) {
		return ret;
	}

	period_ns = NSEC_PER_SEC / frequency_hz;
	pulse_ns = (uint32_t)(((uint64_t)period_ns * duty_percent) / 100U);

	k_mutex_lock(&pwm_lock, K_FOREVER);
	LL_TIM_DisableAllOutputs(PWM_TIMER);
	ret = set_deadtime(deadtime_ns);
	if (ret == 0) {
		ret = pwm_set_dt(&pwm_high, period_ns, pulse_ns);
	}
	if (ret == 0) {
		ret = pwm_set_dt(&pwm_low, period_ns, pulse_ns);
	}
	if (ret == 0) {
		current_state.frequency_hz = frequency_hz;
		current_state.duty_percent = duty_percent;
		current_state.deadtime_ns = deadtime_ns;
		current_state.enabled = true;
		LL_TIM_EnableAllOutputs(PWM_TIMER);
	} else {
		current_state.enabled = false;
	}
	k_mutex_unlock(&pwm_lock);
	return ret;
}

int pwm_control_off(void)
{
	int ret_high;
	int ret_low;

	k_mutex_lock(&pwm_lock, K_FOREVER);
	LL_TIM_DisableAllOutputs(PWM_TIMER);
	ret_high = pwm_set(pwm_high.dev, pwm_high.channel, 0U, 0U, pwm_high.flags);
	ret_low = pwm_set(pwm_low.dev, pwm_low.channel, 0U, 0U, pwm_low.flags);
	current_state.enabled = false;
	k_mutex_unlock(&pwm_lock);
	return (ret_high != 0) ? ret_high : ret_low;
}

void pwm_control_get(struct pwm_control_state *state)
{
	k_mutex_lock(&pwm_lock, K_FOREVER);
	*state = current_state;
	k_mutex_unlock(&pwm_lock);
}
