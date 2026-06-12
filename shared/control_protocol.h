#ifndef CONTROL_PROTOCOL_H_
#define CONTROL_PROTOCOL_H_

#include <stdint.h>

#define CONTROL_PROTOCOL_MAGIC 0x50574d43U
#define CONTROL_PROTOCOL_VERSION 3U
#define CONTROL_ENDPOINT_NAME "power-control"
#define CONTROL_HEARTBEAT_INTERVAL_MS 500U
#define CONTROL_HEARTBEAT_TIMEOUT_MS 2000U

enum control_command_type {
	CONTROL_COMMAND_SET = 1,
	CONTROL_COMMAND_OFF = 2,
	CONTROL_COMMAND_GET = 3,
	CONTROL_COMMAND_HEARTBEAT = 4,
	CONTROL_COMMAND_SET_MODE = 5,
};

enum control_mode {
	CONTROL_MODE_FEEDFORWARD = 0,
	CONTROL_MODE_FEEDBACK = 1,
};

struct control_command {
	uint32_t magic;
	uint16_t version;
	uint16_t type;
	uint32_t sequence;
	uint32_t frequency_hz;
	uint32_t duty_percent;
	uint32_t deadtime_ns;
	uint32_t mode;
};

struct control_response {
	uint32_t magic;
	uint16_t version;
	uint16_t type;
	uint32_t sequence;
	int32_t result;
	uint32_t frequency_hz;
	uint32_t duty_percent;
	uint32_t target_percent;
	uint32_t deadtime_ns;
	uint32_t enabled;
	uint32_t fault_flags;
	uint32_t mode;
	uint32_t adc_raw;
	uint32_t adc_mv;
};

#endif
