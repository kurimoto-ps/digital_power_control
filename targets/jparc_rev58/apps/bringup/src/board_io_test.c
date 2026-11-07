/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <stdio.h>
#include <string.h>
#include "board_io_protocol.h"
#include "board_io_test.h"
#define USER DT_PATH(zephyr_user)
#define SPEC(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),
static const struct gpio_dt_spec inputs[] = { DT_FOREACH_PROP_ELEM(USER, plc_input_gpios, SPEC) };
static const struct gpio_dt_spec outputs[] = { DT_FOREACH_PROP_ELEM(USER, plc_output_gpios, SPEC) };
BUILD_ASSERT(ARRAY_SIZE(inputs) == 16 && ARRAY_SIZE(outputs) == 16, "PLC mapping size");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(uart8), tx_rx_swap), "Motherboard requires swapped UART8");
static const struct device *const serial = DEVICE_DT_GET(DT_NODELABEL(uart8));
RING_BUF_DECLARE(rx_ring, 1024);
static uint32_t rx_total, rx_dropped, error_flags, tx_total;
static uint16_t output_latch;
static const struct gpio_dt_spec acc_inputs[] = {
    GPIO_DT_SPEC_GET(USER, sync_gpios), GPIO_DT_SPEC_GET(USER, trigger_gpios)
};
int board_acc_read(void *unused, uint32_t *levels)
{
    ARG_UNUSED(unused);
    *levels = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(acc_inputs); ++i) {
        int level = gpio_pin_get_raw(acc_inputs[i].port, acc_inputs[i].pin);
        if (level < 0) { return level; }
        *levels |= (uint32_t)level << (i + 1); /* Same bit positions as FPGA. */
    }
    return 0;
}
static void serial_irq(const struct device *dev, void *unused)
{
    ARG_UNUSED(unused);
    uart_irq_update(dev);
    int errors = uart_err_check(dev);
    if (errors > 0) { error_flags |= errors; }
    while (uart_irq_rx_ready(dev)) {
        uint8_t data[32];
        int n = uart_fifo_read(dev, data, sizeof(data));
        if (n <= 0) { break; }
        rx_total += n;
        rx_dropped += n - ring_buf_put(&rx_ring, data, n);
    }
}
static int set_outputs(uint16_t mask)
{
    /* Clear old bits before setting new ones. This is not an atomic bus write. */
    for (unsigned value = 0; value <= 1; ++value) {
        for (unsigned i = 0; i < 16; ++i) {
            if (((mask >> i) & 1u) != value) { continue; }
            int rc = gpio_pin_set_raw(outputs[i].port, outputs[i].pin, value);
            if (rc) {
                /* Best effort all-low after a partial write; never claim success. */
                for (unsigned j = 0; j < 16; ++j) {
                    gpio_pin_set_raw(outputs[j].port, outputs[j].pin, 0);
                }
                output_latch = 0;
                return rc;
            }
        }
    }
    output_latch = mask;
    return 0;
}
int board_io_init(void)
{
    /* Shared with MAX10 and external drivers: inputs only, no pulls/EXTI. */
    for (unsigned i = 0; i < ARRAY_SIZE(acc_inputs); ++i) {
        if (!gpio_is_ready_dt(&acc_inputs[i])) { return -ENODEV; }
        int rc = gpio_pin_configure_dt(&acc_inputs[i], GPIO_INPUT);
        if (rc) { return rc; }
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (!gpio_is_ready_dt(&outputs[i]) || !gpio_is_ready_dt(&inputs[i])) { return -ENODEV; }
        int rc = gpio_pin_configure_dt(&outputs[i], GPIO_OUTPUT_LOW);
        if (rc) { return rc; }
        rc = gpio_pin_configure_dt(&inputs[i], GPIO_INPUT);
        if (rc) { return rc; }
    }
    if (!device_is_ready(serial)) { return -ENODEV; }
    int rc = uart_irq_callback_user_data_set(serial, serial_irq, NULL);
    if (rc) { return rc; }
    uart_irq_rx_enable(serial);
    uart_irq_err_enable(serial);
    return 0;
}
bool board_io_command(const char *line, char reply[SPI4_REPLY_SIZE])
{
    struct board_io_request r = board_io_parse(line);
    if (r.kind == IO_UNKNOWN) { return false; }
    if (r.kind == IO_INVALID) { strcpy(reply, "ERR BOARD_IO_COMMAND\n"); return true; }
    if (r.kind == PLC_WRITE) {
        int rc = set_outputs(r.mask);
        if (rc) { snprintf(reply, SPI4_REPLY_SIZE, "ERR PLC_WRITE %d\n", rc); return true; }
    }
    if (r.kind == PLC_WRITE || r.kind == PLC_READ) {
        uint16_t in = 0, out = 0;
        for (unsigned i = 0; i < 16; ++i) {
            int a = gpio_pin_get_raw(inputs[i].port, inputs[i].pin);
            int b = gpio_pin_get_raw(outputs[i].port, outputs[i].pin);
            if (a < 0 || b < 0) { strcpy(reply, "ERR PLC_READ\n"); return true; }
            in |= a << i; out |= b << i;
        }
        snprintf(reply, SPI4_REPLY_SIZE,
            "OK PLC {\"input_raw\":%u,\"output_raw\":%u,\"output_latch\":%u}\n",
            in, out, output_latch);
        return true;
    }
    if (r.kind == UART_SEND) {
        for (size_t i = 0; i < r.size; ++i) { uart_poll_out(serial, r.data[i]); }
        tx_total += r.size;
    }
    uint8_t data[128]; uint32_t n = 0;
    unsigned key = irq_lock();
    if (r.kind == UART_CLEAR) {
        ring_buf_reset(&rx_ring); rx_total = rx_dropped = error_flags = tx_total = 0;
    }
    if (r.kind == UART_READ) { n = ring_buf_get(&rx_ring, data, sizeof(data)); }
    uint32_t total = rx_total, dropped = rx_dropped, errors = error_flags;
    uint32_t buffered = ring_buf_size_get(&rx_ring), sent = tx_total;
    irq_unlock(key);
    char hex[257];
    for (uint32_t i = 0; i < n; ++i) { snprintf(hex + 2*i, 3, "%02x", data[i]); }
    hex[2*n] = '\0';
    snprintf(reply, SPI4_REPLY_SIZE,
        "OK UART {\"baud\":115200,\"rx_total\":%u,\"rx_dropped\":%u,"
        "\"error_flags\":%u,\"buffered\":%u,\"tx_total\":%u,\"hex\":\"%s\"}\n",
        total, dropped, errors, buffered, sent, hex);
    return true;
}
