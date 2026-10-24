/* SPDX-License-Identifier: Apache-2.0 */
#ifndef JPARC_SPI4_PROTOCOL_H
#define JPARC_SPI4_PROTOCOL_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#define SPI4_LINE_SIZE 96
#define SPI4_REPLY_SIZE 2048
#define SPI4_DEVICE_ID UINT32_C(0x4a503538)
#define SPI4_VERSION UINT32_C(0x00010000)
/* Single owner: the test server thread. Callback transfers exactly one CS frame. */
typedef int (*spi4_transfer_fn)(void *context, const uint8_t tx[8], uint8_t rx[8]);
bool spi4_number(const char *s, uint32_t max, uint32_t *out);
bool spi4_exchange(spi4_transfer_fn transfer, void *ctx, uint8_t op,
                   uint16_t addr, uint32_t value, uint32_t *out, char *reply);
void spi4_encode(uint8_t tx[8], uint8_t op, uint16_t addr, uint32_t value);
/* Mutable, NUL-terminated ASCII line, excluding newline; reply includes newline. */
void spi4_command(char *line, char reply[SPI4_REPLY_SIZE],
                  spi4_transfer_fn transfer, void *context);
#endif
