#include <stdio.h>
#include <string.h>

#include "check.h"
#include "library/playlist_reader.h"
#include "memory_source.h"

typedef struct Expected {
    const char *bytes;
    uint32_t size;
    const char *entries[6];
} Expected;

static void check_entries(const char *bytes, uint32_t size, const char *const *wanted)
{
    MemorySource memory = {.bytes = (const uint8_t *)bytes, .size = size};
    PlaylistReader reader;
    CHECK(playlist_reader_open(&reader, memory_source(&memory), size));
    char entry[PLAYLIST_ENTRY_CAPACITY];
    int index = 0;
    while (playlist_reader_next(&reader, entry, sizeof entry)) {
        CHECK(wanted[index] != NULL);
        if (wanted[index] == NULL) {
            return;
        }
        CHECK_EQUAL(0, strcmp(wanted[index], entry));
        index++;
    }
    CHECK(wanted[index] == NULL);
}

static void utf8_with_a_bom_skips_comments_and_blank_lines(void)
{
    static const char TEXT[] = "\xef\xbb\xbf#EXTM3U\r\n../A/b.flac\r\n\r\n  # c\n  x.flac  \n";
    static const char *const WANTED[] = {"../A/b.flac", "x.flac", NULL};
    check_entries(TEXT, sizeof TEXT - 1, WANTED);
}

static void windows_1252_is_the_fallback(void)
{
    static const char TEXT[] = "Bj\xf6rk\\J\xf3ga.flac\x85"
                               "end\nlast\x1dsplit\n\x81"
                               "bad";
    static const char *const WANTED[] = {"Bj\xc3\xb6rk\\J\xc3\xb3ga.flac\xe2\x80\xa6"
                                         "end",
                                         "last", "split",
                                         "\xef\xbf\xbd"
                                         "bad",
                                         NULL};
    check_entries(TEXT, sizeof TEXT - 1, WANTED);
}

static void unicode_line_separators_split_utf8(void)
{
    static const char TEXT[] = "uni\xe2\x80\xa8sep\xc2\x85next\x0bv\x0c"
                               "f";
    static const char *const WANTED[] = {"uni", "sep", "next", "v", "f", NULL};
    check_entries(TEXT, sizeof TEXT - 1, WANTED);
}

static void an_encoded_surrogate_is_not_utf8(void)
{
    static const char TEXT[] = "\xed\xa0\x80"
                               "bad surrogate";
    static const char *const WANTED[] = {"\xc3\xad\xc2\xa0\xe2\x82\xac"
                                         "bad surrogate",
                                         NULL};
    check_entries(TEXT, sizeof TEXT - 1, WANTED);
}

static void long_files_cross_chunk_boundaries(void)
{
    static char text[5000];
    static const char LINE[] = "Artist/Album/Track.flac\n";
    uint32_t used = 0;
    while (used + sizeof LINE - 1 < sizeof text) {
        memcpy(text + used, LINE, sizeof LINE - 1);
        used += sizeof LINE - 1;
    }
    MemorySource memory = {.bytes = (const uint8_t *)text, .size = used};
    PlaylistReader reader;
    CHECK(playlist_reader_open(&reader, memory_source(&memory), used));
    char entry[PLAYLIST_ENTRY_CAPACITY];
    uint32_t count = 0;
    while (playlist_reader_next(&reader, entry, sizeof entry)) {
        CHECK_EQUAL(0, strcmp("Artist/Album/Track.flac", entry));
        count++;
    }
    CHECK_EQUAL(used / (sizeof LINE - 1), count);
}

static uint32_t start_of_entry(const char *text, uint32_t skip)
{
    MemorySource memory = {.bytes = (const uint8_t *)text, .size = (uint32_t)strlen(text)};
    PlaylistReader reader;
    char entry[PLAYLIST_ENTRY_CAPACITY];
    CHECK(playlist_reader_open(&reader, memory_source(&memory), memory.size));
    for (uint32_t index = 0; index <= skip; index++) {
        CHECK(playlist_reader_next(&reader, entry, sizeof entry));
    }
    return reader.start_ms;
}

static void a_start_directive_belongs_to_the_entry_after_it(void)
{
    static const char TEXT[] = "#EXTM3U\n#EXTINF:3,Song\n#PODIUM-START:1500\nA/one.flac\n"
                               "A/two.flac\n\n  #PODIUM-START:0  \r\n\r\nA/three.flac\n";
    CHECK_EQUAL(1500u, start_of_entry(TEXT, 0));
    CHECK_EQUAL(PLAYLIST_NO_START, start_of_entry(TEXT, 1));
    CHECK_EQUAL(0u, start_of_entry(TEXT, 2));
}

static void the_last_start_directive_before_an_entry_wins(void)
{
    CHECK_EQUAL(20u, start_of_entry("#PODIUM-START:10\n#PODIUM-START:20\nA/one.flac\n", 0));
}

static void a_malformed_start_directive_is_ignored(void)
{
    static const char *const LINES[] = {"#PODIUM-START:",           "#PODIUM-START:12x",
                                        "#PODIUM-START:-5",         "#PODIUM-START:12345678901",
                                        "#PODIUM-START:4294967295", "#podium-start:5",
                                        "#PODIUM-START 5"};
    for (uint32_t index = 0; index < sizeof LINES / sizeof LINES[0]; index++) {
        char text[128];
        snprintf(text, sizeof text, "%s\nA/one.flac\n", LINES[index]);
        CHECK_EQUAL(PLAYLIST_NO_START, start_of_entry(text, 0));
    }
    CHECK_EQUAL(4294967294u, start_of_entry("#PODIUM-START:4294967294\nA/one.flac\n", 0));
}

static void the_playlist_padding_of_a_favorites_file_reads_as_one_entry(void)
{
    static char text[32768];
    static const char HEAD[] = "#EXTM3U\n#PODIUM-START:1000\nA/B/C.flac\n";
    memset(text, ' ', sizeof text);
    memcpy(text, HEAD, sizeof HEAD - 1);
    MemorySource memory = {.bytes = (const uint8_t *)text, .size = sizeof text};
    PlaylistReader reader;
    char entry[PLAYLIST_ENTRY_CAPACITY];
    CHECK(playlist_reader_open(&reader, memory_source(&memory), sizeof text));
    CHECK(playlist_reader_next(&reader, entry, sizeof entry));
    CHECK_EQUAL(0, strcmp("A/B/C.flac", entry));
    CHECK_EQUAL(1000u, reader.start_ms);
    CHECK(!playlist_reader_next(&reader, entry, sizeof entry));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(utf8_with_a_bom_skips_comments_and_blank_lines),
        TEST_CASE(windows_1252_is_the_fallback),
        TEST_CASE(unicode_line_separators_split_utf8),
        TEST_CASE(an_encoded_surrogate_is_not_utf8),
        TEST_CASE(long_files_cross_chunk_boundaries),
        TEST_CASE(a_start_directive_belongs_to_the_entry_after_it),
        TEST_CASE(the_last_start_directive_before_an_entry_wins),
        TEST_CASE(a_malformed_start_directive_is_ignored),
        TEST_CASE(the_playlist_padding_of_a_favorites_file_reads_as_one_entry),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
