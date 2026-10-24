/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/atomic.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_spi.h>
#include <stdio.h>
#include <string.h>
#include "adc_dummy_test.h"
#include "adc_stream.h"

#define RX DT_NODELABEL(spi1)
#define RING_SIZE 32768u
#define HALF_SIZE (RING_SIZE / 2u)
#define SPI_ERRORS (SPI_SR_OVR | SPI_SR_MODF | SPI_SR_TIFRE)
BUILD_ASSERT(!IS_ENABLED(CONFIG_DCACHE), "DMA buffers require D-cache disabled");
BUILD_ASSERT(!IS_ENABLED(CONFIG_SPI_STM32_DMA), "SPI1 DMA is exclusively application-owned");
BUILD_ASSERT(DT_DMAS_CELL_BY_NAME(RX, rx, channel) == 0, "Reserve DMA1 stream 0");
BUILD_ASSERT(DT_DMAS_CELL_BY_NAME(RX, rx, slot) == 37, "SPI1_RX DMAMUX request");
/* AXI SRAM, never DTCM; verified in the build artifact checker. */
static uint8_t adc_dma_ring[RING_SIZE] __aligned(32);
static uint8_t scratch[HALF_SIZE] __aligned(32);
static const struct device *const mux = DEVICE_DT_GET(DT_DMAS_CTLR_BY_NAME(RX, rx));
static atomic_t completed, dma_errors;
static K_SEM_DEFINE(rx_event, 0, 1);
static struct adc_stream_stats stats;
static uint32_t consumed, overrun, spi_errors;

static void dma_done(const struct device *dev, void *user, uint32_t channel, int status)
{
    ARG_UNUSED(dev); ARG_UNUSED(user); ARG_UNUSED(channel);
    if (status < 0) {
        atomic_inc(&dma_errors);
    } else {
        /* STM32 circular mode signals half (BLOCK), then full (COMPLETE). */
        unsigned int expected = (atomic_get(&completed) & 1) ?
                                DMA_STATUS_COMPLETE : DMA_STATUS_BLOCK;
        if ((unsigned int)status != expected) { atomic_inc(&dma_errors); }
        atomic_inc(&completed);
    }
    k_sem_give(&rx_event);
}

static int rx_start(void)
{
    if (!device_is_ready(DEVICE_DT_GET(RX)) || !device_is_ready(mux)) { return -ENODEV; }
    consumed = overrun = spi_errors = 0;
    atomic_clear(&completed);
    atomic_clear(&dma_errors);
    k_sem_reset(&rx_event);
    /* No SPI API caller may access SPI1 while this module owns it. */
    LL_SPI_Disable(SPI1);
    LL_APB2_GRP1_ForceReset(LL_APB2_GRP1_PERIPH_SPI1);
    LL_APB2_GRP1_ReleaseReset(LL_APB2_GRP1_PERIPH_SPI1);
    LL_SPI_SetMode(SPI1, LL_SPI_MODE_SLAVE);
    LL_SPI_SetTransferDirection(SPI1, LL_SPI_SIMPLEX_RX);
    LL_SPI_SetDataWidth(SPI1, LL_SPI_DATAWIDTH_8BIT);
    LL_SPI_SetFIFOThreshold(SPI1, LL_SPI_FIFO_TH_01DATA);
    LL_SPI_SetClockPolarity(SPI1, LL_SPI_POLARITY_LOW);
    LL_SPI_SetClockPhase(SPI1, LL_SPI_PHASE_1EDGE);
    LL_SPI_SetTransferBitOrder(SPI1, LL_SPI_MSB_FIRST);
    LL_SPI_SetNSSMode(SPI1, LL_SPI_NSS_HARD_INPUT);
    LL_SPI_SetTransferSize(SPI1, 0); /* Unlimited RX across NSS boundaries. */
    SPI1->IFCR = 0xffffffff;
    struct dma_block_config block = {
        .source_address = (uintptr_t)&SPI1->RXDR,
        .dest_address = (uintptr_t)adc_dma_ring,
        .block_size = sizeof(adc_dma_ring),
        .source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE,
        .dest_addr_adj = DMA_ADDR_ADJ_INCREMENT,
        .source_reload_en = 1, .dest_reload_en = 1,
    };
    struct dma_config cfg = {
        .dma_slot = DT_DMAS_CELL_BY_NAME(RX, rx, slot),
        .channel_direction = PERIPHERAL_TO_MEMORY,
        .source_data_size = 1, .dest_data_size = 1,
        .source_burst_length = 1, .dest_burst_length = 1,
        .channel_priority = 3, .block_count = 1, .head_block = &block,
        .dma_callback = dma_done,
    };
    int rc = dma_config(mux, 0, &cfg);
    if (!rc) { rc = dma_start(mux, 0); }
    if (rc) { dma_stop(mux, 0); return rc; }
    LL_SPI_EnableDMAReq_RX(SPI1);
    LL_SPI_Enable(SPI1);
    return 0;
}

