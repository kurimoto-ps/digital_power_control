/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <string.h>
#include "spi4_protocol.h"
#include "spi4_test_server.h"

BUILD_ASSERT(IS_ENABLED(CONFIG_NET_CONTEXT_RCVTIMEO) &&
             IS_ENABLED(CONFIG_NET_CONTEXT_SNDTIMEO), "TCP client timeouts must be enabled");
#define FPGA DT_NODELABEL(max10_spi4)
BUILD_ASSERT(DT_PROP(FPGA, spi_max_frequency) <= 2000000, "MAX10 v1 max 2 MHz");
BUILD_ASSERT(DT_PROP(FPGA, spi_cs_setup_delay_ns) >= 1000, "CS setup >= 1 us");
BUILD_ASSERT(DT_PROP(FPGA, spi_cs_hold_delay_ns) >= 1000, "CS hold >= 1 us");
static const struct spi_dt_spec fpga = SPI_DT_SPEC_GET(
    FPGA, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);
static const struct gpio_dt_spec confdone =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), confdone_gpios);
static K_THREAD_STACK_DEFINE(server_stack, 4096);
static struct k_thread server_thread;

/* Only server_thread accesses SPI4; one call is one 8-byte CS-low frame. */
static int transfer(void *unused, const uint8_t tx[8], uint8_t rx[8])
{
    ARG_UNUSED(unused);
    int ready = gpio_pin_get_dt(&confdone);
    if (ready <= 0) { return ready < 0 ? ready : -EAGAIN; }
    struct spi_buf tb = {.buf = (void *)tx, .len = 8};
    struct spi_buf rb = {.buf = rx, .len = 8};
    const struct spi_buf_set ts = {.buffers = &tb, .count = 1};
    const struct spi_buf_set rs = {.buffers = &rb, .count = 1};
    k_busy_wait(1); /* NSS high between frames. */
    return spi_transceive_dt(&fpga, &ts, &rs);
}

static bool send_reply(int fd, const char *reply)
{
    size_t size = strlen(reply);
    while (size) {
        ssize_t n = zsock_send(fd, reply, size, 0);
        if (n < 0 && errno == EINTR) { continue; }
        if (n <= 0) { return false; }
        reply += n; size -= n;
    }
    return true;
}

static void serve_client(int fd)
{
    struct zsock_timeval timeout = {.tv_sec = 5};
    if (zsock_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        zsock_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) {
        printk("SPI4 client timeout setup failed: errno=%d\n", errno);
        return;
    }
    char line[SPI4_LINE_SIZE], reply[SPI4_REPLY_SIZE];
    size_t used = 0;
    int64_t deadline = k_uptime_get() + 5000;
    for (;;) {
        unsigned char c;
        ssize_t n = zsock_recv(fd, &c, 1, 0);
        if (n < 0 && errno == EINTR) { continue; }
        if (n <= 0) { return; }
        /* Bound even a slow client that sends a byte just before every timeout. */
        if (k_uptime_get() > deadline) { send_reply(fd, "ERR TIMEOUT\n"); return; }
        if (c == '\n') {
            line[used] = '\0';
            int done = gpio_pin_get_dt(&confdone);
            if (done <= 0) {
                strcpy(reply, "ERR NOT_READY\n");
            } else {
                /* Conservative 10 ms settling on every command, including after reconfiguration. */
                k_msleep(10);
                spi4_command(line, reply, transfer, NULL);
            }
            if (!send_reply(fd, reply)) { return; }
            used = 0;
            deadline = k_uptime_get() + 5000;
        } else if ((c < 32 && c != '\r' && c != '\t') || c > 126 ||
                   used == sizeof(line) - 1) {
            send_reply(fd, "ERR COMMAND\n");
            return; /* Never execute a truncated line or an embedded-NUL suffix. */
        } else {
            line[used++] = c;
        }
    }
}

static void server(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(CONFIG_JPARC_SPI4_TEST_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };
    for (;;) {
        int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int yes = 1;
        if (fd >= 0 &&
            zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == 0 &&
            zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
            zsock_listen(fd, 1) == 0) {
            printk("SPI4 test listening on %s:%d (GPIO CS, requested %u Hz)\n",
                   CONFIG_JPARC_SPI4_TEST_IP, CONFIG_JPARC_SPI4_TEST_PORT,
                   fpga.config.frequency);
            for (;;) {
                int client = zsock_accept(fd, NULL, NULL);
                if (client < 0) { if (errno == EINTR) { continue; } break; }
                serve_client(client);
                zsock_close(client);
            }
        }
        printk("SPI4 TCP listener restarting: errno=%d\n", errno);
        if (fd >= 0) { zsock_close(fd); }
        k_sleep(K_SECONDS(1));
    }
}

int spi4_test_server_start(void)
{
    if (!spi_is_ready_dt(&fpga) || !gpio_is_ready_dt(&confdone)) { return -ENODEV; }
    int rc = gpio_pin_configure_dt(&confdone, GPIO_INPUT);
    if (rc) { return rc; }
    struct net_if *iface = net_if_get_default();
    struct net_in_addr addr, mask;
    if (!iface || net_addr_pton(AF_INET, CONFIG_JPARC_SPI4_TEST_IP, &addr) ||
        net_addr_pton(AF_INET, "255.255.255.0", &mask)) { return -EINVAL; }
    if (!net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0) ||
        !net_if_ipv4_set_netmask_by_addr(iface, &addr, &mask)) { return -ENOMEM; }
    rc = net_if_up(iface);
    if (rc && rc != -EALREADY) { return rc; }
    k_thread_create(&server_thread, server_stack, K_THREAD_STACK_SIZEOF(server_stack),
                    server, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
    return 0;
}
