#include "check.h"
#include "device/core1_arena.h"

enum {
    ARENA_BYTES = 256
};

static _Alignas(16) uint8_t bytes[ARENA_BYTES];
static Core1Arena arena;

static void reset(void)
{
    core1_arena_init(&arena, bytes, ARENA_BYTES);
}

static void lasting_blocks_round_to_eight_from_the_bottom(void)
{
    reset();
    CHECK(core1_arena_allocate(&arena, 3) == bytes);
    CHECK(core1_arena_allocate(&arena, 9) == bytes + 8);
    CHECK(core1_arena_allocate(&arena, 0) == bytes + 24);
    CHECK_EQUAL(24, arena.low);
}

static void work_blocks_take_their_alignment_from_the_top(void)
{
    reset();
    CHECK(core1_arena_push(&arena, 3, 2) == bytes + ARENA_BYTES - 4);
    CHECK(core1_arena_push(&arena, 6, 8) == bytes + ARENA_BYTES - 16);
    CHECK(core1_arena_push(&arena, 4, 16) == bytes + ARENA_BYTES - 32);
    CHECK_EQUAL(ARENA_BYTES - 32, arena.high);
}

static void refused_requests_leave_both_ends_unchanged(void)
{
    reset();
    CHECK(core1_arena_allocate(&arena, 100) != NULL);
    CHECK(core1_arena_push(&arena, 100, 8) != NULL);
    CHECK(core1_arena_allocate(&arena, 57) == NULL);
    CHECK(core1_arena_push(&arena, 57, 8) == NULL);
    CHECK(core1_arena_allocate(&arena, (size_t)-1) == NULL);
    CHECK(core1_arena_push(&arena, (size_t)-1, 1) == NULL);
    CHECK_EQUAL(104, arena.low);
    CHECK_EQUAL(ARENA_BYTES - 104, arena.high);
    CHECK(core1_arena_allocate(&arena, 48) != NULL);
    CHECK_EQUAL(arena.low, arena.high);
}

static void a_restore_releases_work_pushed_after_its_mark(void)
{
    reset();
    CHECK(core1_arena_push(&arena, 16, 8) != NULL);
    uint32_t mark = core1_arena_mark(&arena);
    CHECK(core1_arena_push(&arena, 64, 8) != NULL);
    CHECK(core1_arena_restore(&arena, mark));
    CHECK_EQUAL(ARENA_BYTES - 16, arena.high);
}

static void a_restore_cannot_claim_work_or_leave_the_arena(void)
{
    reset();
    CHECK(core1_arena_push(&arena, 16, 8) != NULL);
    uint32_t mark = core1_arena_mark(&arena);
    CHECK(!core1_arena_restore(&arena, mark - 8));
    CHECK(!core1_arena_restore(&arena, ARENA_BYTES + 8));
    CHECK_EQUAL(mark, arena.high);
}

static void the_peak_counts_both_ends_and_survives_a_reset(void)
{
    reset();
    CHECK(core1_arena_allocate(&arena, 40) != NULL);
    CHECK(core1_arena_push(&arena, 24, 8) != NULL);
    core1_arena_reset(&arena);
    CHECK(core1_arena_allocate(&arena, 8) != NULL);
    CHECK_EQUAL(64, arena.peak);
    CHECK_EQUAL(8, arena.low);
    CHECK_EQUAL(ARENA_BYTES, arena.high);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(lasting_blocks_round_to_eight_from_the_bottom),
        TEST_CASE(work_blocks_take_their_alignment_from_the_top),
        TEST_CASE(refused_requests_leave_both_ends_unchanged),
        TEST_CASE(a_restore_releases_work_pushed_after_its_mark),
        TEST_CASE(a_restore_cannot_claim_work_or_leave_the_arena),
        TEST_CASE(the_peak_counts_both_ends_and_survives_a_reset),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
