#include "control_client.h"

#include <errno.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/ipc/rpmsg_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(control_client, LOG_LEVEL_INF);

#define RESPONSE_TIMEOUT_MS 1000U
#define ENDPOINT_BIND_TIMEOUT_MS 3000U

static int endpoint_id = -1;
static struct control_response received_response;
static uint32_t expected_sequence;
static uint32_t next_sequence;
static struct k_mutex request_lock;
static K_SEM_DEFINE(response_sem, 0, 1);

static int endpoint_callback(struct rpmsg_endpoint *ept, void *data, size_t len,
			     uint32_t src, void *priv)
{
	struct control_response response;

	ARG_UNUSED(ept);
	ARG_UNUSED(src);
	ARG_UNUSED(priv);

	if (len != sizeof(response)) {
		return RPMSG_SUCCESS;
	}

	memcpy(&response, data, sizeof(response));
	if ((response.magic == CONTROL_PROTOCOL_MAGIC) &&
	    (response.version == CONTROL_PROTOCOL_VERSION) &&
	    (response.sequence == expected_sequence)) {
		received_response = response;
		k_sem_give(&response_sem);
	}
	return RPMSG_SUCCESS;
}

static int register_endpoint(void)
{
	k_mutex_init(&request_lock);
	endpoint_id = rpmsg_service_register_endpoint(CONTROL_ENDPOINT_NAME,
						 endpoint_callback);
	return (endpoint_id < 0) ? endpoint_id : 0;
}

SYS_INIT(register_endpoint, POST_KERNEL, CONFIG_RPMSG_SERVICE_EP_REG_PRIORITY);

static int wait_for_endpoint(void)
{
	uint32_t start = k_uptime_get_32();

	while (!rpmsg_service_endpoint_is_bound(endpoint_id)) {
		if ((k_uptime_get_32() - start) > ENDPOINT_BIND_TIMEOUT_MS) {
			return -ETIMEDOUT;
		}
		k_sleep(K_MSEC(10));
	}
	return 0;
}

static int request(uint16_t type, uint32_t frequency_hz, uint32_t duty_percent,
		   uint32_t deadtime_ns, uint32_t mode, struct control_response *response)
{
	struct control_command command = {
		.magic = CONTROL_PROTOCOL_MAGIC,
		.version = CONTROL_PROTOCOL_VERSION,
		.type = type,
		.frequency_hz = frequency_hz,
		.duty_percent = duty_percent,
		.deadtime_ns = deadtime_ns,
		.mode = mode,
	};
	int ret;

	k_mutex_lock(&request_lock, K_FOREVER);
	ret = wait_for_endpoint();
	if (ret != 0) {
		goto out;
	}

	command.sequence = ++next_sequence;
	expected_sequence = command.sequence;
	k_sem_reset(&response_sem);
	ret = rpmsg_service_send(endpoint_id, &command, sizeof(command));
	if (ret < 0) {
		goto out;
	}

	ret = k_sem_take(&response_sem, K_MSEC(RESPONSE_TIMEOUT_MS));
	if (ret == 0) {
		if (response != NULL) {
			*response = received_response;
		}
		ret = received_response.result;
	}

out:
	k_mutex_unlock(&request_lock);
	return ret;
}

int control_client_set(uint32_t frequency_hz, uint32_t duty_percent,
		       uint32_t deadtime_ns, struct control_response *response)
{
	return request(CONTROL_COMMAND_SET, frequency_hz, duty_percent, deadtime_ns, 0U,
		       response);
}

int control_client_set_mode(uint32_t mode, struct control_response *response)
{
	return request(CONTROL_COMMAND_SET_MODE, 0U, 0U, 0U, mode, response);
}

int control_client_off(struct control_response *response)
{
	return request(CONTROL_COMMAND_OFF, 0U, 0U, 0U, 0U, response);
}

int control_client_get(struct control_response *response)
{
	return request(CONTROL_COMMAND_GET, 0U, 0U, 0U, 0U, response);
}

static void heartbeat_thread(void)
{
	while (true) {
		(void)request(CONTROL_COMMAND_HEARTBEAT, 0U, 0U, 0U, 0U, NULL);
		k_sleep(K_MSEC(CONTROL_HEARTBEAT_INTERVAL_MS));
	}
}

K_THREAD_DEFINE(heartbeat_id, 2048, heartbeat_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(10), 0, 0);

bool control_client_is_ready(void)
{
	return (endpoint_id >= 0) && rpmsg_service_endpoint_is_bound(endpoint_id);
}
