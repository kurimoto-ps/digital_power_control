/* SPDX-License-Identifier: Apache-2.0 */
#ifndef REV58_BOARD_IO_PROTOCOL_H
#define REV58_BOARD_IO_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
enum board_io_kind { IO_UNKNOWN, IO_INVALID, PLC_READ, PLC_WRITE,
                     UART_STATUS, UART_CLEAR, UART_READ, UART_SEND };
struct board_io_request { enum board_io_kind kind; uint16_t mask; uint8_t data[32]; size_t size; };
struct board_io_request board_io_parse(const char *line);
#endif
