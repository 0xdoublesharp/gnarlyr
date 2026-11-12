#include "check.h"
#include "device/wide_divide.h"

static uint64_t state = UINT64_C(0x9E3779B97F4A7C15);

static uint64_t next_random(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static uint64_t random_width(void)
{
    uint64_t value = next_random();
    uint32_t bits = (uint32_t)(next_random() % 64) + 1;
    return bits == 64 ? value : value & ((UINT64_C(1) << bits) - 1);
}

static bool divides_exactly(uint64_t numerator, uint64_t denominator)
{
    uint64_t remainder = 0;
    uint64_t quotient = wide_divide(numerator, denominator, &remainder);
    return quotient == numerator / denominator && remainder == numerator % denominator;
}

static void small_operands_divide_like_the_compiler(void)
{
    for (uint64_t numerator = 0; numerator < 300; numerator++) {
        for (uint64_t denominator = 1; denominator < 300; denominator++) {
            CHECK(divides_exactly(numerator, denominator));
        }
    }
}

static void boundary_operands_divide_like_the_compiler(void)
{
    static const uint64_t VALUES[] = {
        1,
        2,
        3,
        0xFFFF,
        0x10000,
        0x10001,
        0x7FFFFFFF,
        0x80000000,
        0xFFFFFFFF,
        UINT64_C(0x100000000),
        UINT64_C(0x100000001),
        UINT64_C(0x1FFFFFFFF),
        UINT64_C(0x7FFFFFFFFFFFFFFF),
        UINT64_C(0x8000000000000000),
        UINT64_C(0x8000000000000001),
        UINT64_C(0xFFFFFFFF00000000),
        UINT64_C(0xFFFFFFFFFFFFFFFE),
        UINT64_C(0xFFFFFFFFFFFFFFFF),
    };
    enum {
        COUNT = sizeof VALUES / sizeof VALUES[0]
    };
    for (int left = 0; left < COUNT; left++) {
        CHECK(divides_exactly(0, VALUES[left]));
        for (int right = 0; right < COUNT; right++) {
            CHECK(divides_exactly(VALUES[left], VALUES[right]));
            CHECK(divides_exactly(VALUES[left] - 1, VALUES[right]));
            CHECK(divides_exactly(VALUES[left] * 3 + 1, VALUES[right]));
        }
    }
}

static void random_operands_of_every_width_divide_like_the_compiler(void)
{
    for (int round = 0; round < 4000000; round++) {
        uint64_t numerator = random_width();
        uint64_t denominator = random_width();
        if (denominator != 0 && !divides_exactly(numerator, denominator)) {
            CHECK(divides_exactly(numerator, denominator));
            return;
        }
    }
}

static void near_multiples_divide_like_the_compiler(void)
{
    for (int round = 0; round < 1000000; round++) {
        uint64_t denominator = random_width() | 1;
        uint64_t limit = UINT64_MAX / denominator;
        uint64_t multiple = limit == UINT64_MAX ? random_width() : random_width() % (limit + 1);
        uint64_t product = denominator * multiple;
        if (!divides_exactly(product, denominator) || !divides_exactly(product - 1, denominator) ||
            !divides_exactly(product + denominator - 1, denominator)) {
            CHECK(divides_exactly(product, denominator));
            return;
        }
    }
}

static void the_remainder_is_optional(void)
{
    CHECK_EQUAL(UINT64_C(0x5555555555555555), wide_divide(UINT64_MAX, 3, NULL));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(small_operands_divide_like_the_compiler),
        TEST_CASE(boundary_operands_divide_like_the_compiler),
        TEST_CASE(random_operands_of_every_width_divide_like_the_compiler),
        TEST_CASE(near_multiples_divide_like_the_compiler),
        TEST_CASE(the_remainder_is_optional),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
