#include "tags_internal.h"

typedef struct DurationValue {
    uint64_t mantissa;
    int exponent;
} DurationValue;

enum {
    MANTISSA_BITS = 53,
    GUARD_BITS = 3,
    MILLISECONDS_PER_SECOND = 1000
};

static uint64_t rounded_shift(uint64_t value, unsigned shift)
{
    if (shift == 0) {
        return value;
    }
    if (shift >= 64) {
        return 0;
    }
    uint64_t whole = value >> shift;
    uint64_t fraction = value & ((UINT64_C(1) << shift) - 1);
    uint64_t half = UINT64_C(1) << (shift - 1);
    return whole + (uint64_t)(fraction > half || (fraction == half && (whole & 1u) != 0));
}

static DurationValue normalized(uint64_t mantissa, int exponent)
{
    if (mantissa == 0) {
        return (DurationValue){0};
    }
    int bits = 0;
    for (uint64_t rest = mantissa; rest != 0; rest >>= 1) {
        bits++;
    }
    int shift = bits - MANTISSA_BITS;
    mantissa = shift > 0 ? rounded_shift(mantissa, (unsigned)shift) : mantissa << -shift;
    exponent += shift;
    if (mantissa == UINT64_C(1) << MANTISSA_BITS) {
        mantissa >>= 1;
        exponent++;
    }
    return (DurationValue){mantissa, exponent};
}

static DurationValue divided(DurationValue numerator, DurationValue denominator)
{
    if (numerator.mantissa == 0 || denominator.mantissa == 0) {
        return (DurationValue){0};
    }
    bool high = numerator.mantissa >= denominator.mantissa;
    unsigned bits = MANTISSA_BITS - (high ? 1u : 0u);
    uint64_t quotient = high ? 1u : 0u;
    uint64_t remainder = numerator.mantissa - (high ? denominator.mantissa : 0u);
    for (unsigned bit = 0; bit < bits; bit++) {
        remainder <<= 1;
        quotient <<= 1;
        if (remainder >= denominator.mantissa) {
            remainder -= denominator.mantissa;
            quotient++;
        }
    }
    uint64_t twice = remainder << 1;
    quotient += (uint64_t)(twice > denominator.mantissa ||
                           (twice == denominator.mantissa && (quotient & 1u) != 0));
    return normalized(quotient, numerator.exponent - denominator.exponent - (int)bits);
}

static DurationValue ratio(uint64_t units, uint64_t rate)
{
    return divided(normalized(units, 0), normalized(rate, 0));
}

static uint64_t sticky_shift(uint64_t value, unsigned shift)
{
    if (shift == 0) {
        return value;
    }
    if (shift >= 64) {
        return (uint64_t)(value != 0);
    }
    uint64_t mask = (UINT64_C(1) << shift) - 1;
    return value >> shift | (uint64_t)((value & mask) != 0);
}

static DurationValue difference(DurationValue first, DurationValue second)
{
    if (first.mantissa == 0 || (second.mantissa != 0 && (first.exponent < second.exponent ||
                                                         (first.exponent == second.exponent &&
                                                          first.mantissa <= second.mantissa)))) {
        return (DurationValue){0};
    }
    if (second.mantissa == 0) {
        return first;
    }
    unsigned shift = (unsigned)(first.exponent - second.exponent);
    uint64_t subtrahend = sticky_shift(second.mantissa << GUARD_BITS, shift);
    return normalized((first.mantissa << GUARD_BITS) - subtrahend, first.exponent - GUARD_BITS);
}

static void set_seconds(TagReader *reader, DurationValue seconds)
{
    DurationValue value = normalized(seconds.mantissa * MILLISECONDS_PER_SECOND, seconds.exponent);
    uint64_t rounded = value.exponent >= 0 && value.mantissa != 0
                           ? UINT32_MAX
                           : rounded_shift(value.mantissa, (unsigned)-value.exponent);
    reader->tags->duration_ms = rounded > UINT32_MAX ? UINT32_MAX : (uint32_t)rounded;
}

void tag_set_duration(TagReader *reader, uint64_t units, uint64_t rate)
{
    set_seconds(reader, ratio(units, rate));
}

void tag_set_divided_duration(TagReader *reader, uint64_t units, uint32_t alignment, uint32_t rate)
{
    set_seconds(reader, divided(ratio(units, alignment), normalized(rate, 0)));
}

void tag_set_bit_duration(TagReader *reader, uint64_t bytes, uint32_t channels, uint32_t rate)
{
    DurationValue bits = normalized(bytes, 0);
    bits.exponent += 3;
    DurationValue samples = divided(bits, normalized(channels > 0 ? channels : 1u, 0));
    set_seconds(reader, divided(samples, normalized(rate, 0)));
}

void tag_set_preroll_duration(TagReader *reader, uint64_t ticks, uint64_t preroll_ms)
{
    set_seconds(reader, difference(ratio(ticks, 10000000), ratio(preroll_ms, 1000)));
}
