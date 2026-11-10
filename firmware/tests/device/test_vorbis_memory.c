#include <string.h>

#include "check.h"
#include "device/vorbis_memory.h"

enum {
    ARENA_BYTES = 4096,
    GUARD_BYTES = 8
};

static _Alignas(8) uint8_t arena[ARENA_BYTES + GUARD_BYTES * 2];
static VorbisMemory memory;

static void reset(void)
{
    vorbis_memory_unbind();
    memset(arena, 0xA5, sizeof arena);
    CHECK(vorbis_memory_init(&memory, arena + GUARD_BYTES, ARENA_BYTES));
}

static void guards_are_intact(void)
{
    for (uint32_t index = 0; index < GUARD_BYTES; index++) {
        CHECK_EQUAL(0xA5, arena[index]);
        CHECK_EQUAL(0xA5, arena[ARENA_BYTES + GUARD_BYTES + index]);
    }
}

static void permanent_and_temporary_blocks_are_aligned_and_disjoint(void)
{
    reset();
    CHECK(vorbis_memory_bind(&memory));
    uint8_t *first = vorbis_memory_allocate(3);
    uint8_t *second = vorbis_memory_allocate(17);
    uint8_t *temporary = vorbis_temporary_allocate(9);
    CHECK(first == arena + GUARD_BYTES);
    CHECK(second == arena + GUARD_BYTES + 8);
    CHECK(temporary == arena + GUARD_BYTES + ARENA_BYTES - 16);
    CHECK_EQUAL(32, memory.used);
    CHECK_EQUAL(ARENA_BYTES - 16, memory.temporary);
    CHECK_EQUAL(48, memory.peak);
    guards_are_intact();
    vorbis_memory_unbind();
}

static void cleared_allocations_only_clear_their_own_requested_bytes(void)
{
    reset();
    CHECK(vorbis_memory_bind(&memory));
    const uint8_t *block = vorbis_memory_calloc(3, 5);
    CHECK(block == arena + GUARD_BYTES);
    if (block != NULL) {
        for (uint32_t index = 0; index < 15; index++) {
            CHECK_EQUAL(0, block[index]);
        }
        CHECK_EQUAL(0xA5, block[15]);
    }
    guards_are_intact();
    vorbis_memory_unbind();
}

static void inner_temporary_work_is_released_at_its_function_boundary(void)
{
    VORBIS_TEMPORARY_SCOPE;
    uint8_t *block = vorbis_temporary_allocate(700);
    CHECK(block != NULL);
    if (block != NULL) {
        memset(block, 0x42, 700);
    }
}

static void nested_temporary_scopes_retain_the_callers_work(void)
{
    reset();
    CHECK(vorbis_memory_bind(&memory));
    uint8_t *outer = vorbis_temporary_allocate(17);
    CHECK(outer != NULL);
    uint32_t before = memory.temporary;
    inner_temporary_work_is_released_at_its_function_boundary();
    CHECK_EQUAL(before, memory.temporary);
    CHECK_EQUAL(24 + 704, memory.peak);
    CHECK(vorbis_memory_allocate(ARENA_BYTES - 24) != NULL);
    CHECK_EQUAL(memory.used, memory.temporary);
    guards_are_intact();
    vorbis_memory_unbind();
}

static void full_arenas_abort_before_touching_memory_and_can_be_reset(void)
{
    for (uint32_t kind = 0; kind < 4; kind++) {
        reset();
        if (VORBIS_MEMORY_TRY(&memory) == 0) {
            CHECK(vorbis_memory_bind(&memory));
            CHECK(vorbis_memory_allocate(ARENA_BYTES) != NULL);
            if (kind == 0) {
                (void)vorbis_memory_allocate(1);
            } else if (kind == 1) {
                (void)vorbis_temporary_allocate(1);
            } else if (kind == 2) {
                (void)vorbis_memory_calloc((size_t)-1, 2);
            } else {
                (void)vorbis_memory_allocate((size_t)-1);
            }
            CHECK(false);
        } else {
            CHECK_EQUAL(VORBIS_MEMORY_EXHAUSTED, memory.error);
            CHECK_EQUAL(ARENA_BYTES, memory.used);
            CHECK_EQUAL(ARENA_BYTES, memory.temporary);
        }
        vorbis_memory_unbind();
        guards_are_intact();
    }
    reset();
    CHECK(vorbis_memory_bind(&memory));
    CHECK(vorbis_memory_allocate(16) != NULL);
    CHECK_EQUAL(VORBIS_MEMORY_OK, memory.error);
    vorbis_memory_unbind();
}

static void malformed_input_can_leave_the_same_guard_without_marking_oom(void)
{
    reset();
    if (VORBIS_MEMORY_TRY(&memory) == 0) {
        CHECK(vorbis_memory_bind(&memory));
        vorbis_memory_invalid();
    } else {
        CHECK_EQUAL(VORBIS_MEMORY_INVALID, memory.error);
    }
    vorbis_memory_unbind();
    guards_are_intact();
}

static void independent_decoder_states_cannot_replace_an_active_guard(void)
{
    reset();
    VorbisMemory other;
    CHECK(vorbis_memory_init(&other, arena + GUARD_BYTES, ARENA_BYTES));
    CHECK(vorbis_memory_bind(&memory));
    CHECK(!vorbis_memory_bind(&other));
    CHECK(vorbis_memory_allocate(16) != NULL);
    CHECK_EQUAL(16, memory.used);
    CHECK_EQUAL(0, other.used);
    vorbis_memory_unbind();
    CHECK(vorbis_memory_bind(&other));
    vorbis_memory_unbind();
}

static void invalid_workspaces_are_refused_without_changing_the_previous_one(void)
{
    reset();
    CHECK(!vorbis_memory_init(&memory, arena + 1, ARENA_BYTES));
    CHECK(!vorbis_memory_init(&memory, NULL, ARENA_BYTES));
    CHECK(!vorbis_memory_init(&memory, arena, 7));
    CHECK_EQUAL(ARENA_BYTES, memory.capacity);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(permanent_and_temporary_blocks_are_aligned_and_disjoint),
        TEST_CASE(cleared_allocations_only_clear_their_own_requested_bytes),
        TEST_CASE(nested_temporary_scopes_retain_the_callers_work),
        TEST_CASE(full_arenas_abort_before_touching_memory_and_can_be_reset),
        TEST_CASE(malformed_input_can_leave_the_same_guard_without_marking_oom),
        TEST_CASE(independent_decoder_states_cannot_replace_an_active_guard),
        TEST_CASE(invalid_workspaces_are_refused_without_changing_the_previous_one),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
