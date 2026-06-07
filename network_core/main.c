/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * TCP commands:
 *   SET <frequency_hz> <duty_percent> <deadtime_ns>
 *   OFF
 *   GET
 *   HELP
 */

#include "control_client.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(digital_power_control, LOG_LEVEL_INF);

#define SERVER_PORT 4242
#define COMMAND_BUFFER_SIZE 160

static struct net_mgmt_event_callback ipv4_cb;

static int send_text(int fd, const char *text)
{
	size_t remaining = strlen(text);

	while (remaining > 0U) {
		ssize_t sent = zsock_send(fd, text, remaining, 0);
		if (sent < 0) {
			return -errno;
		}
		text += sent;
		remaining -= sent;
	}
	return 0;
}

static int parse_u32(const char *text, uint32_t *value)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 10);
	if ((errno != 0) || (end == text) || (*end != '\0') ||
	    (parsed > UINT32_MAX)) {
		return -EINVAL;
	}
	*value = (uint32_t)parsed;
	return 0;
}

static const char *mode_name(uint32_t mode)
{
	return (mode == CONTROL_MODE_FEEDBACK) ? "feedback" : "feedforward";
}

static void process_command(int client, char *command)
{
	char response[224];
	char *save;
	char *verb = strtok_r(command, " \t", &save);

	if (verb == NULL) {
		return;
	}

	if (strcmp(verb, "SET") == 0) {
		char *frequency_text = strtok_r(NULL, " \t", &save);
		char *duty_text = strtok_r(NULL, " \t", &save);
		char *deadtime_text = strtok_r(NULL, " \t", &save);
		char *extra = strtok_r(NULL, " \t", &save);
		uint32_t frequency_hz;
		uint32_t duty_percent;
		uint32_t deadtime_ns;
		int ret;

		if ((frequency_text == NULL) || (duty_text == NULL) ||
		    (deadtime_text == NULL) || (extra != NULL) ||
		    (parse_u32(frequency_text, &frequency_hz) != 0) ||
		    (parse_u32(duty_text, &duty_percent) != 0) ||
		    (parse_u32(deadtime_text, &deadtime_ns) != 0)) {
			send_text(client, "ERR usage: SET <20..20000 Hz> <0..100 duty> <0..4000 ns>\r\n");
			return;
		}

		struct control_response control_response;

		ret = control_client_set(frequency_hz, duty_percent, deadtime_ns,
					 &control_response);
		if (ret != 0) {
			snprintk(response, sizeof(response),
				 "ERR cannot set PWM (%d)\r\n", ret);
		} else {
			snprintk(response, sizeof(response),
				 "OK mode=%s frequency=%uHz duty=%u%% deadtime=%uns adc=%umV\r\n",
				 mode_name(control_response.mode), control_response.frequency_hz,
				 control_response.duty_percent, control_response.deadtime_ns,
				 control_response.adc_mv);
		}
		send_text(client, response);
		return;
	}

	if (strcmp(verb, "MODE") == 0) {
		char *mode_text = strtok_r(NULL, " \t", &save);
		char *extra = strtok_r(NULL, " \t", &save);
		uint32_t mode;
		struct control_response state;
		int ret;

		if ((mode_text == NULL) || (extra != NULL)) {
			send_text(client, "ERR usage: MODE <FEEDFORWARD|FEEDBACK>\r\n");
			return;
		}
		if (strcmp(mode_text, "FEEDFORWARD") == 0) {
			mode = CONTROL_MODE_FEEDFORWARD;
		} else if (strcmp(mode_text, "FEEDBACK") == 0) {
			mode = CONTROL_MODE_FEEDBACK;
		} else {
			send_text(client, "ERR mode must be FEEDFORWARD or FEEDBACK\r\n");
			return;
		}

		ret = control_client_set_mode(mode, &state);
		if (ret == 0) {
			snprintk(response, sizeof(response), "OK mode=%s adc=%umV duty=%u%%\r\n",
				 mode_name(state.mode), state.adc_mv, state.duty_percent);
		} else {
			snprintk(response, sizeof(response), "ERR cannot set mode (%d)\r\n", ret);
		}
		send_text(client, response);
		return;
	}

	if (strcmp(verb, "OFF") == 0) {
		struct control_response control_response;
		int ret = control_client_off(&control_response);
		if (ret == 0) {
			send_text(client, "OK PWM off\r\n");
		} else {
			snprintk(response, sizeof(response), "ERR cannot stop PWM (%d)\r\n", ret);
			send_text(client, response);
		}
		return;
	}

	if (strcmp(verb, "GET") == 0) {
		struct control_response state;

		if (control_client_get(&state) != 0) {
			send_text(client, "ERR M4 control core unavailable\r\n");
			return;
		}
		snprintk(response, sizeof(response),
			 "OK mode=%s enabled=%u frequency=%uHz duty=%u%% deadtime=%uns adc_raw=%u adc=%umV fault=0x%08x\r\n",
			 mode_name(state.mode), state.enabled, state.frequency_hz, state.duty_percent,
			 state.deadtime_ns, state.adc_raw, state.adc_mv, state.fault_flags);
		send_text(client, response);
		return;
	}

	if (strcmp(verb, "STATUS") == 0) {
		snprintk(response, sizeof(response), "OK rpmsg_bound=%u\r\n",
			 control_client_is_ready());
		send_text(client, response);
		return;
	}

	if (strcmp(verb, "HELP") == 0) {
		send_text(client, "Commands: SET <frequency_hz> <duty_percent> <deadtime_ns>, MODE <FEEDFORWARD|FEEDBACK>, OFF, GET, STATUS, HELP\r\n");
		return;
	}
	send_text(client, "ERR unknown command; send HELP\r\n");
}

