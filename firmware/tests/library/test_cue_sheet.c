#include <string.h>

#include "check.h"
#include "library/cue_sheet.h"
#include "memory_source.h"

enum {
    MAX_ENTRIES = 8
};

typedef struct Parsed {
    CueSheet sheet;
    CueEntry entries[MAX_ENTRIES];
    uint32_t count;
    bool usable;
} Parsed;

static void parse(const char *text, Parsed *parsed)
{
    static CueSheet sheet;
    MemorySource memory = {.bytes = (const uint8_t *)text, .size = (uint32_t)strlen(text)};
    cue_sheet_open(&sheet, memory_source(&memory), memory.size);
    parsed->count = 0;
    for (const CueEntry *entry = cue_sheet_next(&sheet); entry != NULL;
         entry = cue_sheet_next(&sheet)) {
        CHECK(parsed->count < MAX_ENTRIES);
        parsed->entries[parsed->count++] = *entry;
    }
    parsed->sheet = sheet;
    cue_sheet_open(&sheet, memory_source(&memory), memory.size);
    parsed->usable = cue_sheet_usable(&sheet);
}

static bool text_is(const char *actual, const char *expected)
{
    return strcmp(actual, expected) == 0;
}

static void a_track_takes_its_fields_and_ends_where_the_next_one_starts(void)
{
    static Parsed parsed;
    parse("FILE \"a.flac\" WAVE\n"
          "  TRACK 01 AUDIO\n    TITLE \"One\"\n    PERFORMER \"Band\"\n    INDEX 01 00:00:00\n"
          "  TRACK 02 AUDIO\n    TITLE \"Two\"\n    INDEX 01 03:20:00\n",
          &parsed);
    CHECK_EQUAL(2u, parsed.count);
    CHECK_EQUAL(1u, parsed.entries[0].number);
    CHECK(text_is(parsed.entries[0].title, "One"));
    CHECK(text_is(parsed.entries[0].performer, "Band"));
    CHECK_EQUAL(0u, parsed.entries[0].start_ms);
    CHECK_EQUAL(200000u, parsed.entries[0].end_ms);
    CHECK_EQUAL(2u, parsed.entries[1].number);
    CHECK_EQUAL(200000u, parsed.entries[1].start_ms);
    CHECK_EQUAL(0u, parsed.entries[1].end_ms);
    CHECK(text_is(parsed.entries[1].performer, ""));
}

static void lines_before_the_first_track_describe_the_album(void)
{
    static Parsed parsed;
    parse("REM GENRE Alternative Rock\nREM DATE 1990\nREM COMMENT x\nPERFORMER \"Group\"\n"
          "TITLE \"Record\"\nFILE \"r.flac\" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n",
          &parsed);
    CHECK(text_is(parsed.sheet.album.genre, "Alternative Rock"));
    CHECK(text_is(parsed.sheet.album.date, "1990"));
    CHECK(text_is(parsed.sheet.album.performer, "Group"));
    CHECK(text_is(parsed.sheet.album.title, "Record"));
    CHECK(text_is(parsed.sheet.album.file, "r.flac"));
    CHECK(text_is(parsed.entries[0].title, ""));
}

static void index_times_count_seventy_five_frames_per_second(void)
{
    static Parsed parsed;
    parse("FILE a.wav WAVE\nTRACK 1 AUDIO\nINDEX 01 00:00:01\nTRACK 2 AUDIO\nINDEX 01 12:34:56\n",
          &parsed);
    CHECK_EQUAL(13u, parsed.entries[0].start_ms);
    CHECK_EQUAL(754746u, parsed.entries[1].start_ms);
    CHECK_EQUAL(754746u, parsed.entries[0].end_ms);
}

static void keywords_ignore_case(void)
{
    static Parsed parsed;
    parse("file \"a.wav\" wave\nrem date 2001\nTrAcK 03 audio\ntitle Plain Title\n"
          "PERFORMER Someone Else\nindex 01 00:01:00\n",
          &parsed);
    CHECK_EQUAL(1u, parsed.count);
    CHECK_EQUAL(3u, parsed.entries[0].number);
    CHECK(text_is(parsed.entries[0].title, "Plain Title"));
    CHECK(text_is(parsed.entries[0].performer, "Someone Else"));
    CHECK(text_is(parsed.sheet.album.date, "2001"));
}

static void quoting_follows_the_cue_rules(void)
{
    static Parsed parsed;
    parse("FILE \"my file.wav\" WAVE\nTRACK 01 AUDIO\nTITLE \"A \\\" B\"\nPERFORMER \"X\" tail\n"
          "INDEX 01 00:00:00\n",
          &parsed);
    CHECK(text_is(parsed.sheet.album.file, "my file.wav"));
    CHECK(text_is(parsed.entries[0].title, "A \\"));
    CHECK(text_is(parsed.entries[0].performer, "X"));
    parse("FILE unquoted.wav WAVE\nTRACK 01 AUDIO\nTITLE \"open\nINDEX 01 00:00:00\n", &parsed);
    CHECK(text_is(parsed.sheet.album.file, "unquoted.wav"));
    CHECK(text_is(parsed.entries[0].title, "open"));
}

static void a_byte_order_mark_and_windows_line_ends_are_handled(void)
{
    static Parsed parsed;
    parse("\xef\xbb\xbf"
          "TITLE \"Disc\"\r\nFILE \"d.flac\" WAVE\r\n  TRACK 01 AUDIO\r\n    TITLE "
          "\"Caf\xc3\xa9\"\r\n"
          "    INDEX 01 00:00:00\r\n",
          &parsed);
    CHECK(text_is(parsed.sheet.album.title, "Disc"));
    CHECK(text_is(parsed.entries[0].title, "Caf\xc3\xa9"));
    CHECK(parsed.usable);
}

