/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include "peripheral_checks.h"

#if DT_NODE_HAS_STATUS(DT_NODELABEL(sdram), okay)
BUILD_ASSERT(!IS_ENABLED(CONFIG_DCACHE), "SDRAM bring-up must access physical memory");
BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(sdram2)) == 0xd0000000, "BANK2 address");
BUILD_ASSERT(DT_REG_SIZE(DT_NODELABEL(sdram2)) == 0x00800000, "8 MiB capacity");
struct memory_test_status {
    unsigned int stage; /* 1=data bus, 2=address, 3=inverse/retention, 4=byte lanes, 5=pass */
    uintptr_t failed_address;
    uint32_t expected;
    uint32_t actual;
};
volatile struct memory_test_status rev58_memtest;

static int mismatch(uintptr_t address, uint32_t expected, uint32_t actual)
{
    rev58_memtest.failed_address = address;
    rev58_memtest.expected = expected;
    rev58_memtest.actual = actual;
    printk("SDRAM FAIL stage=%u address=0x%08lx expected=%08x actual=%08x\n",
           rev58_memtest.stage, (unsigned long)address, expected, actual);
    return -EIO;
}

static int check_sdram(void)
{
    if (!device_is_ready(DEVICE_DT_GET(DT_NODELABEL(sdram)))) {
        return -ENODEV;
    }
    volatile uint32_t *words = (void *)DT_REG_ADDR(DT_NODELABEL(sdram2));
    const size_t count = DT_REG_SIZE(DT_NODELABEL(sdram2)) / sizeof(*words);
    rev58_memtest.stage = 1;
    for (unsigned int bit = 0; bit < 32; ++bit) {
        for (unsigned int inverse = 0; inverse < 2; ++inverse) {
            uint32_t expected = inverse ? ~BIT(bit) : BIT(bit);
            words[0] = expected;
            uint32_t actual = words[0];
            if (actual != expected) {
                return mismatch((uintptr_t)words, expected, actual);
            }
        }
    }
    /* Separate whole-memory write/read passes detect address aliasing. */
    for (unsigned int inverse = 0; inverse < 2; ++inverse) {
        rev58_memtest.stage = inverse ? 3 : 2;
        for (size_t i = 0; i < count; ++i) {
            uint32_t expected = (uint32_t)(uintptr_t)&words[i];
            words[i] = inverse ? ~expected : expected;
        }
        if (inverse) {
            k_sleep(K_SECONDS(1)); /* Refresh/retention smoke test only. */
        }
        for (size_t i = 0; i < count; ++i) {
            uint32_t expected = (uint32_t)(uintptr_t)&words[i];
            expected = inverse ? ~expected : expected;
            uint32_t actual = words[i];
            if (actual != expected) {
                return mismatch((uintptr_t)&words[i], expected, actual);
            }
        }
    }
    rev58_memtest.stage = 4;
    volatile uint16_t *halves = (void *)words;
    volatile uint8_t *bytes = (void *)words;
    for (size_t i = 0; i < count * 2; ++i) {
        halves[i] = 0xa55a;
        bytes[2 * i] = 0x3c;
        uint16_t actual = halves[i];
        if (actual != 0xa53c) {
            return mismatch((uintptr_t)&halves[i], 0xa53c, actual);
        }
        bytes[2 * i + 1] = 0xc3;
        actual = halves[i];
        if (actual != 0xc33c) {
            return mismatch((uintptr_t)&halves[i], 0xc33c, actual);
        }
    }
    rev58_memtest.stage = 5;
    printk("SDRAM smoke test PASS: 8 MiB address/inverse/byte lanes; 1 s retention\n");
    return 0;
}
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(mac), okay)
#include <zephyr/net/phy.h>
#include <zephyr/net/mii.h>
static const struct device *const phy = DEVICE_DT_GET(DT_NODELABEL(eth_phy));
static int check_ethernet(void)
{
    if (!device_is_ready(phy) || !device_is_ready(DEVICE_DT_GET(DT_NODELABEL(mac)))) {
        return -ENODEV;
    }
    uint32_t id1, id2;
    int ret = phy_read(phy, MII_PHYID1R, &id1);
    if (ret == 0) {
        ret = phy_read(phy, MII_PHYID2R, &id2);
    }
    if (ret != 0) {
        return ret;
    }
    printk("PHY address 0: ID1=%04x ID2=%04x (compare with assembled LAN8742A)\n", id1, id2);
    return 0;
}
#endif

int rev58_peripheral_checks(void)
{
#if DT_NODE_HAS_STATUS(DT_NODELABEL(sdram), okay)
    {
        int err = check_sdram();
        if (err) { return err; }
    }
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(mac), okay)
    {
        int err = check_ethernet();
        if (err) { return err; }
    }
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi1), okay)
    const struct device *const buses[] = {
        DEVICE_DT_GET(DT_NODELABEL(spi1)),
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi2), okay)
        DEVICE_DT_GET(DT_NODELABEL(spi2)),
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi3), okay)
        DEVICE_DT_GET(DT_NODELABEL(spi3)),
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi4), okay)
        DEVICE_DT_GET(DT_NODELABEL(spi4)),
#endif
    };
    for (size_t i = 0; i < ARRAY_SIZE(buses); ++i) {
        if (!device_is_ready(buses[i])) { return -ENODEV; }
    }
    printk("Enabled SPI drivers ready\n");
#endif
    return 0;
}

void rev58_peripheral_poll(void)
{
#if DT_NODE_HAS_STATUS(DT_NODELABEL(mac), okay)
    struct phy_link_state state;
    int err = phy_get_link_state(phy, &state);
    if (err) {
        printk("PHY link read failed: %d\n", err);
    } else {
        printk("PHY link=%s speed_flags=0x%x\n", state.is_up ? "up" : "down", state.speed);
    }
#endif
}
