/* SPDX-License-Identifier: Apache-2.0 */
#ifndef JPARC_ADC_STREAM_H
#define JPARC_ADC_STREAM_H
#include <stddef.h>
#include <stdint.h>
struct adc_channel_stats {
    uint32_t records, value_errors, sequence_errors, first_sequence, last_sequence, last_value;
};
struct adc_stream_stats {
    struct adc_channel_stats channel[3];
    uint32_t crc_errors, header_errors, discarded_bytes;
    uint8_t pending[8];
    size_t pending_size;
    uint32_t expected[3];
};
uint16_t adc_crc16(const uint8_t *p, size_t n);
void adc_stream_init(struct adc_stream_stats *s, const uint32_t expected[3]);
void adc_stream_feed(struct adc_stream_stats *s, const uint8_t *p, size_t n);
#endif
