#include <string.h>

#include "check.h"
#include "core/dac_identity.h"

static DacIdentity read_as(uint8_t mask, const uint8_t *bytes)
{
    DacIdentity identity = {.source = DAC_IDENTITY_READ, .read_mask = mask};
    memcpy(identity.bytes, bytes, DAC_IDENTITY_BYTES);
    return identity;
}

static bool shows(DacIdentity identity, const char *expected)
{
    char text[DAC_IDENTITY_TEXT_CAPACITY];
    dac_identity_text(&identity, text, sizeof text);
    return strcmp(expected, text) == 0;
}

static void a_cs43198_shows_its_id_bytes_then_revision_and_subrevision(void)
{
    static const uint8_t BYTES[] = {0x43, 0x19, 0x80, 0xA0, 0x00};
    DacIdentity identity = read_as(DAC_IDENTITY_ALL_READ, BYTES);
    CHECK_EQUAL(DAC_PART_CS43198, dac_identity_part(&identity));
    CHECK(shows(identity, "431980 A0.00"));
}

static void a_cs43131_shows_its_id_bytes_then_revision_and_subrevision(void)
{
    static const uint8_t BYTES[] = {0x43, 0x13, 0x10, 0xB1, 0x07};
    DacIdentity identity = read_as(DAC_IDENTITY_ALL_READ, BYTES);
    CHECK_EQUAL(DAC_PART_CS43131, dac_identity_part(&identity));
    CHECK(shows(identity, "431310 B1.07"));
}

static void the_low_nibble_of_the_third_byte_is_shown_but_not_part_of_the_id(void)
{
    static const uint8_t BYTES[] = {0x43, 0x19, 0x8F, 0xA0, 0x00};
    DacIdentity identity = read_as(DAC_IDENTITY_ALL_READ, BYTES);
    CHECK_EQUAL(DAC_PART_CS43198, dac_identity_part(&identity));
    CHECK(shows(identity, "43198F A0.00"));
}

static void an_unrecognised_part_is_marked_and_keeps_its_bytes(void)
{
    static const uint8_t BYTES[] = {0x43, 0x99, 0x00, 0xC2, 0x1E};
    DacIdentity identity = read_as(DAC_IDENTITY_ALL_READ, BYTES);
    CHECK_EQUAL(DAC_PART_UNKNOWN, dac_identity_part(&identity));
    CHECK(shows(identity, "?439900 C2.1E"));
}

static void a_part_that_answers_zeros_is_unrecognised_not_blank(void)
{
    static const uint8_t BYTES[DAC_IDENTITY_BYTES] = {0};
    DacIdentity identity = read_as(DAC_IDENTITY_ALL_READ, BYTES);
    CHECK_EQUAL(DAC_PART_UNKNOWN, dac_identity_part(&identity));
    CHECK(shows(identity, "?000000 00.00"));
}

static void a_failed_read_says_so_and_shows_no_bytes(void)
{
    static const uint8_t BYTES[] = {0x43, 0x19, 0x80, 0xA0, 0x00};
    DacIdentity identity = read_as(0, BYTES);
    CHECK_EQUAL(DAC_PART_UNKNOWN, dac_identity_part(&identity));
    CHECK(shows(identity, "Read failed"));
}

static void a_partial_read_shows_dashes_for_the_bytes_it_lacks(void)
{
    static const uint8_t BYTES[] = {0x43, 0x19, 0x00, 0x00, 0x00};
    DacIdentity identity = read_as(0x03, BYTES);
    CHECK_EQUAL(DAC_PART_UNKNOWN, dac_identity_part(&identity));
    CHECK(shows(identity, "4319-- --.--"));
    CHECK(shows(read_as(0x17, BYTES), "431900 --.00"));
}

static void a_partial_read_never_names_a_part(void)
{
    static const uint8_t BYTES[] = {0x43, 0x19, 0x80, 0xA0, 0x00};
    DacIdentity identity = read_as(0x0F, BYTES);
    CHECK_EQUAL(DAC_PART_UNKNOWN, dac_identity_part(&identity));
    CHECK(shows(identity, "431980 A0.--"));
}

static void before_any_read_it_asks_for_a_track(void)
{
    DacIdentity identity = {0};
    CHECK_EQUAL(DAC_PART_UNKNOWN, dac_identity_part(&identity));
    CHECK(shows(identity, "Play a track"));
}

static void a_simulated_dac_never_shows_bytes(void)
{
    static const uint8_t BYTES[] = {0x43, 0x19, 0x80, 0xA0, 0x00};
    DacIdentity identity = read_as(DAC_IDENTITY_ALL_READ, BYTES);
    identity.source = DAC_IDENTITY_SIMULATED;
    CHECK_EQUAL(DAC_PART_UNKNOWN, dac_identity_part(&identity));
    CHECK(shows(identity, "Simulated"));
}

static void the_longest_text_fits_the_capacity(void)
{
    static const uint8_t WIDEST[] = {0xDD, 0xDD, 0xDD, 0xDD, 0xDD};
    CHECK(shows(read_as(DAC_IDENTITY_ALL_READ, WIDEST), "?DDDDDD DD.DD"));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_cs43198_shows_its_id_bytes_then_revision_and_subrevision),
        TEST_CASE(a_cs43131_shows_its_id_bytes_then_revision_and_subrevision),
        TEST_CASE(the_low_nibble_of_the_third_byte_is_shown_but_not_part_of_the_id),
        TEST_CASE(an_unrecognised_part_is_marked_and_keeps_its_bytes),
        TEST_CASE(a_part_that_answers_zeros_is_unrecognised_not_blank),
        TEST_CASE(a_failed_read_says_so_and_shows_no_bytes),
        TEST_CASE(a_partial_read_shows_dashes_for_the_bytes_it_lacks),
        TEST_CASE(a_partial_read_never_names_a_part),
        TEST_CASE(before_any_read_it_asks_for_a_track),
        TEST_CASE(a_simulated_dac_never_shows_bytes),
        TEST_CASE(the_longest_text_fits_the_capacity),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
