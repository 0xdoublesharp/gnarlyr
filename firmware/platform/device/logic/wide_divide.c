#include "device/wide_divide.h"

#include <stddef.h>

enum {
    WIDE_DIVIDE_DIGIT_BITS = 16,
    WIDE_DIVIDE_DIGIT = 0xFFFF
};

static uint32_t leading_zeros(uint32_t value)
{
    return value == 0 ? 32u : (uint32_t)__builtin_clz(value);
}

static uint32_t divide_digit(uint32_t *partial, uint32_t digit, uint32_t divisor)
{
    uint32_t high = divisor >> WIDE_DIVIDE_DIGIT_BITS;
    uint32_t low = divisor & WIDE_DIVIDE_DIGIT;
    uint32_t quotient = *partial / high;
    uint32_t rest = *partial - quotient * high;
    while (rest <= WIDE_DIVIDE_DIGIT &&
           (quotient > WIDE_DIVIDE_DIGIT ||
            quotient * low > (rest << WIDE_DIVIDE_DIGIT_BITS | digit))) {
        quotient--;
        rest += high;
    }
    *partial = (*partial << WIDE_DIVIDE_DIGIT_BITS | digit) - quotient * divisor;
    return quotient;
}

static uint32_t divide_long(uint64_t numerator, uint32_t divisor, uint32_t *remainder)
{
    uint32_t shift = leading_zeros(divisor);
    divisor <<= shift;
    numerator <<= shift;
    uint32_t partial = (uint32_t)(numerator >> 32);
    uint32_t low = (uint32_t)numerator;
    uint32_t quotient = divide_digit(&partial, low >> WIDE_DIVIDE_DIGIT_BITS, divisor)
                        << WIDE_DIVIDE_DIGIT_BITS;
    quotient |= divide_digit(&partial, low & WIDE_DIVIDE_DIGIT, divisor);
    *remainder = partial >> shift;
    return quotient;
}

static uint64_t divide_narrow(uint64_t numerator, uint32_t divisor, uint64_t *remainder)
{
    uint32_t high = (uint32_t)(numerator >> 32);
    if (high == 0) {
        uint32_t quotient = (uint32_t)numerator / divisor;
        *remainder = (uint32_t)numerator - quotient * divisor;
        return quotient;
    }
    uint32_t upper = high / divisor;
    uint32_t rest = 0;
    uint64_t carried = (uint64_t)(high - upper * divisor) << 32 | (uint32_t)numerator;
    uint32_t lower = divide_long(carried, divisor, &rest);
    *remainder = rest;
    return (uint64_t)upper << 32 | lower;
}

static uint64_t divide_wide(uint64_t numerator, uint64_t denominator, uint64_t *remainder)
{
    uint32_t shift = leading_zeros((uint32_t)(denominator >> 32));
    uint32_t top = (uint32_t)((denominator << shift) >> 32);
    uint32_t unused = 0;
    uint64_t quotient = divide_long(numerator >> 1, top, &unused) >> (31 - shift);
    if (quotient != 0) {
        quotient--;
    }
    uint64_t rest = numerator - quotient * denominator;
    if (rest >= denominator) {
        quotient++;
        rest -= denominator;
    }
    *remainder = rest;
    return quotient;
}

uint64_t wide_divide(uint64_t numerator, uint64_t denominator, uint64_t *remainder)
{
    uint64_t rest = 0;
    uint64_t quotient = denominator >> 32 == 0
                            ? divide_narrow(numerator, (uint32_t)denominator, &rest)
                            : divide_wide(numerator, denominator, &rest);
    if (remainder != NULL) {
        *remainder = rest;
    }
    return quotient;
}