static int consume_blocks(void)
{
    spi_errors |= SPI1->SR & SPI_ERRORS;
    if (spi_errors || atomic_get(&dma_errors)) { return -EIO; }
    uint32_t produced = atomic_get(&completed);
    if (produced - consumed > 1) { ++overrun; return -EOVERFLOW; }
    if (produced == consumed) { return 0; }
    memcpy(scratch, &adc_dma_ring[(consumed & 1u) * HALF_SIZE], HALF_SIZE);
    __DMB();
    /* The DMA must not have returned to this half while it was being copied. */
    if ((uint32_t)atomic_get(&completed) != produced) { ++overrun; return -EOVERFLOW; }
    ++consumed;
    adc_stream_feed(&stats, scratch, sizeof(scratch));
    return 0;
}

static int rx_finish(void)
{
    /* FPGA has stopped and BUSY is clear; allow its final bytes to reach RAM. */
    k_busy_wait(50);
    k_sleep(K_MSEC(1));
    spi_errors |= SPI1->SR & SPI_ERRORS;
    LL_SPI_DisableDMAReq_RX(SPI1);
    struct dma_status status = {0};
    int rc = dma_get_status(mux, 0, &status);
    int stopped = dma_stop(mux, 0);
    LL_SPI_Disable(SPI1);
    if (rc || stopped) { return rc ? rc : stopped; }
    rc = consume_blocks();
    if (rc) { return rc; }
    uint32_t pos = (RING_SIZE - status.pending_length) % RING_SIZE;
    uint32_t begin = (consumed & 1u) * HALF_SIZE;
    if (pos < begin || pos - begin >= HALF_SIZE) { ++overrun; return -EOVERFLOW; }
    adc_stream_feed(&stats, &adc_dma_ring[begin], pos - begin);
    return 0;
}

struct control {
    spi4_transfer_fn transfer;
    void *context;
    char error[SPI4_REPLY_SIZE];
};
static bool access(struct control *c, uint8_t op, uint16_t addr, uint32_t data, uint32_t *out)
{
    return spi4_exchange(c->transfer, c->context, op, addr, data, out, c->error);
}
static bool read_reg(struct control *c, uint16_t addr, uint32_t *value)
{
    return access(c, 3, addr, 0, value);
}
static bool write_reg(struct control *c, uint16_t addr, uint32_t value, uint32_t expected)
{
    uint32_t actual;
    if (!access(c, 2, addr, value, &actual) || !read_reg(c, addr, &actual)) { return false; }
    if (actual != expected) {
        snprintf(c->error, sizeof(c->error), "ERR ADC_CONTROL_VERIFY 0x%04x\n", addr);
        return false;
    }
    return true;
}
static bool wait_idle(struct control *c)
{
    int64_t deadline = k_uptime_get() + 20;
    do {
        uint32_t value;
        if (!read_reg(c, 0x0404, &value)) { return false; }
        if (!(value & BIT(16))) { return true; }
        k_usleep(50);
    } while (k_uptime_get() < deadline);
    strcpy(c->error, "ERR ADC_BUSY_TIMEOUT\n");
    return false;
}

