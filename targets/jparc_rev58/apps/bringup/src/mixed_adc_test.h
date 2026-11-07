/* SPDX-License-Identifier: Apache-2.0 */
#ifndef REV58_MIXED_ADC_TEST_H
#define REV58_MIXED_ADC_TEST_H
#include "spi4_protocol.h"
bool mixed_adc_command(char *line, char reply[SPI4_REPLY_SIZE],
                       spi4_transfer_fn transfer, void *context);
#endif
