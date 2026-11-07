/* SPDX-License-Identifier: Apache-2.0 */
#ifndef REV58_MIXED_STREAM_H
#define REV58_MIXED_STREAM_H
#include "mixed_adc_protocol.h"
struct mixed_stats {
    struct mixed_adc_frame last;
    uint32_t frames, crc_errors, discarded, sequence_errors, flag_errors, value_errors;
    uint32_t known[3], fresh[3], stale[3], max_age[3];
    int32_t minimum[8], maximum[8];
    uint32_t expected[3], required, age_limit, dummy, negate;
    uint8_t pending[48];
    size_t used;
};
void mixed_stream_init(struct mixed_stats *s, uint32_t dummy, uint32_t required,
                       uint32_t age_limit, uint32_t negate, const uint32_t expected[3]);
void mixed_stream_feed(struct mixed_stats *s, const uint8_t *p, size_t n);
#endif
