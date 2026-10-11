/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_STM32H745XX_M7), "rev58 supports M7 bring-up only");
BUILD_ASSERT(!IS_ENABLED(CONFIG_STM32H7_DUAL_CORE), "M4 bring-up is not implemented");
BUILD_ASSERT(DT_ENUM_HAS_VALUE(DT_NODELABEL(pwr), power_supply, smps_direct), "Direct SMPS required");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(rcc), clock_frequency) == 400000000, "Review VOS1 clock limits before changing SYSCLK");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(rcc), hpre) == 2, "HCLK must be 200 MHz");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(clk_hse), clock_frequency) == 25000000, "rev58 HSE is 25 MHz");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(clk_hse), hse_bypass), "rev58 uses an oscillator");
BUILD_ASSERT(DT_SAME_NODE(DT_CHOSEN(zephyr_sram), DT_NODELABEL(sram0)), "Keep startup data in internal SRAM");
