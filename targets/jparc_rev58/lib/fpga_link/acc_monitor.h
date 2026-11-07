/* SPDX-License-Identifier: Apache-2.0 */
#ifndef REV58_ACC_MONITOR_H
#define REV58_ACC_MONITOR_H
#include "spi4_protocol.h"
typedef int (*acc_gpio_read_fn)(void *context, uint32_t *levels);
bool acc_monitor_command(const char *line, char reply[SPI4_REPLY_SIZE],
                         spi4_transfer_fn transfer, void *context,
                         acc_gpio_read_fn read_gpio, void *gpio_context);
#endif
