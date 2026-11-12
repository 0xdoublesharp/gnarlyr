#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "check.h"

void *board_memcpy(void *destination, const void *source, size_t count);
void *board_memmove(void *destination, const void *source, size_t count);
void *board_memset(void *destination, int value, size_t count);

enum {
    AREA = 96,
    LONGEST = 40
};

static uint8_t area[AREA];
static uint8_t expected[AREA];

static void fill(void)
{
    for (uint32_t index = 0; index < AREA; index++) {
        area[index] = (uint8_t)(index * 7u + 3u);
        expected[index] = area[index];
    }
}

static bool same(void)
{
    for (uint32_t index = 0; index < AREA; index++) {
        if (area[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

static void expect_move(uint32_t to, uint32_t from, uint32_t count)
{
    uint8_t copy[LONGEST];
    for (uint32_t index = 0; index < count; index++) {
        copy[index] = expected[from + index];
    }
    for (uint32_t index = 0; index < count; index++) {
        expected[to + index] = copy[index];
    }
}

static void copies_match_a_byte_copy_at_every_alignment_and_length(void)
{
    for (uint32_t to = 0; to < 4; to++) {
        for (uint32_t from = 48; from < 52; from++) {
            for (uint32_t count = 0; count < LONGEST; count++) {
                fill();
                expect_move(to, from, count);
                CHECK(board_memcpy(area + to, area + from, count) == area + to);
                CHECK(same());
            }
        }
    }
}

static void overlapping_moves_match_a_buffered_copy_in_both_directions(void)
{
    for (uint32_t to = 8; to < 16; to++) {
        for (uint32_t from = 8; from < 16; from++) {
            for (uint32_t count = 0; count < LONGEST; count++) {
                fill();
                expect_move(to, from, count);
                CHECK(board_memmove(area + to, area + from, count) == area + to);
                CHECK(same());
            }
        }
    }
}

static void sets_touch_exactly_the_requested_bytes(void)
{
    for (uint32_t to = 0; to < 4; to++) {
        for (uint32_t count = 0; count < LONGEST; count++) {
            fill();
            for (uint32_t index = 0; index < count; index++) {
                expected[to + index] = 0xA5;
            }
            CHECK(board_memset(area + to, 0x1A5, count) == area + to);
            CHECK(same());
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(copies_match_a_byte_copy_at_every_alignment_and_length),
        TEST_CASE(overlapping_moves_match_a_buffered_copy_in_both_directions),
        TEST_CASE(sets_touch_exactly_the_requested_bytes),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
