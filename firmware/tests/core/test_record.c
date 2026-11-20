#include "check.h"
#include "core/hash.h"
#include "core/record.h"

static const RecordKind KIND = {{'D', 'S', 'Z', 'Z'}, 0x0102};

static void a_header_is_the_magic_then_the_little_endian_version(void)
{
    uint8_t bytes[RECORD_HEADER_BYTES];
    record_begin(bytes, &KIND);
    CHECK_EQUAL('D', bytes[0]);
    CHECK_EQUAL('Z', bytes[3]);
    CHECK_EQUAL(0x02, bytes[4]);
    CHECK_EQUAL(0x01, bytes[5]);
    CHECK(record_is(bytes, &KIND));
}

static void another_magic_or_version_is_not_the_kind(void)
{
    uint8_t bytes[RECORD_HEADER_BYTES];
    record_begin(bytes, &KIND);
    bytes[2] = 'Y';
    CHECK(!record_is(bytes, &KIND));
    record_begin(bytes, &KIND);
    bytes[5] = 0x02;
    CHECK(!record_is(bytes, &KIND));
}

static void the_seal_is_the_fnv_hash_of_the_bytes_before_it(void)
{
    uint8_t bytes[10] = {1, 2, 3, 4, 5, 6};
    record_seal(bytes, 6);
    CHECK_EQUAL(hash_update(HASH_START, bytes, 6), hash_load(bytes + 6));
    CHECK(record_sealed(bytes, 6));
}

static void a_changed_byte_breaks_the_seal(void)
{
    uint8_t bytes[10] = {1, 2, 3, 4, 5, 6};
    record_seal(bytes, 6);
    bytes[0] ^= 0x80;
    CHECK(!record_sealed(bytes, 6));
    bytes[0] ^= 0x80;
    bytes[9] ^= 0x01;
    CHECK(!record_sealed(bytes, 6));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_header_is_the_magic_then_the_little_endian_version),
        TEST_CASE(another_magic_or_version_is_not_the_kind),
        TEST_CASE(the_seal_is_the_fnv_hash_of_the_bytes_before_it),
        TEST_CASE(a_changed_byte_breaks_the_seal),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
