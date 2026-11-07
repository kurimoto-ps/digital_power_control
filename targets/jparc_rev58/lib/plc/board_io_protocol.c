/* SPDX-License-Identifier: Apache-2.0 */
#include "board_io_protocol.h"
#include "spi4_protocol.h"
#include <stdio.h>
#include <string.h>
static int hex(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}
struct board_io_request board_io_parse(const char *line)
{
    struct board_io_request r = {0};
    char t[4][65];
    int n = sscanf(line, "%64s %64s %64s %64s", t[0], t[1], t[2], t[3]);
    if (n < 1 || (strcmp(t[0], "PLC") && strcmp(t[0], "UART"))) { return r; }
    r.kind = IO_INVALID;
    if (n == 2) {
        if (!strcmp(t[0], "PLC") && !strcmp(t[1], "READ")) { r.kind = PLC_READ; }
        if (!strcmp(t[0], "UART")) {
            if (!strcmp(t[1], "STATUS")) { r.kind = UART_STATUS; }
            if (!strcmp(t[1], "CLEAR")) { r.kind = UART_CLEAR; }
            if (!strcmp(t[1], "READ")) { r.kind = UART_READ; }
        }
    }
    if (n == 3 && !strcmp(t[0], "PLC") && !strcmp(t[1], "WRITE")) {
        uint32_t mask;
        if (spi4_number(t[2], 65535, &mask)) { r.kind = PLC_WRITE; r.mask = mask; }
    }
    if (n == 3 && !strcmp(t[0], "UART") && !strcmp(t[1], "SEND")) {
        size_t size = strlen(t[2]);
        if (size % 2 || size == 0 || size > 64) { return r; }
        for (size_t i = 0; i < size / 2; ++i) {
            int a = hex(t[2][2*i]), b = hex(t[2][2*i+1]);
            if (a < 0 || b < 0) { return r; }
            r.data[i] = (a << 4) | b;
        }
        r.kind = UART_SEND; r.size = size / 2;
    }
    return r;
}
