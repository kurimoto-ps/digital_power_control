/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <soc.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_pwr.h>
#include "peripheral_checks.h"
#ifdef CONFIG_JPARC_SPI4_REGS_TEST
#include "spi4_test_server.h"
#endif

/* Read from GDB even when no RTT client is connected. Raw register snapshots. */
struct bringup_status {
    uint32_t magic;
    uint32_t ticks;
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pwr_cr3;
    uint32_t pwr_d3cr;
    uint32_t syscfg_pwrcr;
    uint32_t syscfg_ur1;
    int error;
    int ready_raw;
    int fault_raw;
    int irq_raw;
};
volatile struct bringup_status rev58_status;
#define IO DT_PATH(zephyr_user)
static const struct gpio_dt_spec inputs[] = {
    GPIO_DT_SPEC_GET(IO, ready_gpios),
    GPIO_DT_SPEC_GET(IO, fault_gpios),
    GPIO_DT_SPEC_GET(IO, irq_gpios),
};

int main(void)
{
    LL_APB4_GRP1_EnableClock(LL_APB4_GRP1_PERIPH_SYSCFG);
    rev58_status.magic = 0x52563538; /* RV58 */
    rev58_status.sysclk_hz = HAL_RCC_GetSysClockFreq();
    rev58_status.hclk_hz = HAL_RCC_GetHCLKFreq();
    rev58_status.pwr_cr3 = PWR->CR3;
    rev58_status.pwr_d3cr = PWR->D3CR;
    rev58_status.syscfg_pwrcr = SYSCFG->PWRCR;
    rev58_status.syscfg_ur1 = SYSCFG->UR1;
    printk("J-PARC rev58 M7 bring-up: SYSCLK=%u HCLK=%u\n",
           rev58_status.sysclk_hz, rev58_status.hclk_hz);
    /* BCM4 must be cleared before boot: do not change persistent option bytes here. */
    if (rev58_status.sysclk_hz != 400000000 || rev58_status.hclk_hz != 200000000 ||
        (PWR->CR3 & (PWR_CR3_SMPSEN | PWR_CR3_LDOEN | PWR_CR3_BYPASS)) != PWR_CR3_SMPSEN ||
        (PWR->D3CR & PWR_D3CR_VOS) != LL_PWR_REGU_VOLTAGE_SCALE1 ||
        (SYSCFG->PWRCR & SYSCFG_PWRCR_ODEN) || (SYSCFG->UR1 & SYSCFG_UR1_BCM4)) {
        rev58_status.error = -EINVAL;
        printk("Clock/power/BCM4 mismatch: inspect rev58_status with debugger\n");
        return 0;
    }
    for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
        int err = gpio_is_ready_dt(&inputs[i]) ?
                  gpio_pin_configure_dt(&inputs[i], GPIO_INPUT) : -ENODEV;
        if (err) {
            rev58_status.error = err;
            printk("GPIO input %u initialization failed: %d\n", (unsigned int)i, err);
            return 0;
        }
    }
    rev58_status.error = rev58_peripheral_checks();
    if (rev58_status.error) {
        printk("Peripheral check failed: %d\n", rev58_status.error);
        return 0;
    }
#ifdef CONFIG_JPARC_SPI4_REGS_TEST
    rev58_status.error = spi4_test_server_start();
    if (rev58_status.error) {
        printk("SPI4 server initialization failed: %d\n", rev58_status.error);
        return 0;
    }
#endif
    printk("Bring-up ready; inspect rev58_status.ticks and raw GPIO levels\n");
    for (;;) {
        rev58_status.ready_raw = gpio_pin_get_raw(inputs[0].port, inputs[0].pin);
        rev58_status.fault_raw = gpio_pin_get_raw(inputs[1].port, inputs[1].pin);
        rev58_status.irq_raw = gpio_pin_get_raw(inputs[2].port, inputs[2].pin);
        ++rev58_status.ticks;
        rev58_peripheral_poll();
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
