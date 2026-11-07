/* SPDX-License-Identifier: Apache-2.0 */
#include "acc_monitor.h"
#include "mixed_adc_protocol.h"
#include <stdio.h>
#include <string.h>
bool acc_monitor_command(const char *line, char reply[SPI4_REPLY_SIZE],
                         spi4_transfer_fn transfer, void *context,
                         acc_gpio_read_fn read_gpio, void *gpio_context)
{
    char a[16], b[16], extra;
    int n = sscanf(line, "%15s %15s %c", a, b, &extra);
    if (n < 1 || strcmp(a, "ACC")) { return false; }
    if (n != 2 || strcmp(b, "READ")) { strcpy(reply, "ERR COMMAND ACC_READ\n"); return true; }
    uint32_t id, version, caps, levels, before, after;
    if (!spi4_exchange(transfer, context, 3, 0, 0, &id, reply) ||
        !spi4_exchange(transfer, context, 3, MIX_REG_VERSION, 0, &version, reply) ||
        !spi4_exchange(transfer, context, 3, MIX_REG_CAPABILITIES, 0, &caps, reply)) { return true; }
    /* Never probe the new register on old firmware or without feature bit6. */
    if (id != SPI4_DEVICE_ID || version != 0x00020001 || !(caps & MIX_CAP_ACC_INPUT_LEVELS)) {
        strcpy(reply, "ERR ACC_REQUIRES_V2_0_1_CAP_BIT6\n"); return true;
    }
    if (read_gpio(gpio_context, &before)) { strcpy(reply, "ERR ACC_GPIO\n"); return true; }
    if (!spi4_exchange(transfer, context, 3, MIX_REG_ACC_INPUT_LEVELS, 0, &levels, reply)) { return true; }
    if (read_gpio(gpio_context, &after)) { strcpy(reply, "ERR ACC_GPIO\n"); return true; }
    if (levels & ~7u) { strcpy(reply, "ERR ACC_RESERVED_BITS\n"); return true; }
    if ((before | after) & ~6u) { strcpy(reply, "ERR ACC_GPIO_BITS\n"); return true; }
    /* Sequential reads, not simultaneous samples, edge counters or frequency estimates. */
    snprintf(reply, SPI4_REPLY_SIZE,
             "OK ACC {\"version\":%u,\"capabilities\":%u,\"fpga_raw\":%u,"
             "\"stm32_before\":%u,\"stm32_after\":%u,\"stm32_mask\":6}\n",
             version, caps, levels, before, after);
    return true;
}
