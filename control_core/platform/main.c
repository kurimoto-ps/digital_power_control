#include "adc_input.h"
#include "control_protocol.h"
#include "feedback_control.h"
#include "pwm_control.h"

#include <errno.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ipc/rpmsg_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(power_control_m4, LOG_LEVEL_INF);

#define COMMAND_QUEUE_DEPTH 8
#define FEEDBACK_UPDATE_INTERVAL_MS 10U
#define FAULT_COMMUNICATION_TIMEOUT BIT(0)
#define FAULT_ADC_READ BIT(1)

K_MSGQ_DEFINE(command_queue, sizeof(struct control_command), COMMAND_QUEUE_DEPTH, 4);

static int endpoint_id = -1;
static const struct gpio_dt_spec control_led = GPIO_DT_SPEC_GET(DT_ALIAS(control_led), gpios);
static struct adc_input_sample latest_adc;
static uint32_t last_contact_ms;
static uint32_t fault_flags;
static uint32_t control_mode = CONTROL_MODE_FEEDFORWARD;
static uint32_t configured_frequency_hz = 20000U;
static uint32_t configured_deadtime_ns;
static uint32_t feedforward_duty_percent = 50U;

static int endpoint_callback(struct rpmsg_endpoint *ept, void *data, size_t len,
			     uint32_t src, void *priv)
{
	struct control_command command;

	ARG_UNUSED(ept);
	ARG_UNUSED(src);
	ARG_UNUSED(priv);

	if (len != sizeof(command)) {
		return RPMSG_SUCCESS;
	}

	memcpy(&command, data, sizeof(command));
	(void)k_msgq_put(&command_queue, &command, K_NO_WAIT);
	return RPMSG_SUCCESS;
}

static int register_endpoint(void)
{
	endpoint_id = rpmsg_service_register_endpoint(CONTROL_ENDPOINT_NAME,
						 endpoint_callback);
	return (endpoint_id < 0) ? endpoint_id : 0;
}

SYS_INIT(register_endpoint, POST_KERNEL, CONFIG_RPMSG_SERVICE_EP_REG_PRIORITY);

static void send_response(const struct control_command *command, int result)
{
	struct pwm_control_state state;
	struct control_response response = {
		.magic = CONTROL_PROTOCOL_MAGIC,
		.version = CONTROL_PROTOCOL_VERSION,
		.type = command->type,
		.sequence = command->sequence,
		.result = result,
		.fault_flags = fault_flags,
		.mode = control_mode,
		.adc_raw = latest_adc.raw,
		.adc_mv = latest_adc.millivolts,
	};

	pwm_control_get(&state);
	response.frequency_hz = state.frequency_hz;
	response.duty_percent = state.duty_percent;
	response.deadtime_ns = state.deadtime_ns;
	response.enabled = state.enabled;
	(void)rpmsg_service_send(endpoint_id, &response, sizeof(response));
}

static int update_feedback_pwm(bool force)
{
	struct pwm_control_state state;
	uint32_t duty_percent;
	int ret;

	ret = adc_input_read(&latest_adc);
	if (ret != 0) {
		fault_flags |= FAULT_ADC_READ;
		return ret;
	}
	fault_flags &= ~FAULT_ADC_READ;

	duty_percent = feedback_control_duty_from_adc(latest_adc.raw);
	pwm_control_get(&state);
	if (!force && (!state.enabled || (state.duty_percent == duty_percent))) {
		return 0;
	}

	return pwm_control_set(configured_frequency_hz, duty_percent,
			       configured_deadtime_ns);
}

static int set_mode(uint32_t mode)
{
	struct pwm_control_state state;

	if ((mode != CONTROL_MODE_FEEDFORWARD) &&
	    (mode != CONTROL_MODE_FEEDBACK)) {
		return -EINVAL;
	}

	control_mode = mode;
	pwm_control_get(&state);
	if (!state.enabled) {
		return 0;
	}

	if (mode == CONTROL_MODE_FEEDBACK) {
		return update_feedback_pwm(true);
	}

	return pwm_control_set(configured_frequency_hz, feedforward_duty_percent,
			       configured_deadtime_ns);
}

static void process_command(const struct control_command *command)
{
	uint32_t duty_percent = command->duty_percent;
	int result;

	if ((command->magic != CONTROL_PROTOCOL_MAGIC) ||
	    (command->version != CONTROL_PROTOCOL_VERSION)) {
		send_response(command, -EPROTO);
		return;
	}

	last_contact_ms = k_uptime_get_32();
	fault_flags &= ~FAULT_COMMUNICATION_TIMEOUT;

	switch (command->type) {
	case CONTROL_COMMAND_SET:
		configured_frequency_hz = command->frequency_hz;
		configured_deadtime_ns = command->deadtime_ns;
		feedforward_duty_percent = command->duty_percent;
		if (control_mode == CONTROL_MODE_FEEDBACK) {
			result = adc_input_read(&latest_adc);
			if (result == 0) {
				duty_percent = feedback_control_duty_from_adc(latest_adc.raw);
				fault_flags &= ~FAULT_ADC_READ;
			} else {
				fault_flags |= FAULT_ADC_READ;
				break;
			}
		}
		result = pwm_control_set(configured_frequency_hz, duty_percent,
					 configured_deadtime_ns);
		break;
	case CONTROL_COMMAND_SET_MODE:
		result = set_mode(command->mode);
		break;
	case CONTROL_COMMAND_OFF:
		result = pwm_control_off();
		break;
	case CONTROL_COMMAND_GET:
	case CONTROL_COMMAND_HEARTBEAT:
		result = 0;
		break;
	default:
		result = -EINVAL;
		break;
	}

	send_response(command, result);
}

int main(void)
{
	struct control_command command;
	struct pwm_control_state state;
	int ret;

	if (gpio_is_ready_dt(&control_led)) {
		(void)gpio_pin_configure_dt(&control_led, GPIO_OUTPUT_ACTIVE);
	}

	ret = pwm_control_init();
	if (ret != 0) {
		LOG_ERR("PWM initialization failed (%d)", ret);
		return 0;
	}

	ret = adc_input_init();
	if (ret != 0) {
		fault_flags |= FAULT_ADC_READ;
		LOG_ERR("ADC initialization failed (%d); feedforward remains available", ret);
	} else {
		(void)adc_input_read(&latest_adc);
	}

	last_contact_ms = k_uptime_get_32();
	LOG_INF("M4 control ready; ADC1_INP15 on Arduino A0, 0..3300 mV -> 0..100%% duty");

	while (true) {
		if (k_msgq_get(&command_queue, &command,
			       K_MSEC(FEEDBACK_UPDATE_INTERVAL_MS)) == 0) {
			process_command(&command);
		}

		pwm_control_get(&state);
		if (state.enabled &&
		    ((k_uptime_get_32() - last_contact_ms) > CONTROL_HEARTBEAT_TIMEOUT_MS)) {
			(void)pwm_control_off();
			fault_flags |= FAULT_COMMUNICATION_TIMEOUT;
			LOG_ERR("M7 heartbeat timeout; PWM stopped");
			continue;
		}

		if (state.enabled && (control_mode == CONTROL_MODE_FEEDBACK) &&
		    (update_feedback_pwm(false) != 0)) {
			(void)pwm_control_off();
			LOG_ERR("ADC feedback failed; PWM stopped");
		}
	}
	return 0;
}
