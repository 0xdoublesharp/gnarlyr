#include <string.h>

#include "check.h"
#include "fixture_library.h"
#include "library/library_content.h"

enum {
    SECTION_TABLE = 8,
    SECTION_ENTRY_SIZE = 12,
    ENTRY_COUNT_FIELD = 4,
    ENTRY_MUSIC_FIELD = 10,
    FOLDER_FLAGS_FIELD = 14
};

static const uint32_t NO_FAILURE = UINT32_MAX;

typedef struct Probe {
    MemorySource memory;
    uint32_t fail_offset;
    uint32_t largest_read;
    uint32_t smallest_read;
    int reads;
} Probe;

static FixtureLibrary fixture;
static Probe probe;

static bool read_probe(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    Probe *source = context;
    source->reads++;
    source->largest_read = length > source->largest_read ? length : source->largest_read;
    source->smallest_read = length < source->smallest_read ? length : source->smallest_read;
    if (offset >= source->fail_offset) {
        return false;
    }
    ByteSource memory = memory_source(&source->memory);
    return memory.read(memory.context, offset, bytes, length);
}

static Library *reopen(void)
{
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&fixture.library, memory_source(&fixture.memory), fixture.size));
    return &fixture.library;
}

static Library *open_probed(uint32_t fail_offset)
{
    fixture_library_open(&fixture);
    probe =
        (Probe){.memory = fixture.memory, .fail_offset = fail_offset, .smallest_read = UINT32_MAX};
    static Library library;
    CHECK_EQUAL(LIBRARY_OK, library_open(&library, (ByteSource){read_probe, &probe}, fixture.size));
    return &library;
}

static uint32_t content_of(Library *library)
{
    uint32_t fingerprint = 0;
    CHECK(fixture_library_identify(library));
    CHECK(library_content_fingerprint(library, &fingerprint));
    return fingerprint;
}

static void every_section_is_covered_when_layout_and_counts_are_unchanged(void)
{
    int covered = 0;
    for (int section = 0; section < LIBRARY_SECTION_COUNT; section++) {
        Library *library = fixture_library_open(&fixture);
        uint32_t layout = library_fingerprint(library);
        uint32_t before = content_of(library);
        LibrarySectionInfo info = library->sections[section];
        if (info.count == 0) {
            continue;
        }
        fixture.bytes[info.offset + info.count * info.record_size - 1] ^= 1;
        library = reopen();
        CHECK_EQUAL(layout, library_fingerprint(library));
        CHECK(before != content_of(library));
        covered++;
    }
    CHECK(covered >= LIBRARY_SECTION_COUNT - 1);
}

static void the_header_is_part_of_the_identity(void)
{
    Library *library = fixture_library_open(&fixture);
    uint32_t layout = library_fingerprint(library);
    uint32_t before = content_of(library);
    fixture.bytes[SECTION_TABLE + LIBRARY_ALBUMS * SECTION_ENTRY_SIZE + ENTRY_MUSIC_FIELD] ^= 1;
    library = reopen();
    CHECK_EQUAL(layout, library_fingerprint(library));
    CHECK(before != content_of(library));
}

static void a_chapter_folder_flag_changes_the_identity(void)
{
    CHECK_EQUAL(6, LIBRARY_VERSION);
    Library *library = fixture_library_open(&fixture);
    uint32_t before = content_of(library);
    LibrarySectionInfo folders = library->sections[LIBRARY_FOLDERS];
    LibraryFolder folder;
    CHECK(library_read_folder(library, 1, &folder));
    CHECK_EQUAL(0, folder.flags);
    fixture.bytes[folders.offset + folders.record_size + FOLDER_FLAGS_FIELD] =
        LIBRARY_FOLDER_SHEET | LIBRARY_FOLDER_CHAPTERS;
    library = reopen();
    CHECK(library_read_folder(library, 1, &folder));
    CHECK_EQUAL(LIBRARY_FOLDER_SHEET | LIBRARY_FOLDER_CHAPTERS, folder.flags);
    CHECK(before != content_of(library));
}

static void the_fingerprint_is_unknown_until_the_last_block_and_then_costs_no_reads(void)
{
    Library *library = open_probed(NO_FAILURE);
    uint32_t fingerprint = 1;
    CHECK(!library_content_fingerprint(library, &fingerprint));
    CHECK_EQUAL(0, fingerprint);
    LibraryContent job;
    library_content_begin(&job, library);
    probe.reads = 0;
    int steps = 0;
    while (library_content_step(&job) == LIBRARY_CONTENT_WORKING) {
        CHECK_EQUAL(++steps, probe.reads);
        CHECK(!library_content_fingerprint(library, &fingerprint));
    }
    CHECK_EQUAL(LIBRARY_CONTENT_READY, job.state);
    CHECK_EQUAL(steps + 1, probe.reads);
    CHECK(steps >= 1);
    CHECK(probe.largest_read <= LIBRARY_CONTENT_BLOCK_BYTES);
    CHECK(probe.smallest_read > 0);
    for (int repeat = 0; repeat < 100; repeat++) {
        uint32_t again = 0;
        CHECK(library_content_fingerprint(library, &again));
        CHECK_EQUAL(job.hash, again);
    }
    CHECK_EQUAL(LIBRARY_CONTENT_READY, library_content_step(&job));
    CHECK_EQUAL(steps + 1, probe.reads);
}

