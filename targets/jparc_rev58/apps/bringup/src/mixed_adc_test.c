/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/atomic.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_spi.h>
#include <stdio.h>
#include <string.h>
#include "mixed_adc_test.h"
#include "mixed_stream.h"

#define RX DT_NODELABEL(spi1)
#define RING_SIZE 30720u
#define HALF_SIZE (RING_SIZE / 2u)
BUILD_ASSERT(HALF_SIZE % 48 == 0 && HALF_SIZE % 32 == 0, "DMA frame alignment");
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
static struct mixed_stats stats;
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
    mixed_stream_feed(&stats, scratch, sizeof(scratch));
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
    mixed_stream_feed(&stats, &adc_dma_ring[begin], pos - begin);
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

/* APPLY always submits a new atomic snapshot. Readback of CONTROL is not ACK. */
static bool apply_enable(struct control *c, uint32_t enable)
{
    uint32_t status, before, submitted, applied, value, expected[5];
    if (!read_reg(c, 0x0518, &status) || (status & 1) ||
        !read_reg(c, 0x0530, &before)) { return false; }
    for (unsigned i = 0; i < 5; ++i) {
        if (!read_reg(c, 0x0504 + 4 * i, &expected[i])) { return false; }
    }
    if (!write_reg(c, 0x0500, 2 | enable, enable) ||
        !read_reg(c, 0x0530, &submitted) || submitted != before + UINT32_C(1)) { return false; }
    int64_t deadline = k_uptime_get() + 100;
    do {
        if (!read_reg(c, 0x0518, &status) || (status & 2)) { return false; }
        if (!(status & 1)) {
            if (!read_reg(c, 0x051c, &applied) || applied != submitted ||
                !read_reg(c, 0x0554, &value) || value != enable) { return false; }
            for (unsigned i = 0; i < 5; ++i) {
                if (!read_reg(c, 0x0540 + 4 * i, &value) || value != expected[i]) { return false; }
            }
            return true;
        }
        k_usleep(50);
    } while (k_uptime_get() < deadline);
    return false;
}

bool mixed_adc_command(char *line, char reply[SPI4_REPLY_SIZE],
                       spi4_transfer_fn transfer, void *context)
{
    if (strncmp(line, "MIXCAP", 6)) { return false; }
    char t[9][24]; uint32_t args[7];
    int n = sscanf(line, "%23s %23s %23s %23s %23s %23s %23s %23s %23s",
                   t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8]);
    if (n != 8 || strcmp(t[0], "MIXCAP")) { goto syntax; }
    const uint32_t limits[] = {3000, 1, 7, 65535, 0xffffff, 0xffffff, 0xffffff};
    for (unsigned i = 0; i < 7; ++i) {
        if (!spi4_number(t[i + 1], limits[i], &args[i])) { goto syntax; }
    }
    if (args[0] < 100) { goto syntax; }
    struct control ctrl = {.transfer = transfer, .context = context};
    uint32_t id, version, caps, count, mask, negate, period, status, enabled;
    if (!read_reg(&ctrl, 0, &id) || !read_reg(&ctrl, 4, &version) ||
        !read_reg(&ctrl, 8, &caps) || !read_reg(&ctrl, 0x040c, &count) ||
        id != SPI4_DEVICE_ID || !((version == 0x00020000 && caps == 0x3f) ||
          (version == 0x00020001 && caps == 0x7f)) || count != 3 ||
        !read_reg(&ctrl, 0x0560, &count) || count != 8) {
        strcpy(reply, "ERR MIX_REQUIRES_V2_CAPS_3F_CHANNELS_3_8\n"); return true;
    }
    if (!read_reg(&ctrl, 0x0060, &mask) || mask || !read_reg(&ctrl, 0x0510, &negate)) {
        strcpy(reply, "ERR MIX_REQUIRES_MANUAL_DAC_MASK_ZERO\n"); return true;
    }
    if (!read_reg(&ctrl, 0x0554, &enabled) || enabled ||
        !read_reg(&ctrl, 0x0518, &status) || (status & 3) ||
        !read_reg(&ctrl, 0x0514, &period) || !period) {
        strcpy(reply, "ERR MIX_CONFIGURE_STOPPED_FIRST\n"); return true;
    }
    mixed_stream_init(&stats, args[1], args[2], args[3], negate, args + 4);
    consumed = overrun = spi_errors = 0;
    atomic_clear(&completed); atomic_clear(&dma_errors);
    bool armed = false, ok = false;
    int rc = 0, cleanup = 0;
    uint32_t tx = 0, diagnostics[3] = {0}, restarts[3] = {0}, replaced[3] = {0};
    int64_t start = 0, elapsed = 0;
    if (!write_reg(&ctrl, 0x0400, 0, 0) || !wait_idle(&ctrl)) { goto finish; }
    for (unsigned i = 0; i < 3; ++i) {
        if (!write_reg(&ctrl, 0x0484 + 4 * i, args[4 + i], args[4 + i])) { goto finish; }
    }
    if (!write_reg(&ctrl, 0x0480, args[1], args[1]) || !apply_enable(&ctrl, 1)) { goto finish; }
    rc = rx_start(); if (rc) { goto finish; } armed = true;
    start = k_uptime_get();
    if (!write_reg(&ctrl, 0x0400, 3, 1)) { goto finish; }
    while (k_uptime_get() - start < args[0]) {
        rc = consume_blocks(); if (rc) { goto finish; }
        k_sem_take(&rx_event, K_MSEC(1));
    }
    ok = true;
