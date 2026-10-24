/* SPDX-License-Identifier: Apache-2.0 */
#ifndef JPARC_ADC_DUMMY_TEST_H
#define JPARC_ADC_DUMMY_TEST_H
#include "spi4_protocol.h"
/* Called only by the SPI4 server owner. Returns false for unrelated commands. */
bool adc_dummy_command(char *line, char reply[SPI4_REPLY_SIZE],
                       spi4_transfer_fn transfer, void *context);
#endif
