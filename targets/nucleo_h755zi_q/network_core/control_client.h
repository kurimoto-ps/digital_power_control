#ifndef CONTROL_CLIENT_H_
#define CONTROL_CLIENT_H_

#include <stdbool.h>
#include <stdint.h>

#include "control_protocol.h"

int control_client_set(uint32_t frequency_hz, uint32_t duty_percent,
		       uint32_t deadtime_ns, struct control_response *response);
int control_client_set_mode(uint32_t mode, struct control_response *response);
int control_client_off(struct control_response *response);
int control_client_get(struct control_response *response);
bool control_client_is_ready(void);

#endif