finish:
    /* Stop acquisition first, then drain the TX queue before stopping SPI1.
     * Cleanup runs inside the MCU even when the host disconnects. */
    if (!apply_enable(&ctrl, 0)) { cleanup = -EIO; }
    if (armed) {
        int64_t deadline = k_uptime_get() + 20;
        uint32_t status = 0;
        do {
            int drain_rc = consume_blocks(); if (drain_rc && !rc) { rc = drain_rc; }
            if (!read_reg(&ctrl, 0x0404, &status)) { cleanup = -EIO; break; }
            if ((status & BIT(8)) && !(status & BIT(16))) { break; }
            k_usleep(50);
        } while (k_uptime_get() < deadline);
        if (!(status & BIT(8)) || (status & BIT(16))) { cleanup = -ETIMEDOUT; }
    }
    if (!write_reg(&ctrl, 0x0400, 0, 0) || !wait_idle(&ctrl)) { cleanup = -EIO; }
    elapsed = start ? k_uptime_get() - start : 0;
    if (armed) { int end_rc = rx_finish(); if (end_rc && !rc) { rc = end_rc; } }
    const uint16_t diagnostic_regs[] = {0x0524, 0x0528, 0x0538};
    for (unsigned i = 0; i < 3; ++i) {
        if (!read_reg(&ctrl, diagnostic_regs[i], &diagnostics[i])) { ok = false; }
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!read_reg(&ctrl, 0x0430 + 4 * i, &restarts[i]) ||
            !read_reg(&ctrl, 0x0420 + 4 * i, &replaced[i])) { ok = false; }
    }
    if (!read_reg(&ctrl, 0x0408, &tx)) { ok = false; }
    /* Leave dummy mode explicitly; this clears flow counters after their snapshot. */
    if (!write_reg(&ctrl, 0x0480, 0, 0)) { cleanup = -EIO; }
    ok = ok && !rc && !cleanup && !ctrl.error[0] && stats.frames && stats.frames == tx &&
        !stats.crc_errors && !stats.discarded && !stats.used && !stats.sequence_errors &&
        !stats.flag_errors && !stats.value_errors && !overrun && !spi_errors &&
        !atomic_get(&dma_errors) && !diagnostics[0] && !diagnostics[1] && !diagnostics[2];
    uint64_t nominal = (uint64_t)args[0] * 40000 / period;
    ok = ok && stats.frames >= nominal * 9 / 10 && stats.frames <= nominal * 11 / 10 + 4;
    for (unsigned i = 0; i < 3; ++i) {
        if ((stats.required & BIT(i)) && (!stats.known[i] || stats.stale[i] || restarts[i])) { ok = false; }
    }
    int used = snprintf(reply, SPI4_REPLY_SIZE,
        "OK MIX {\"pass\":%s,\"frames\":%u,\"fpga_tx\":%u,\"elapsed_ms\":%lld,"
        "\"crc_errors\":%u,\"discarded\":%u,\"trailing\":%u,\"sequence_errors\":%u,"
        "\"flag_errors\":%u,\"value_errors\":%u,\"dma_errors\":%ld,\"overruns\":%u,"
        "\"spi_errors\":%u,\"error\":%d,\"cleanup_error\":%d,\"control_error\":%s,"
        "\"source_drop\":%u,\"missed\":%u,\"frame_drop\":%u,\"sequence\":%u,\"flags\":%u,\"adc24\":[",
        ok ? "true" : "false", stats.frames, tx, (long long)elapsed, stats.crc_errors,
        stats.discarded, (unsigned)stats.used, stats.sequence_errors, stats.flag_errors,
        stats.value_errors, (long)atomic_get(&dma_errors), overrun, spi_errors, rc, cleanup,
        ctrl.error[0] ? "true" : "false", diagnostics[0], diagnostics[1], diagnostics[2],
        stats.last.sequence, stats.last.flags);
    for (unsigned i = 0; i < 3; ++i) {
        used += snprintf(reply + used, SPI4_REPLY_SIZE - used,
            "%s{\"raw\":%u,\"seq\":%u,\"age_us\":%u,\"known\":%u,\"fresh\":%u,\"stale\":%u,\"max_age_us\":%u,\"restart\":%u,\"replaced\":%u}",
            i ? "," : "", stats.last.adc24_raw[i], stats.last.adc24_sequence[i],
            stats.last.adc24_age_us[i], stats.known[i], stats.fresh[i], stats.stale[i], stats.max_age[i], restarts[i], replaced[i]);
    }
    used += snprintf(reply + used, SPI4_REPLY_SIZE - used, "],\"adc16\":[");
    for (unsigned i = 0; i < 8; ++i) {
        used += snprintf(reply + used, SPI4_REPLY_SIZE - used,
            "%s{\"raw\":%u,\"min\":%d,\"max\":%d}", i ? "," : "",
            stats.last.adc16[i], stats.minimum[i], stats.maximum[i]);
    }
    snprintf(reply + used, SPI4_REPLY_SIZE - used, "]}\n");
    return true;
syntax:
    strcpy(reply, "ERR COMMAND MIXCAP ms(100..3000) dummy(0|1) required24_mask max_age_us raw1 raw2 raw3\n");
    return true;
}
