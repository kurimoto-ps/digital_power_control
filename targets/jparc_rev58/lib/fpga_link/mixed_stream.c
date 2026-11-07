/* SPDX-License-Identifier: Apache-2.0 */
#include "mixed_stream.h"
#include <string.h>
#include <limits.h>
#include <stdbool.h>
void mixed_stream_init(struct mixed_stats *s, uint32_t dummy, uint32_t required,
                       uint32_t age_limit, uint32_t negate, const uint32_t expected[3])
{
    memset(s, 0, sizeof(*s));
    s->dummy = dummy; s->required = required; s->age_limit = age_limit; s->negate = negate;
    memcpy(s->expected, expected, sizeof(s->expected));
    for (unsigned i = 0; i < 8; ++i) { s->minimum[i] = INT32_MAX; s->maximum[i] = INT32_MIN; }
}
static void accept(struct mixed_stats *s, const struct mixed_adc_frame *f)
{
    bool adjacent = s->frames && f->sequence == s->last.sequence + UINT32_C(1);
    if ((s->frames && !adjacent) || (!s->frames && f->sequence != 0)) { ++s->sequence_errors; }
    if (!(f->flags & 8) || (f->flags >> 7) != s->negate || f->adc24_dummy != s->dummy) {
        ++s->flag_errors;
    }
    for (unsigned i = 0; i < 3; ++i) {
        bool known = mixed_adc24_known(f, i), fresh = mixed_adc24_fresh(f, i);
        if (!known && (fresh || f->adc24_raw[i] || f->adc24_age_us[i] != 65535)) { ++s->flag_errors; }
        if (adjacent && mixed_adc24_known(&s->last, i) &&
            (!known || (!fresh && (f->adc24_sequence[i] != s->last.adc24_sequence[i] ||
             f->adc24_raw[i] != s->last.adc24_raw[i] ||
             f->adc24_age_us[i] < s->last.adc24_age_us[i])))) {
            ++s->flag_errors;
        }
        /* Counters wrap at 16 bits; fresh may accompany an exact full wrap.
         * Normal 10 kHz frames advance ADC24 sequence by about 7 or 8, not 1. */
        if (known) {
            ++s->known[i];
            if (fresh) { ++s->fresh[i]; }
            if (f->adc24_age_us[i] > s->max_age[i]) { s->max_age[i] = f->adc24_age_us[i]; }
            if (f->adc24_age_us[i] > s->age_limit) { ++s->stale[i]; }
            if (s->dummy && f->adc24_raw[i] != s->expected[i]) { ++s->value_errors; }
        } else if (s->known[i]) { ++s->stale[i]; }
    }
    for (unsigned i = 0; i < 8; ++i) {
        int32_t v = mixed_adc16_signed(f->adc16[i]);
        if (v < s->minimum[i]) { s->minimum[i] = v; }
        if (v > s->maximum[i]) { s->maximum[i] = v; }
    }
    s->last = *f;
    ++s->frames;
}
void mixed_stream_feed(struct mixed_stats *s, const uint8_t *p, size_t n)
{
    while (n--) {
        s->pending[s->used++] = *p++;
        while (s->used && (s->pending[0] != 0xa5 ||
               (s->used >= 2 && s->pending[1] != 0x5a) ||
               (s->used >= 3 && s->pending[2] != 2))) {
            memmove(s->pending, s->pending + 1, --s->used); ++s->discarded;
        }
        if (s->used != 48) { continue; }
        struct mixed_adc_frame f;
        int rc = mixed_adc_decode(s->pending, 48, &f);
        if (!rc) { accept(s, &f); s->used = 0; }
        else {
            if (rc == -2) { ++s->crc_errors; } else { ++s->flag_errors; }
            memmove(s->pending, s->pending + 1, 47); s->used = 47; ++s->discarded;
        }
    }
}