static void serve_client(int client)
{
	char buffer[COMMAND_BUFFER_SIZE];
	size_t used = 0U;

	send_text(client, "NUCLEO-H755ZI-Q complementary PWM server; send HELP\r\n");
	while (true) {
		ssize_t received = zsock_recv(client, &buffer[used], sizeof(buffer) - used - 1U, 0);
		if (received <= 0) {
			return;
		}
		used += received;
		buffer[used] = '\0';

		while (true) {
			char *newline = strchr(buffer, '\n');
			size_t command_len;
			if (newline == NULL) {
				break;
			}
			*newline = '\0';
			command_len = strlen(buffer);
			if ((command_len > 0U) && (buffer[command_len - 1U] == '\r')) {
				buffer[command_len - 1U] = '\0';
			}
			process_command(client, buffer);
			used -= (size_t)(newline - buffer) + 1U;
			memmove(buffer, newline + 1, used);
			buffer[used] = '\0';
		}
		if (used == (sizeof(buffer) - 1U)) {
			send_text(client, "ERR command too long\r\n");
			used = 0U;
		}
	}
}

static void log_ipv4_address(struct net_if *iface, struct net_if_addr *if_addr, void *user_data)
{
	char address[NET_IPV4_ADDR_LEN];
	ARG_UNUSED(iface);
	ARG_UNUSED(user_data);
	if (if_addr->addr_type == NET_ADDR_MANUAL) {
		LOG_INF("IPv4 address: %s", net_addr_ntop(AF_INET, &if_addr->address.in_addr,
						       address, sizeof(address)));
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			       struct net_if *iface)
{
	ARG_UNUSED(cb);
	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		net_if_ipv4_addr_foreach(iface, log_ipv4_address, NULL);
	}
}

int main(void)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(SERVER_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	int server;

	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	server = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server < 0) {
		LOG_ERR("socket failed: %d", errno);
		return 0;
	}
	if (zsock_bind(server, (struct sockaddr *)&address, sizeof(address)) < 0) {
		LOG_ERR("bind failed: %d", errno);
		zsock_close(server);
		return 0;
	}
	if (zsock_listen(server, 1) < 0) {
		LOG_ERR("listen failed: %d", errno);
		zsock_close(server);
		return 0;
	}

	LOG_INF("TCP PWM server listening on port %d", SERVER_PORT);
	while (true) {
		int client = zsock_accept(server, NULL, NULL);
		if (client < 0) {
			LOG_ERR("accept failed: %d", errno);
			continue;
		}
		serve_client(client);
		zsock_close(client);
	}
	return 0;
}
