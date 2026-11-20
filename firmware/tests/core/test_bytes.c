#include "check.h"
#include "core/bytes.h"

static const uint8_t SAMPLE[] = {0x81, 0x92, 0xA3, 0xF4};

static void little_endian_values_read_the_low_byte_first(void)
{
    CHECK_EQUAL(0x9281u, bytes_le16(SAMPLE));
    CHECK_EQUAL(0xF4A39281u, bytes_le32(SAMPLE));
}

static void big_endian_values_read_the_high_byte_first(void)
{
    CHECK_EQUAL(0x8192u, bytes_be16(SAMPLE));
    CHECK_EQUAL(0x8192A3F4u, bytes_be32(SAMPLE));
}

static void written_values_read_back_in_their_own_order(void)
{
    uint8_t bytes[4];
    bytes_put_le16(bytes, 0x12345678u);
    CHECK_EQUAL(0x78, bytes[0]);
    CHECK_EQUAL(0x56, bytes[1]);
    bytes_put_le32(bytes, 0x8192A3F4u);
    CHECK_EQUAL(0xF4, bytes[0]);
    CHECK_EQUAL(0x8192A3F4u, bytes_le32(bytes));
    bytes_put_be32(bytes, 0x8192A3F4u);
    CHECK_EQUAL(0x81, bytes[0]);
    CHECK_EQUAL(0xF4, bytes[3]);
    CHECK_EQUAL(0x8192A3F4u, bytes_be32(bytes));
}

static void a_synchsafe_value_takes_seven_bits_from_each_byte(void)
{
    static const uint8_t largest[] = {0x7F, 0x7F, 0x7F, 0x7F};
    static const uint8_t ten_bits[] = {0x00, 0x00, 0x07, 0x7F};
    CHECK_EQUAL(0x0FFFFFFFu, bytes_synchsafe(largest));
    CHECK_EQUAL(0x3FFu, bytes_synchsafe(ten_bits));
    CHECK_EQUAL(0x002491F4u, bytes_synchsafe(SAMPLE));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(little_endian_values_read_the_low_byte_first),
        TEST_CASE(big_endian_values_read_the_high_byte_first),
        TEST_CASE(written_values_read_back_in_their_own_order),
        TEST_CASE(a_synchsafe_value_takes_seven_bits_from_each_byte),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
