#include <stdio.h>

#include "check.h"
#include "tags_internal.h"

typedef struct DurationInput {
    uint64_t units;
    uint64_t preroll;
    uint32_t rate;
    uint32_t alignment;
    unsigned style;
} DurationInput;

static uint32_t reference(double seconds)
{
    double milliseconds = seconds * 1000.0;
    if (!(milliseconds > 0.0)) {
        return 0;
    }
    if (milliseconds >= 4294967295.0) {
        return UINT32_MAX;
    }
    uint32_t whole = (uint32_t)milliseconds;
    double fraction = milliseconds - (double)whole;
    return whole + (uint32_t)(fraction > 0.5 || (fraction == 0.5 && (whole & 1u) != 0));
}

static bool same_result(DurationInput input)
{
    TrackTags tags = {0};
    TagReader reader = {.tags = &tags};
    double seconds = 0;
    if (input.style == 0) {
        tag_set_duration(&reader, input.units, input.rate);
        seconds = (double)input.units / (double)input.rate;
    } else if (input.style == 1) {
        tag_set_divided_duration(&reader, input.units, input.alignment, input.rate);
        seconds = (double)input.units / (double)input.alignment / (double)input.rate;
    } else if (input.style == 2) {
        tag_set_bit_duration(&reader, input.units, input.alignment, input.rate);
        seconds = (double)input.units * 8.0 / (double)input.alignment / (double)input.rate;
    } else {
        tag_set_preroll_duration(&reader, input.units, input.preroll);
        seconds = (double)input.units / 10000000.0 - (double)input.preroll / 1000.0;
    }
    uint32_t expected = reference(seconds);
    if (expected != tags.duration_ms) {
        fprintf(stderr, "style %u units %llu rate %u alignment %u preroll %llu\n", input.style,
                (unsigned long long)input.units, input.rate, input.alignment,
                (unsigned long long)input.preroll);
        CHECK_EQUAL(expected, tags.duration_ms);
        return false;
    }
    return true;
}

static uint64_t random_word(uint64_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static void half_millisecond_boundaries_keep_the_existing_binary_rounding(void)
{
    for (uint64_t half = 1; half < 100000; half += 2) {
        DurationInput input = {.units = half * 24, .rate = 48000, .alignment = 1};
        if (!same_result(input)) {
            return;
        }
    }
}

static void large_values_and_saturation_match_the_previous_arithmetic(void)
{
    static const uint64_t UNITS[] = {
        0, 1, 2, 3, 1003, UINT32_MAX, UINT64_C(1) << 53, (UINT64_C(1) << 53) + 1, UINT64_MAX};
    static const uint32_t RATES[] = {1, 7, 1000, 2000, 8000, 44100, 48000, UINT32_MAX};
    for (unsigned style = 0; style < 4; style++) {
        for (size_t unit = 0; unit < sizeof UNITS / sizeof UNITS[0]; unit++) {
            for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
                DurationInput input = {.units = UNITS[unit],
                                       .preroll = UNITS[unit] / 10000,
                                       .rate = RATES[rate],
                                       .alignment = 65535,
                                       .style = style};
                if (!same_result(input)) {
                    return;
                }
            }
        }
    }
}

static void all_duration_forms_match_a_deterministic_integer_corpus(void)
{
    uint64_t state = UINT64_C(0xB43D2A179EF50123);
    for (unsigned index = 0; index < 50000; index++) {
        uint64_t units = random_word(&state) >> (index % 64);
        uint64_t delta = random_word(&state) % 100000;
        uint64_t milliseconds = units / 10000;
        DurationInput input = {.units = units,
                               .preroll = milliseconds > delta ? milliseconds - delta : 0,
                               .rate = (uint32_t)random_word(&state) | 1u,
                               .alignment = (uint32_t)random_word(&state) % 65535 + 1,
                               .style = index % 4};
        if (!same_result(input)) {
            return;
        }
    }
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(half_millisecond_boundaries_keep_the_existing_binary_rounding),
        TEST_CASE(large_values_and_saturation_match_the_previous_arithmetic),
        TEST_CASE(all_duration_forms_match_a_deterministic_integer_corpus),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
