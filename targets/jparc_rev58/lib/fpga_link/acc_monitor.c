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
    if (id != SPI4_DEVICE_ID || (version != 0x00020001 && version != 0x00020002) || !(caps & MIX_CAP_ACC_INPUT_LEVELS)) {
        strcpy(reply, "ERR ACC_REQUIRES_V2_0_1_OR_V2_0_2_CAP_BIT6\n"); return true;
    }
    if (read_gpio(gpio_context, &before)) { strcpy(reply, "ERR ACC_GPIO\n"); return true; }
    if (!spi4_exchange(transfer, context, 3, MIX_REG_ACC_INPUT_LEVELS, 0, &levels, reply)) { return true; }
    if (read_gpio(gpio_context, &after)) { strcpy(reply, "ERR ACC_GPIO\n"); return true; }
    if (levels & ~7u) { strcpy(reply, "ERR ACC_RESERVED_BITS\n"); return true; }
    if ((before | after) & ~6u) { strcpy(reply, "ERR ACC_GPIO_BITS\n"); return true; }
    /* Clock registers are independently sampled windows, not a paired snapshot. */
    char clock[180] = "\"clock_supported\":false,\"clock_status\":null,\"clock_count\":null";
    if (version == 0x00020002 && (caps & MIX_CAP_ACC_CLOCK_MONITOR)) {
        uint32_t status, count;
        if (!spi4_exchange(transfer, context, 3, MIX_REG_ACC_CLOCK_STATUS, 0, &status, reply) ||
            !spi4_exchange(transfer, context, 3, MIX_REG_ACC_12MHZ_COUNT, 0, &count, reply)) { return true; }
        if ((status & ~7u) || (count & ~0x1ffffu)) {
            strcpy(reply, "ERR ACC_CLOCK_RESERVED_BITS\n"); return true;
        }
        snprintf(clock, sizeof(clock), "\"clock_supported\":true,\"clock_status\":%u,\"clock_count\":%u", status, count);
    }
    /* GPIO/0x24 are instantaneous. Clock status/count are separate 1 ms windows. */
    snprintf(reply, SPI4_REPLY_SIZE,
             "OK ACC {\"version\":%u,\"capabilities\":%u,\"fpga_raw\":%u,"
             "\"stm32_before\":%u,\"stm32_after\":%u,\"stm32_mask\":6,%s}\n",
             version, caps, levels, before, after, clock);
    return true;
}
