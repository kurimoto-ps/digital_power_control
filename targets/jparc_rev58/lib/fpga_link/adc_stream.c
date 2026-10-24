/* SPDX-License-Identifier: Apache-2.0 */
#include "adc_stream.h"
#include <string.h>
uint16_t adc_crc16(const uint8_t *p, size_t n)
{
    static const uint16_t table[16] = {
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
        0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef
    };
    uint16_t crc = 0xffff;
    while (n--) {
        crc = (uint16_t)(crc << 4) ^ table[(crc >> 12) ^ (*p >> 4)];
        crc = (uint16_t)(crc << 4) ^ table[(crc >> 12) ^ (*p++ & 15)];
    }
    return crc;
}
void adc_stream_init(struct adc_stream_stats *s, const uint32_t expected[3])
{
    memset(s, 0, sizeof(*s));
    memcpy(s->expected, expected, sizeof(s->expected));
}
void adc_stream_feed(struct adc_stream_stats *s, const uint8_t *p, size_t n)
{
    while (n--) {
        s->pending[s->pending_size++] = *p++;
        if (s->pending_size < 8) { continue; }
        const uint8_t *r = s->pending;
        if (r[0] < 0xa1 || r[0] > 0xa3) {
            ++s->header_errors;
        } else if (adc_crc16(r, 6) != ((uint16_t)r[6] << 8 | r[7])) {
            ++s->crc_errors;
        } else {
            unsigned int ch = r[0] - 0xa1;
            struct adc_channel_stats *c = &s->channel[ch];
            uint16_t seq = (uint16_t)r[1] << 8 | r[2];
            uint32_t value = (uint32_t)r[3] << 16 | (uint32_t)r[4] << 8 | r[5];
            if (c->records == 0) {
                c->first_sequence = seq;
                if (seq != 0) { ++c->sequence_errors; }
            } else if ((uint16_t)(seq - c->last_sequence) != 1) {
                ++c->sequence_errors;
            }
            ++c->records;
            c->last_sequence = seq;
            c->last_value = value;
            if (value != s->expected[ch]) { ++c->value_errors; }
            s->pending_size = 0;
            continue;
        }
        ++s->discarded_bytes;
        memmove(s->pending, s->pending + 1, 7);
        s->pending_size = 7;
    }
}
