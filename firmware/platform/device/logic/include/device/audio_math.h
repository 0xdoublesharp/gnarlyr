#pragma once

#include <stdint.h>

enum {
    AUDIO_MATH_FRACTION_BITS = 30
};

typedef struct AudioRate {
    uint32_t rate;
    uint32_t mclk;
    uint8_t acodec[4];
    uint8_t dac_byte;
} AudioRate;

const AudioRate *audio_clock_rate(uint32_t rate);

uint32_t i2c_clock_divider(uint32_t pclk_hz, uint32_t bus_khz);
uint32_t i2s_divider_truncated(uint32_t mclk_hz, uint32_t rate_hz);
uint32_t i2s_divider_rounded(uint32_t mclk_hz, uint32_t rate_hz);
uint32_t i2s_clock_register(uint32_t divider);
int64_t fixed_cosine(int64_t angle);
int64_t fixed_sine(int64_t angle);
void tone_fill(int16_t *samples, int count, int16_t amplitude);
