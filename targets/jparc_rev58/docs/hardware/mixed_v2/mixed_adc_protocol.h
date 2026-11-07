#ifndef JPARC_MIXED_ADC_PROTOCOL_H
#define JPARC_MIXED_ADC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Firmware major 2 is incompatible with the old 8-byte SPI1 records. */
#define MIXED_FIRMWARE_VERSION       UINT32_C(0x00020000)
#define MIXED_FRAME_BYTES            48u
#define MIXED_FRAME_PAYLOAD_BYTES    46u
#define MIXED_FRAME_FORMAT           0x02u
#define MIXED_SPI1_HZ                20000000u

#define MIX_REG_VERSION              0x0004u
#define MIX_REG_CAPABILITIES         0x0008u
#define MIX_REG_DAC_SOURCE_MASK      0x0060u
#define MIX_DAC_AUTO_ALL             0x07ffu
#define MIX_REG_DAC_MANUAL_BASE      0x0200u
#define MIX_REG_DAC_SENT_BASE        0x0300u
#define MIX_REG_DAC_EFFECTIVE_BASE   0x0340u
#define MIX_REG_DAC_AUTO_BASE        0x0380u
#define MIX_REG_STREAM_CONTROL       0x0400u
#define MIX_REG_STREAM_STATUS        0x0404u
#define MIX_REG_TX_COUNT             0x0408u
#define MIX_REG_ADC24_COUNT          0x040cu
#define MIX_REG_ADC24_RX_BASE        0x0410u
#define MIX_REG_ADC24_REPLACED_BASE  0x0420u
#define MIX_REG_ADC24_RESTART_BASE   0x0430u
#define MIX_REG_ADC24_LAST_BASE      0x0440u
#define MIX_REG_ADC24_PENDING_BASE   0x0450u
#define MIX_REG_ADC24_AGE_BASE       0x0460u
#define MIX_REG_ADC24_SOURCE         0x0480u
#define MIX_REG_ADC24_DUMMY_BASE     0x0484u
#define MIX_REG_ADS_CONTROL          0x0500u
#define MIX_REG_ADS_DELAY            0x0504u
#define MIX_REG_ADS_CONVERSION       0x0508u
#define MIX_REG_ADS_PHASE            0x050cu
#define MIX_REG_ADS_NEGATE           0x0510u
#define MIX_REG_ADS_PERIOD           0x0514u
#define MIX_REG_ADS_STATUS           0x0518u
#define MIX_REG_ADS_APPLIED_ID       0x051cu
#define MIX_REG_ADS_RX_COUNT         0x0520u
#define MIX_REG_ADS_SOURCE_DROP      0x0524u
#define MIX_REG_ADS_MISSED           0x0528u
#define MIX_REG_ADS_CONFIG_REJECTED  0x052cu
#define MIX_REG_ADS_SUBMITTED_ID     0x0530u
#define MIX_REG_FRAME_CAPTURE_COUNT  0x0534u
#define MIX_REG_FRAME_DROP_COUNT     0x0538u
#define MIX_REG_FRAME_FIFO_LEVEL     0x053cu
#define MIX_REG_ADS_APPLIED_DELAY    0x0540u
#define MIX_REG_ADS_APPLIED_CONV     0x0544u
#define MIX_REG_ADS_APPLIED_PHASE    0x0548u
#define MIX_REG_ADS_APPLIED_NEGATE   0x054cu
#define MIX_REG_ADS_APPLIED_PERIOD   0x0550u
#define MIX_REG_ADS_APPLIED_ENABLE   0x0554u
#define MIX_REG_LAST_FRAME_SEQUENCE  0x0558u
#define MIX_REG_ADS_CHANNEL_COUNT    0x0560u
#define MIX_REG_ADS_VALUE_BASE       0x0580u
#define MIX_REG_ADS_RAW_BASE         0x05a0u

#define MIX_STREAM_ENABLE            1u
#define MIX_STREAM_CLEAR             2u
#define MIX_ADS_ENABLE               1u
#define MIX_ADS_APPLY                2u
#define MIX_ADS_CONFIG_PENDING       1u
#define MIX_ADS_CONFIG_ERROR         2u

struct mixed_adc_frame {
    uint32_t sequence;
    uint16_t adc24_sequence[3];
    uint16_t adc24_age_us[3];
    uint32_t adc24_raw[3];
    uint16_t adc16[8]; /* Effective data, including optional arithmetic negation. */
    uint8_t flags;
    uint8_t adc24_dummy;
};

static inline uint16_t mixed_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t mixed_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static inline uint16_t mixed_crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0xffffu;
    for (size_t i = 0; i < n; ++i) {
        c ^= (uint16_t)p[i] << 8;
        for (unsigned int b = 0; b < 8; ++b) {
            c = (uint16_t)((c << 1) ^ ((c & 0x8000u) ? 0x1021u : 0u));
        }
    }
    return c;
}

/* 0=OK, -1=size/header/format, -2=CRC, -3=reserved bits. */
static inline int mixed_adc_decode(const uint8_t *p, size_t n,
                                   struct mixed_adc_frame *out)
{
    if (p == NULL || out == NULL || n != MIXED_FRAME_BYTES ||
        p[0] != 0xa5u || p[1] != 0x5au || p[2] != MIXED_FRAME_FORMAT) {
        return -1;
    }
    if (mixed_crc16(p, MIXED_FRAME_PAYLOAD_BYTES) != mixed_be16(p + 46)) {
        return -2;
    }
    if ((p[45] & 0xfeu) != 0u) {
        return -3;
    }
    out->flags = p[3];
    out->sequence = mixed_be32(p + 4);
    for (unsigned int i = 0; i < 3; ++i) {
        out->adc24_sequence[i] = mixed_be16(p + 8 + 2 * i);
        out->adc24_age_us[i] = mixed_be16(p + 14 + 2 * i);
        out->adc24_raw[i] = ((uint32_t)p[20 + 3 * i] << 16) |
                            ((uint32_t)p[21 + 3 * i] << 8) | p[22 + 3 * i];
    }
    for (unsigned int i = 0; i < 8; ++i) {
        out->adc16[i] = mixed_be16(p + 29 + 2 * i);
    }
    out->adc24_dummy = p[45] & 1u;
    return 0;
}

static inline int mixed_adc24_known(const struct mixed_adc_frame *f, unsigned int i)
{
    return i < 3u && (f->flags & (1u << i)) != 0u;
}

static inline int mixed_adc24_fresh(const struct mixed_adc_frame *f, unsigned int i)
{
    return i < 3u && (f->flags & (1u << (4u + i))) != 0u;
}

/* Only use this if the signal is intended to be interpreted as signed 16-bit. */
static inline int32_t mixed_adc16_signed(uint16_t x)
{
    return (x & 0x8000u) ? (int32_t)x - 65536 : (int32_t)x;
}

#endif
