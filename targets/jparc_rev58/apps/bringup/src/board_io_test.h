/* SPDX-License-Identifier: Apache-2.0 */
#ifndef REV58_BOARD_IO_TEST_H
#define REV58_BOARD_IO_TEST_H
#include "spi4_protocol.h"
int board_io_init(void);
bool board_io_command(const char *line, char reply[SPI4_REPLY_SIZE]);
#endif
