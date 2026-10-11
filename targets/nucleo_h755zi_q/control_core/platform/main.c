#include "adc_input.h"
#include "control_protocol.h"
#include "feedback_control.h"
#include "pwm_control.h"
#include "synchronized_adc.h"

#include <errno.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ipc/rpmsg_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(power_control_m4, LOG_LEVEL_INF);

#define COMMAND_QUEUE_DEPTH 8
#define COMMAND_WAIT_INTERVAL_MS 10U
#define FEEDBACK_THREAD_STACK_SIZE 2048
#define FEEDBACK_THREAD_PRIORITY 0
#define FAULT_COMMUNICATION_TIMEOUT BIT(0)
#define FAULT_ADC_READ BIT(1)

K_MSGQ_DEFINE(command_queue, sizeof(struct control_command), COMMAND_QUEUE_DEPTH, 4);
K_THREAD_STACK_DEFINE(feedback_thread_stack, FEEDBACK_THREAD_STACK_SIZE);

static int endpoint_id = -1;
static const struct gpio_dt_spec control_led = GPIO_DT_SPEC_GET(DT_ALIAS(control_led), gpios);
static struct adc_input_sample latest_adc;
static struct k_spinlock latest_adc_lock;
static struct k_thread feedback_thread_data;
static uint32_t last_contact_ms;
static uint32_t fault_flags;
static uint32_t control_mode = CONTROL_MODE_FEEDFORWARD;
static uint32_t configured_frequency_hz = 20000U;
static uint32_t configured_deadtime_ns;
static uint32_t requested_duty_percent = 50U;
static bool synchronized_adc_ready;

static void set_latest_adc(const struct adc_input_sample *sample)
{
	k_spinlock_key_t key = k_spin_lock(&latest_adc_lock);

	latest_adc = *sample;
	k_spin_unlock(&latest_adc_lock, key);
}

static struct adc_input_sample get_latest_adc(void)
{
	struct adc_input_sample sample;
	k_spinlock_key_t key = k_spin_lock(&latest_adc_lock);

	sample = latest_adc;
	k_spin_unlock(&latest_adc_lock, key);
	return sample;
}

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
	struct adc_input_sample sample = get_latest_adc();
	struct control_response response = {
		.magic = CONTROL_PROTOCOL_MAGIC,
		.version = CONTROL_PROTOCOL_VERSION,
		.type = command->type,
		.sequence = command->sequence,
		.result = result,
		.fault_flags = fault_flags,
		.mode = control_mode,
		.adc_raw = sample.raw,
		.adc_mv = sample.millivolts,
	};

	pwm_control_get(&state);
	response.frequency_hz = state.frequency_hz;
	response.duty_percent = state.duty_percent;
	response.target_percent = feedback_control_get_target_percent();
	response.deadtime_ns = state.deadtime_ns;
	response.enabled = state.enabled;
	(void)rpmsg_service_send(endpoint_id, &response, sizeof(response));
}

static int set_mode(uint32_t mode)
{
	struct pwm_control_state state;

	if ((mode != CONTROL_MODE_FEEDFORWARD) && (mode != CONTROL_MODE_FEEDBACK)) {
		return -EINVAL;
	}
	if ((mode == CONTROL_MODE_FEEDBACK) && !synchronized_adc_ready) {
		return -ENODEV;
	}
	pwm_control_get(&state);
	if (mode == CONTROL_MODE_FEEDBACK) {
		feedback_control_set_target_percent(requested_duty_percent);
		feedback_control_reset(state.enabled ? state.duty_percent : 0U);
		control_mode = mode;
		return 0;
	}
	control_mode = mode;
	if (!state.enabled) {
		return 0;
	}
	return pwm_control_set(configured_frequency_hz, requested_duty_percent,
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
		requested_duty_percent = command->duty_percent;
		feedback_control_set_target_percent(requested_duty_percent);
		if (control_mode == CONTROL_MODE_FEEDBACK) {
			struct pwm_control_state state;

			pwm_control_get(&state);
			duty_percent = state.enabled ? state.duty_percent : 0U;
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

static void feedback_thread(void *arg1, void *arg2, void *arg3)
{
	struct adc_input_sample sample;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		if (synchronized_adc_wait_sample(&sample, K_FOREVER) != 0) {
			fault_flags |= FAULT_ADC_READ;
			(void)pwm_control_off();
			continue;
		}
		set_latest_adc(&sample);
		fault_flags &= ~FAULT_ADC_READ;
		if (control_mode == CONTROL_MODE_FEEDBACK) {
			(void)pwm_control_set_duty_percent(feedback_control_pi_step(&sample));
		}
	}
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
	ret = synchronized_adc_init();
	if (ret != 0) {
		fault_flags |= FAULT_ADC_READ;
		LOG_ERR("Synchronized ADC initialization failed (%d); feedforward remains available", ret);
	} else {
		synchronized_adc_ready = true;
		(void)k_thread_create(&feedback_thread_data, feedback_thread_stack,
				      K_THREAD_STACK_SIZEOF(feedback_thread_stack),
				      feedback_thread, NULL, NULL, NULL,
				      FEEDBACK_THREAD_PRIORITY, 0, K_NO_WAIT);
	}

	last_contact_ms = k_uptime_get_32();
	feedback_control_set_target_percent(requested_duty_percent);
	feedback_control_reset(0U);
	LOG_INF("M4 control ready; ADC1_INP15 sampled at fixed 10 kHz by TIM6/DMA, feedback=PI");

	while (true) {
		if (k_msgq_get(&command_queue, &command, K_MSEC(COMMAND_WAIT_INTERVAL_MS)) == 0) {
			process_command(&command);
		}
		pwm_control_get(&state);
		if (state.enabled &&
		    ((k_uptime_get_32() - last_contact_ms) > CONTROL_HEARTBEAT_TIMEOUT_MS)) {
			(void)pwm_control_off();
			fault_flags |= FAULT_COMMUNICATION_TIMEOUT;
			LOG_ERR("M7 heartbeat timeout; PWM stopped");
		}
	}
	return 0;
}
