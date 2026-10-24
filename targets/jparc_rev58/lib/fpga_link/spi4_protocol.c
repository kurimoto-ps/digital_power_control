/* SPDX-License-Identifier: Apache-2.0 */
#include "spi4_protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void spi4_encode(uint8_t tx[8], uint8_t op, uint16_t addr, uint32_t value)
{
    tx[0] = op; tx[1] = addr >> 8; tx[2] = addr; tx[3] = 0;
    for (unsigned int i = 0; i < 4; ++i) { tx[4 + i] = value >> (24 - 8 * i); }
}

bool spi4_number(const char *s, uint32_t max, uint32_t *out)
{
    unsigned int base = 10;
    uint32_t n = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    if (*s == '\0') { return false; }
    for (; *s; ++s) {
        unsigned int d = *s >= '0' && *s <= '9' ? *s - '0' :
                         *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
                         *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : 99;
        if (d >= base || d > max || n > (max - d) / base) { return false; }
        n = n * base + d;
    }
    *out = n;
    return true;
}

bool spi4_exchange(spi4_transfer_fn transfer, void *ctx, uint8_t op,
                  uint16_t addr, uint32_t value, uint32_t *out, char *reply)
{
    uint8_t tx[8], rx[8] = {0};
    spi4_encode(tx, op, addr, value);
    int rc = transfer(ctx, tx, rx);
    if (rc != 0) {
        snprintf(reply, SPI4_REPLY_SIZE, "ERR TRANSPORT %d\n", rc);
        return false;
    }
    if (rx[0] || rx[1] || rx[2]) {
        snprintf(reply, SPI4_REPLY_SIZE, "ERR FRAME\n");
        return false;
    }
    if (rx[3]) {
        snprintf(reply, SPI4_REPLY_SIZE, "ERR FPGA 0x%02x\n", rx[3]);
        return false;
    }
    *out = (uint32_t)rx[4] << 24 | (uint32_t)rx[5] << 16 |
           (uint32_t)rx[6] << 8 | rx[7];
    if (op == 2 && *out != 0) {
        snprintf(reply, SPI4_REPLY_SIZE, "ERR FRAME\n");
        return false;
    }
    return true;
}

void spi4_command(char *line, char reply[SPI4_REPLY_SIZE],
                  spi4_transfer_fn transfer, void *ctx)
{
    char *args[4];
    size_t count = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') { ++p; }
        if (!*p) { break; }
        if (count == 4) { break; }
        args[count++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r') { ++p; }
        if (*p) { *p++ = '\0'; }
    }
    bool info = count == 1 && strcmp(args[0], "INFO") == 0;
    bool read = count == 2 && strcmp(args[0], "READ") == 0;
    bool write = count == 3 && strcmp(args[0], "WRITE") == 0;
    uint32_t addr = 0, value = 0, actual;
    if ((!info && !read && !write) ||
        (!info && !spi4_number(args[1], UINT16_MAX, &addr)) ||
        (write && !spi4_number(args[2], UINT32_MAX, &value))) {
        snprintf(reply, SPI4_REPLY_SIZE, "ERR COMMAND\n");
        return;
    }
    /* Probe before any write: a floating/shorted MISO must never look like success. */
    uint32_t id, version;
    if (!spi4_exchange(transfer, ctx, 3, 0, 0, &id, reply) ||
        !spi4_exchange(transfer, ctx, 3, 4, 0, &version, reply)) { return; }
    if (id != SPI4_DEVICE_ID || (version < SPI4_VERSION || version > 0x00010003)) {
        snprintf(reply, SPI4_REPLY_SIZE, "ERR ID 0x%08lx 0x%08lx\n",
                 (unsigned long)id, (unsigned long)version);
        return;
    }
    if (info) {
        uint32_t caps, count_regs, writes;
        if (!spi4_exchange(transfer, ctx, 3, 8, 0, &caps, reply) ||
            !spi4_exchange(transfer, ctx, 3, 12, 0, &count_regs, reply) ||
            !spi4_exchange(transfer, ctx, 3, 16, 0, &writes, reply)) { return; }
        snprintf(reply, SPI4_REPLY_SIZE,
                 "OK INFO 0x%08lx 0x%08lx 0x%08lx %lu %lu\n",
                 (unsigned long)id, (unsigned long)version, (unsigned long)caps,
                 (unsigned long)count_regs, (unsigned long)writes);
    } else {
        if (write && !spi4_exchange(transfer, ctx, 2, addr, value, &actual, reply)) { return; }
        if (!spi4_exchange(transfer, ctx, 3, addr, 0, &actual, reply)) { return; }
        uint32_t expected = value;
        if (version >= 0x00010001 && addr >= 0x0200 && addr <= 0x023c) {
            expected &= 0xffff;
        }
        if (version >= 0x00010002 && addr == 0x0400) { expected &= 1; }
        if (version >= 0x00010003) {
            if (addr == 0x0480) { expected &= 1; }
            if (addr >= 0x0484 && addr <= 0x048c) { expected &= 0xffffff; }
        }
        if (write && actual != expected) {
            snprintf(reply, SPI4_REPLY_SIZE, "ERR VERIFY 0x%04lx 0x%08lx 0x%08lx\n",
                     (unsigned long)addr, (unsigned long)value, (unsigned long)actual);
            return;
        }
        snprintf(reply, SPI4_REPLY_SIZE, "OK %s 0x%04lx 0x%08lx\n",
                 write ? "WRITE" : "READ", (unsigned long)addr, (unsigned long)actual);
    }
}
