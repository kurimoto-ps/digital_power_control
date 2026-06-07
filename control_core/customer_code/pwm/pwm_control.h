#ifndef PWM_CONTROL_H_
#define PWM_CONTROL_H_

#include <stdbool.h>
#include <stdint.h>

struct pwm_control_state {
	uint32_t frequency_hz;
	uint32_t duty_percent;
	uint32_t deadtime_ns;
	bool enabled;
};

int pwm_control_init(void);
int pwm_control_set(uint32_t frequency_hz, uint32_t duty_percent,
		    uint32_t deadtime_ns);
int pwm_control_off(void);
void pwm_control_get(struct pwm_control_state *state);

#endif
