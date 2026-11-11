#pragma once
#include "platform.h"
#include "fft.h"
#include "mdct.h"
#define av_log2(x) ((x) != 0 ? 31 - __builtin_clz((uint32_t)(x)) : 0)
static inline unsigned int bs_generic(unsigned int v, int mode)
{
    unsigned int leading = v != 0 ? (unsigned int)__builtin_clz(v) : 32u;
    return (mode & 1) != 0 ? leading : (leading == 32 ? 0 : 31 - leading);
}
#define BS_LOG2 0
#define BS_CLZ 1
#define BS_SHORT 2
#define BS_0_0 4