static void text_that_is_not_utf8_reads_as_windows_1252(void)
{
    static Parsed parsed;
    parse("FILE \"d.flac\" WAVE\nTRACK 01 AUDIO\nTITLE \"Caf\xe9 \x80\"\nINDEX 01 00:00:00\n",
          &parsed);
    CHECK(text_is(parsed.entries[0].title, "Caf\xc3\xa9 \xe2\x82\xac"));
}

static void a_track_without_index_01_is_dropped_and_its_neighbours_close_up(void)
{
    static Parsed parsed;
    parse("FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nTRACK 02 AUDIO\nINDEX 00 00:10:00\n"
          "TRACK 03 AUDIO\nINDEX 00 00:20:00\nINDEX 01 00:30:00\n",
          &parsed);
    CHECK_EQUAL(2u, parsed.count);
    CHECK_EQUAL(1u, parsed.entries[0].number);
    CHECK_EQUAL(0u, parsed.entries[0].start_ms);
    CHECK_EQUAL(3u, parsed.entries[1].number);
    CHECK_EQUAL(30000u, parsed.entries[1].start_ms);
    CHECK_EQUAL(30000u, parsed.entries[0].end_ms);
}

static void malformed_index_lines_are_ignored(void)
{
    static Parsed parsed;
    parse("FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00\nINDEX 01 aa:bb:cc\nINDEX\n"
          "INDEX 01 00:00:02\n",
          &parsed);
    CHECK_EQUAL(1u, parsed.count);
    CHECK_EQUAL(26u, parsed.entries[0].start_ms);
}

static void track_numbers_must_be_between_one_and_ninety_nine(void)
{
    static Parsed parsed;
    parse("FILE a.wav WAVE\nTRACK AUDIO\nINDEX 01 00:00:00\n"
          "TRACK 00 AUDIO\nTITLE Not Album\nPERFORMER Not Artist\nINDEX 01 00:01:00\n"
          "TRACK 100 AUDIO\nINDEX 01 00:02:00\n"
          "TRACK 01AUDIO\nINDEX 01 00:03:00\nTRACK 09 AUDIO\nINDEX 01 00:04:00\n",
          &parsed);
    CHECK_EQUAL(1u, parsed.count);
    CHECK_EQUAL(9u, parsed.entries[0].number);
    CHECK_EQUAL(4000u, parsed.entries[0].start_ms);
    CHECK(text_is(parsed.sheet.album.title, ""));
    CHECK(text_is(parsed.sheet.album.performer, ""));
}

static void index_fields_stay_within_cue_limits_and_end_at_the_line(void)
{
    static Parsed parsed;
    parse("FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:60:00\n", &parsed);
    CHECK_EQUAL(0u, parsed.count);
    parse("FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:75\n", &parsed);
    CHECK_EQUAL(0u, parsed.count);
    parse("FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00 trailing\n", &parsed);
    CHECK_EQUAL(0u, parsed.count);
    CHECK(!parsed.usable);
}

static void equal_or_descending_starts_are_dropped(void)
{
    static Parsed parsed;
    parse("FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:10:00\n"
          "TRACK 02 AUDIO\nINDEX 01 00:05:00\nTRACK 03 AUDIO\nINDEX 01 00:10:00\n"
          "TRACK 04 AUDIO\nINDEX 01 00:15:00\n",
          &parsed);
    CHECK_EQUAL(2u, parsed.count);
    CHECK_EQUAL(1u, parsed.entries[0].number);
    CHECK_EQUAL(10000u, parsed.entries[0].start_ms);
    CHECK_EQUAL(15000u, parsed.entries[0].end_ms);
    CHECK_EQUAL(4u, parsed.entries[1].number);
    CHECK_EQUAL(15000u, parsed.entries[1].start_ms);
}

static void a_sheet_is_usable_with_one_file_and_at_least_one_track(void)
{
    static Parsed parsed;
    parse("FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n", &parsed);
    CHECK(parsed.usable);
    parse("FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nFILE b.wav WAVE\nTRACK 02 AUDIO\n"
          "INDEX 01 00:00:00\n",
          &parsed);
    CHECK(!parsed.usable);
    parse("FILE a.wav WAVE\nTRACK 01 AUDIO\nINDEX 00 00:00:00\n", &parsed);
    CHECK(!parsed.usable);
    parse("TRACK 01 AUDIO\nINDEX 01 00:00:00\n", &parsed);
    CHECK(!parsed.usable);
    parse("", &parsed);
    CHECK(!parsed.usable);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_track_takes_its_fields_and_ends_where_the_next_one_starts),
        TEST_CASE(lines_before_the_first_track_describe_the_album),
        TEST_CASE(index_times_count_seventy_five_frames_per_second),
        TEST_CASE(keywords_ignore_case),
        TEST_CASE(quoting_follows_the_cue_rules),
        TEST_CASE(a_byte_order_mark_and_windows_line_ends_are_handled),
        TEST_CASE(text_that_is_not_utf8_reads_as_windows_1252),
        TEST_CASE(a_track_without_index_01_is_dropped_and_its_neighbours_close_up),
        TEST_CASE(malformed_index_lines_are_ignored),
        TEST_CASE(track_numbers_must_be_between_one_and_ninety_nine),
        TEST_CASE(index_fields_stay_within_cue_limits_and_end_at_the_line),
        TEST_CASE(equal_or_descending_starts_are_dropped),
        TEST_CASE(a_sheet_is_usable_with_one_file_and_at_least_one_track),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