static void the_same_database_has_the_same_fingerprint_after_reopening(void)
{
    static FixtureLibrary other;
    uint32_t first = content_of(fixture_library_open(&fixture));
    CHECK_EQUAL(first, content_of(fixture_library_open(&other)));
    Library *library = reopen();
    uint32_t forgotten = 1;
    CHECK(!library_content_fingerprint(library, &forgotten));
    CHECK_EQUAL(first, content_of(library));
}

static void reserved_file_capacity_does_not_change_the_identity(void)
{
    Library *library = fixture_library_open(&fixture);
    uint32_t first = content_of(library);
    memset(fixture.bytes + fixture.size, 0xa5, sizeof fixture.bytes - fixture.size);
    fixture.memory.size = sizeof fixture.bytes;
    CHECK_EQUAL(LIBRARY_OK,
                library_open(library, memory_source(&fixture.memory), sizeof fixture.bytes));
    CHECK_EQUAL(first, content_of(library));
}

static void an_empty_section_is_skipped_without_a_read(void)
{
    fixture_library_open(&fixture);
    uint32_t entry = SECTION_TABLE + LIBRARY_PLAYLISTS * SECTION_ENTRY_SIZE + ENTRY_COUNT_FIELD;
    memset(fixture.bytes + entry, 0, 4);
    probe =
        (Probe){.memory = fixture.memory, .fail_offset = NO_FAILURE, .smallest_read = UINT32_MAX};
    Library library;
    CHECK_EQUAL(LIBRARY_OK, library_open(&library, (ByteSource){read_probe, &probe}, fixture.size));
    CHECK_EQUAL(0, library_count(&library, LIBRARY_PLAYLISTS));
    CHECK(fixture_library_identify(&library));
    CHECK(probe.smallest_read > 0);
}

static void a_read_failure_does_not_publish_a_partial_identity(void)
{
    fixture_library_open(&fixture);
    Library *library = open_probed(fixture.library.sections[LIBRARY_STRINGS].offset);
    uint32_t fingerprint = 1;
    LibraryContent job;
    library_content_begin(&job, library);
    while (library_content_step(&job) == LIBRARY_CONTENT_WORKING) {
    }
    CHECK_EQUAL(LIBRARY_CONTENT_FAILED, job.state);
    CHECK_EQUAL(LIBRARY_CONTENT_FAILED, library_content_step(&job));
    CHECK(!library_content_fingerprint(library, &fingerprint));
    CHECK_EQUAL(0, fingerprint);
    CHECK(probe.largest_read <= LIBRARY_CONTENT_BLOCK_BYTES);
}

static void a_new_pass_forgets_the_previous_fingerprint_until_it_finishes(void)
{
    Library *library = fixture_library_open(&fixture);
    uint32_t first = content_of(library);
    uint32_t fingerprint = 0;
    LibraryContent job;
    library_content_begin(&job, library);
    CHECK(!library_content_fingerprint(library, &fingerprint));
    CHECK_EQUAL(LIBRARY_CONTENT_WORKING, library_content_step(&job));
    CHECK(!library_content_fingerprint(library, &fingerprint));
    CHECK_EQUAL(first, content_of(library));
}

static void a_library_that_did_not_open_has_no_identity(void)
{
    fixture_library_open(&fixture);
    fixture.bytes[0] = 'X';
    Library library;
    CHECK_EQUAL(LIBRARY_BAD_MAGIC,
                library_open(&library, memory_source(&fixture.memory), fixture.size));
    uint32_t fingerprint = 1;
    CHECK(!fixture_library_identify(&library));
    CHECK(!library_content_fingerprint(&library, &fingerprint));
    int reads = fixture.memory.reads;
    LibraryContent idle = {0};
    CHECK_EQUAL(LIBRARY_CONTENT_IDLE, library_content_step(&idle));
    CHECK_EQUAL(reads, fixture.memory.reads);
}

static void an_extra_playlist_in_memory_is_not_part_of_the_identity(void)
{
    static const LibraryExtraPlaylist EXTRA = {.name = "Favorites", .count = 2, .tracks = {1, 3}};
    Library *library = fixture_library_open(&fixture);
    uint32_t first = content_of(library);
    LibraryExtraPlaylist extra = EXTRA;
    extra.playlist = (uint16_t)library_count(library, LIBRARY_PLAYLISTS);
    library->extra = &extra;
    CHECK_EQUAL(extra.playlist + 1u, library_count(library, LIBRARY_PLAYLISTS));
    CHECK_EQUAL(first, content_of(library));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(every_section_is_covered_when_layout_and_counts_are_unchanged),
        TEST_CASE(the_header_is_part_of_the_identity),
        TEST_CASE(a_chapter_folder_flag_changes_the_identity),
        TEST_CASE(the_fingerprint_is_unknown_until_the_last_block_and_then_costs_no_reads),
        TEST_CASE(the_same_database_has_the_same_fingerprint_after_reopening),
        TEST_CASE(reserved_file_capacity_does_not_change_the_identity),
        TEST_CASE(an_empty_section_is_skipped_without_a_read),
        TEST_CASE(a_read_failure_does_not_publish_a_partial_identity),
        TEST_CASE(a_new_pass_forgets_the_previous_fingerprint_until_it_finishes),
        TEST_CASE(a_library_that_did_not_open_has_no_identity),
        TEST_CASE(an_extra_playlist_in_memory_is_not_part_of_the_identity),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
