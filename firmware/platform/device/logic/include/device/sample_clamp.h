#pragma once

#include <stdint.h>

enum {
    SAMPLE_CLAMP_MAX = 8388607,
    SAMPLE_CLAMP_MIN = -8388608
};

static inline int32_t sample_clamp_24(int32_t value)
{
#if defined(__ARM_ARCH_7M__)
    int32_t clamped;
    __asm__("ssat %0, #24, %1" : "=r"(clamped) : "r"(value));
    return clamped;
#else
    return value > SAMPLE_CLAMP_MAX   ? SAMPLE_CLAMP_MAX
           : value < SAMPLE_CLAMP_MIN ? SAMPLE_CLAMP_MIN
                                      : value;
#endif
}