bool adc_dummy_command(char *line, char reply[SPI4_REPLY_SIZE],
                       spi4_transfer_fn transfer, void *context)
{
    if (strncmp(line, "ADCTEST", 7) != 0) { return false; }
    char token[6][24];
    uint32_t ms, values[3];
    int n = sscanf(line, "%23s %23s %23s %23s %23s %23s",
                   token[0], token[1], token[2], token[3], token[4], token[5]);
    if (n != 5 || strcmp(token[0], "ADCTEST") ||
        !spi4_number(token[1], 3000, &ms) || ms < 100 ||
        !spi4_number(token[2], 0xffffff, &values[0]) ||
        !spi4_number(token[3], 0xffffff, &values[1]) ||
        !spi4_number(token[4], 0xffffff, &values[2])) {
        strcpy(reply, "ERR COMMAND ADCTEST milliseconds(100..3000) raw1 raw2 raw3\n");
        return true;
    }
    struct control ctrl = {.transfer = transfer, .context = context};
    uint32_t id, version, caps, channels;
    if (!read_reg(&ctrl, 0, &id) || !read_reg(&ctrl, 4, &version) ||
        !read_reg(&ctrl, 8, &caps) || !read_reg(&ctrl, 0x040c, &channels)) {
        strcpy(reply, ctrl.error); return true;
    }
    if (id != SPI4_DEVICE_ID || version != 0x00010003 || caps != 0x0f || channels != 3) {
        strcpy(reply, "ERR ADC_REQUIRES_V1_3_CAPS_0F_CHANNELS_3\n"); return true;
    }
    adc_stream_init(&stats, values);
    consumed = overrun = spi_errors = 0;
    atomic_clear(&completed);
    atomic_clear(&dma_errors);
    bool armed = false, ok = false;
    int rc = 0, cleanup_error = 0;
    uint32_t tx_count = 0, drops[3] = {0}, restarts[3] = {0};
    int64_t start = 0, elapsed = 0;
    if (!write_reg(&ctrl, 0x0400, 0, 0) || !wait_idle(&ctrl)) { goto cleanup; }
    for (unsigned int ch = 0; ch < 3; ++ch) {
        if (!write_reg(&ctrl, 0x0484 + 4 * ch, values[ch], values[ch])) { goto cleanup; }
    }
    if (!write_reg(&ctrl, 0x0480, 1, 1)) { goto cleanup; }
    rc = rx_start();
    if (rc) { goto cleanup; }
    armed = true;
    start = k_uptime_get();
    /* Clear/start only after DMA is enabled; clear bit reads back as zero. */
    if (!write_reg(&ctrl, 0x0400, 3, 1)) { goto cleanup; }
    while (k_uptime_get() - start < ms) {
        rc = consume_blocks();
        if (rc) { goto cleanup; }
        k_sem_take(&rx_event, K_MSEC(1));
    }
    /* The source keeps generating after TX is stopped. Reading DROP after stop
     * would measure our deliberate FIFO accumulation, not streaming loss.
     * Capture diagnostics while TX still runs, draining DMA between reads.
     */
    for (unsigned int ch = 0; ch < 3; ++ch) {
        if (!read_reg(&ctrl, 0x0420 + 4 * ch, &drops[ch]) ||
            !read_reg(&ctrl, 0x0430 + 4 * ch, &restarts[ch])) { goto cleanup; }
        rc = consume_blocks();
        if (rc) { goto cleanup; }
    }
    ok = true;
cleanup:
    /* Always stop even if the client disconnected or the test failed. */
    {
        char first_error[SPI4_REPLY_SIZE];
        strcpy(first_error, ctrl.error);
        if (!write_reg(&ctrl, 0x0400, 0, 0) || !wait_idle(&ctrl)) {
            cleanup_error = -EIO; ok = false;
        }
        elapsed = start ? k_uptime_get() - start : 0;
        if (armed) {
            int end_rc = rx_finish();
            if (end_rc && !rc) { rc = end_rc; }
        }
        /* TX count is stable after STOP/BUSY; compare every transmitted record. */
        if (!read_reg(&ctrl, 0x0408, &tx_count)) { ok = false; }
        if (!write_reg(&ctrl, 0x0480, 0, 0)) { cleanup_error = -EIO; ok = false; }
        if (first_error[0]) { strcpy(ctrl.error, first_error); }
    }
    uint32_t received = 0;
    ok = ok && !rc && !cleanup_error && !stats.crc_errors && !stats.header_errors &&
         !stats.discarded_bytes && !stats.pending_size && !overrun &&
         !spi_errors && !atomic_get(&dma_errors);
    for (unsigned int ch = 0; ch < 3; ++ch) {
        struct adc_channel_stats *c = &stats.channel[ch];
        received += c->records;
        /* 10% envelope includes scheduling and SPI4 start/stop latency. */
        uint64_t nominal = (uint64_t)ms * 78125 / 1000;
        ok = ok && c->records >= nominal * 9 / 10 && c->records <= nominal * 11 / 10 &&
             !c->value_errors && !c->sequence_errors && !drops[ch] && !restarts[ch];
    }
    ok = ok && received == tx_count;
    int used = snprintf(reply, SPI4_REPLY_SIZE,
        "OK ADC {\"pass\":%s,\"requested_ms\":%u,\"elapsed_ms\":%lld,"
        "\"received\":%u,\"fpga_tx\":%u,\"crc_errors\":%u,\"header_errors\":%u,"
        "\"discarded_bytes\":%u,\"trailing_bytes\":%u,\"dma_errors\":%ld,"
        "\"dma_overruns\":%u,\"spi_error_flags\":%u,\"error\":%d,"
        "\"control_error\":%s,\"cleanup_error\":%d,\"channels\":[",
        ok ? "true" : "false", ms, (long long)elapsed, received, tx_count,
        stats.crc_errors, stats.header_errors, stats.discarded_bytes,
        (unsigned int)stats.pending_size, (long)atomic_get(&dma_errors),
        overrun, spi_errors, rc, ctrl.error[0] ? "true" : "false", cleanup_error);
    for (unsigned int ch = 0; ch < 3; ++ch) {
        struct adc_channel_stats *c = &stats.channel[ch];
        used += snprintf(reply + used, SPI4_REPLY_SIZE - used,
            "%s{\"channel\":%u,\"records\":%u,\"expected\":%u,\"last\":%u,"
            "\"first_seq\":%u,\"last_seq\":%u,\"value_errors\":%u,"
            "\"sequence_errors\":%u,\"fpga_drop\":%u,\"fpga_restart\":%u}",
            ch ? "," : "", ch + 1, c->records, values[ch], c->last_value,
            c->first_sequence, c->last_sequence, c->value_errors,
            c->sequence_errors, drops[ch], restarts[ch]);
    }
    snprintf(reply + used, SPI4_REPLY_SIZE - used, "]}\n");
    return true;
}
