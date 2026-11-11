#include <string.h>

#include "check.h"
#include "core/string_builder.h"

enum {
    SMALL = 6,
    LARGE = 32
};

static void appended_text_is_terminated(void)
{
    char buffer[LARGE];
    StringBuilder builder = string_builder_make(buffer, sizeof buffer);
    string_builder_append(&builder, "Echo");
    string_builder_append_char(&builder, ' ');
    string_builder_append(&builder, "Mini");
    CHECK(strcmp("Echo Mini", buffer) == 0);
    CHECK_EQUAL(9, builder.length);
    CHECK(!builder.overflowed);
}

static void unsigned_values_are_written_in_decimal(void)
{
    char buffer[LARGE];
    StringBuilder builder = string_builder_make(buffer, sizeof buffer);
    string_builder_append_unsigned(&builder, 0);
    string_builder_append_char(&builder, ',');
    string_builder_append_unsigned(&builder, 4294967295u);
    CHECK(strcmp("0,4294967295", buffer) == 0);
}

static void overflow_keeps_the_longest_prefix_that_fits(void)
{
    char buffer[SMALL];
    StringBuilder builder = string_builder_make(buffer, sizeof buffer);
    string_builder_append(&builder, "Overflowing");
    CHECK(strcmp("Overf", buffer) == 0);
    CHECK(builder.overflowed);
}

static void overflow_never_leaves_part_of_a_character(void)
{
    char buffer[SMALL];
    StringBuilder builder = string_builder_make(buffer, sizeof buffer);
    string_builder_append(&builder, "abcd\xC3\xA9z");
    CHECK(strcmp("abcd", buffer) == 0);
    CHECK_EQUAL(4, builder.length);
    CHECK(builder.overflowed);
    builder = string_builder_make(buffer, sizeof buffer);
    string_builder_append(&builder, "abc\xC3\xA9z");
    CHECK(strcmp("abc\xC3\xA9", buffer) == 0);
    builder = string_builder_make(buffer, sizeof buffer);
    string_builder_append(&builder, "ab\xE2\x80\xA6z");
    CHECK(strcmp("ab\xE2\x80\xA6", buffer) == 0);
    builder = string_builder_make(buffer, sizeof buffer);
    string_builder_append(&builder, "abc\xE2\x80\xA6");
    CHECK(strcmp("abc", buffer) == 0);
}

static void a_number_that_does_not_fit_is_cut_like_text(void)
{
    char buffer[SMALL];
    StringBuilder builder = string_builder_make(buffer, sizeof buffer);
    string_builder_append(&builder, "ab");
    string_builder_append_unsigned(&builder, 123456);
    CHECK(strcmp("ab123", buffer) == 0);
    CHECK(builder.overflowed);
}

static void a_fresh_builder_holds_an_empty_string(void)
{
    char buffer[SMALL] = "junk";
    StringBuilder builder = string_builder_make(buffer, sizeof buffer);
    CHECK(strcmp("", buffer) == 0);
    CHECK_EQUAL(0, builder.length);
}

static void an_empty_buffer_reports_overflow_without_dereferencing_it(void)
{
    StringBuilder builder = string_builder_make(NULL, 0);
    string_builder_append(&builder, "hello");
    string_builder_append_unsigned(&builder, 42);
    CHECK(builder.overflowed);
    CHECK_EQUAL(0, builder.length);
}

static void a_one_byte_buffer_remains_terminated_after_overflow(void)
{
    char buffer[2] = {'x', 'y'};
    StringBuilder builder = string_builder_make(buffer, 1);
    string_builder_append_char(&builder, 'a');
    string_builder_append(&builder, "\xc3\xa9");
    CHECK(builder.overflowed);
    CHECK_EQUAL(0, builder.length);
    CHECK_EQUAL('\0', buffer[0]);
    CHECK_EQUAL('y', buffer[1]);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(appended_text_is_terminated),
        TEST_CASE(unsigned_values_are_written_in_decimal),
        TEST_CASE(overflow_keeps_the_longest_prefix_that_fits),
        TEST_CASE(overflow_never_leaves_part_of_a_character),
        TEST_CASE(a_number_that_does_not_fit_is_cut_like_text),
        TEST_CASE(a_fresh_builder_holds_an_empty_string),
        TEST_CASE(an_empty_buffer_reports_overflow_without_dereferencing_it),
        TEST_CASE(a_one_byte_buffer_remains_terminated_after_overflow),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
