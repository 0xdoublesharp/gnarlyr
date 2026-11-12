#include <stdint.h>
#include <string.h>

#include "check.h"
#include "jpeg_internal.h"

enum {
    BLOCKS = 2000,
    SIDE = 8,
    VALUE_RANGE = 4096
};

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static void random_block(uint32_t *state, int32_t *values, bool *rows_used)
{
    uint32_t density = next_random(state) % 64;
    for (uint32_t v = 0; v < SIDE; v++) {
        rows_used[v] = false;
        for (uint32_t u = 0; u < SIDE; u++) {
            bool kept = next_random(state) % 64 <= density;
            int32_t value = (int32_t)(next_random(state) % (2 * VALUE_RANGE)) - VALUE_RANGE;
            values[v * SIDE + u] = kept ? value : 0;
            rows_used[v] = rows_used[v] || values[v * SIDE + u] != 0;
        }
    }
}

static void check_outputs(uint32_t shift)
{
    static JpegDecoder decoder;
    memset(&decoder, 0, sizeof decoder);
    decoder.shift = shift;
    decoder.outputs = SIDE >> shift;
    jpeg_prepare_kernel(&decoder);
    uint32_t state = shift + 1;
    int32_t values[SIDE * SIDE];
    bool rows_used[SIDE];
    int32_t fast[SIDE * SIDE];
    int32_t general[SIDE * SIDE];
    uint32_t mismatches = 0;
    for (uint32_t block = 0; block < BLOCKS; block++) {
        random_block(&state, values, rows_used);
        jpeg_inverse_block(&decoder, values, rows_used, fast);
        jpeg_inverse_general(&decoder, values, rows_used, general);
        uint32_t count = decoder.outputs * decoder.outputs;
        mismatches += memcmp(fast, general, count * sizeof fast[0]) != 0;
    }
    CHECK_EQUAL(0u, mismatches);
}

static void two_by_two_blocks_match_the_general_inverse(void)
{
    check_outputs(2);
}

static void four_by_four_blocks_match_the_general_inverse(void)
{
    check_outputs(1);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(two_by_two_blocks_match_the_general_inverse),
        TEST_CASE(four_by_four_blocks_match_the_general_inverse),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
