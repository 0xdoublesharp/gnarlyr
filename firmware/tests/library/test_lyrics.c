#include <string.h>

#include "check.h"
#include "library/lyrics.h"

typedef struct Input {
    const char *text;
    uint32_t bytes;
    bool fail;
} Input;

static LyricsIndex index_file;

static bool read_input(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    const Input *input = context;
    if (input->fail || offset > input->bytes || length > input->bytes - offset) {
        return false;
    }
    memcpy(bytes, input->text + offset, length);
    return true;
}

static bool parse(Input *input)
{
    return lyrics_parse(&index_file, (ByteSource){read_input, input}, input->bytes);
}

static void timestamps_sort_and_multiple_tags_reuse_the_same_text(void)
{
    const char *text = "[ar:Someone]\n[00:12.34]Later\r\n[00:01.5][00:02.050]Earlier\n";
    Input input = {text, (uint32_t)strlen(text), false};
    CHECK(parse(&input));
    CHECK_EQUAL(3, index_file.count);
    CHECK_EQUAL(1500, (int)index_file.lines[0].time_ms);
    CHECK_EQUAL(2050, (int)index_file.lines[1].time_ms);
    CHECK_EQUAL(12340, (int)index_file.lines[2].time_ms);
    CHECK_EQUAL(-1, lyrics_current(&index_file, 1499));
    CHECK_EQUAL(1, lyrics_current(&index_file, 2050));
    char line[32];
    CHECK(lyrics_text((ByteSource){read_input, &input}, &index_file.lines[0], line, sizeof line));
    CHECK(strcmp(line, "Earlier") == 0);
    CHECK_EQUAL(index_file.lines[0].offset, index_file.lines[1].offset);
}

static void an_offset_applies_even_when_its_tag_is_last(void)
{
    const char *text = "[00:00.100]One\n[00:02]Two\n[offset:-500]";
    Input input = {text, (uint32_t)strlen(text), false};
    CHECK(parse(&input));
    CHECK_EQUAL(2, index_file.count);
    CHECK_EQUAL(0, index_file.lines[0].time_ms);
    CHECK_EQUAL(1500, index_file.lines[1].time_ms);
}

static void a_bom_before_an_offset_tag_is_accepted(void)
{
    const char *text = "\357\273\277[offset:250]\n[00:01]One";
    Input input = {text, (uint32_t)strlen(text), false};
    CHECK(parse(&input));
    CHECK_EQUAL(1, index_file.count);
    CHECK_EQUAL(1250, index_file.lines[0].time_ms);
}

static void malformed_tags_and_plain_text_do_not_create_timed_lines(void)
{
    const char *text = "[00:60]bad\n[aa:10]bad\n[01:01.1234]bad\nPlain\n[00:10]Good";
    Input input = {text, (uint32_t)strlen(text), false};
    CHECK(parse(&input));
    CHECK_EQUAL(1, index_file.count);
    CHECK_EQUAL(10000, index_file.lines[0].time_ms);
}

static void reads_are_bounded_and_failure_discards_the_partial_index(void)
{
    Input input = {"[00:01]one", 11, true};
    CHECK(!parse(&input));
    CHECK_EQUAL(0, index_file.count);
    input.fail = false;
    input.bytes = LYRICS_FILE_BYTES + 1;
    CHECK(!parse(&input));
    CHECK_EQUAL(0, index_file.count);
}

static void text_is_clipped_on_a_complete_utf8_character(void)
{
    const char *text = "[00:01]aéz";
    Input input = {text, (uint32_t)strlen(text), false};
    CHECK(parse(&input));
    char line[3];
    CHECK(lyrics_text((ByteSource){read_input, &input}, &index_file.lines[0], line, sizeof line));
    CHECK(strcmp(line, "a") == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(timestamps_sort_and_multiple_tags_reuse_the_same_text),
        TEST_CASE(an_offset_applies_even_when_its_tag_is_last),
        TEST_CASE(a_bom_before_an_offset_tag_is_accepted),
        TEST_CASE(malformed_tags_and_plain_text_do_not_create_timed_lines),
        TEST_CASE(reads_are_bounded_and_failure_discards_the_partial_index),
        TEST_CASE(text_is_clipped_on_a_complete_utf8_character),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
