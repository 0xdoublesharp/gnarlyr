#include "check.h"
#include "core/hash.h"

static void the_empty_input_keeps_the_start_value(void)
{
    CHECK_EQUAL(HASH_START, hash_update(HASH_START, "", 0));
}

static void known_inputs_give_known_fnv1a_values(void)
{
    CHECK_EQUAL(0xE40C292Cu, hash_update(HASH_START, "a", 1));
    CHECK_EQUAL(0xBF9CF968u, hash_update(HASH_START, "foobar", 6));
}

static void hashing_in_pieces_matches_hashing_at_once(void)
{
    uint32_t pieces = hash_update(hash_update(HASH_START, "foo", 3), "bar", 3);
    CHECK_EQUAL(hash_update(HASH_START, "foobar", 6), pieces);
}

static void a_stored_hash_is_little_endian_and_loads_back(void)
{
    uint8_t bytes[4];
    hash_store(bytes, 0x01020304u);
    CHECK_EQUAL(4, bytes[0]);
    CHECK_EQUAL(1, bytes[3]);
    CHECK_EQUAL(0x01020304u, hash_load(bytes));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_empty_input_keeps_the_start_value),
        TEST_CASE(known_inputs_give_known_fnv1a_values),
        TEST_CASE(hashing_in_pieces_matches_hashing_at_once),
        TEST_CASE(a_stored_hash_is_little_endian_and_loads_back),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
