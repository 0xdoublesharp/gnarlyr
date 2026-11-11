#pragma once

#include <stdint.h>

#include "device/audio_math.h"

static inline int64_t audio_series_one(void)
{
    return INT64_C(1) << AUDIO_MATH_FRACTION_BITS;
}

static inline int64_t audio_series_multiply(int64_t first, int64_t second)
{
    return first * second / audio_series_one();
}

static inline int64_t audio_series_sum(int64_t angle, int first_power)
{
    int64_t angle_squared = audio_series_multiply(angle, angle);
    int64_t term = first_power == 0 ? audio_series_one() : angle;
    int64_t sum = term;
    for (int power = first_power + 2; power <= first_power + 12; power += 2) {
        term = -audio_series_multiply(term, angle_squared) / ((power - 1) * power);
        sum += term;
    }
    return sum;
}
