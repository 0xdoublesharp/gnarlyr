#include <string.h>

#include "big_library.h"
#include "check.h"
#include "core/hash.h"
#include "fixture_library.h"
#include "keys_disk.h"
#include "playback/mix_identity.h"
#include "playback/track_keys.h"

enum {
    WORK_ENTRIES = 4096,
    TABLE_ENTRIES = 2048,
    TRACKS_PER_ALBUM = 10,
    NOT_LISTED = 0x7FFFFFFF
};

static KeysDisk disk;
static TrackKeys keys;
static TrackKeysUpdate job;
static TrackKey work[WORK_ENTRIES];
static TrackKey table[TABLE_ENTRIES];
static TrackKey other[TABLE_ENTRIES];
static FixtureLibrary fixture;

static void start(void)
{
    keys_disk_reset(&disk);
    track_keys_open(&keys, keys_disk_file(&disk));
}

static StoreResult update_with(const Library *library, uint32_t capacity)
{
    CHECK(track_keys_update_begin(&job, &keys, library, (TrackKeysWork){work, capacity}));
    StoreResult result = STORE_WORKING;
    for (int step = 0; step < 200000 && result == STORE_WORKING; step++) {
        result = track_keys_update_step(&job);
    }
    return result;
}

static StoreResult update(const Library *library)
{
    return update_with(library, WORK_ENTRIES);
}

static uint32_t read_table(TrackKey *entries)
{
    CHECK(keys.count <= TABLE_ENTRIES);
    CHECK(track_keys_read(&keys, 0, keys.count, entries));
    return keys.count;
}

static uint32_t key_of(const Library *library, uint16_t track)
{
    MixIdentity identity;
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, track, &identity));
    return mix_identity_hash(&identity);
}

static uint32_t arrival_of(const TrackKey *entries, uint32_t count, uint32_t key)
{
    for (uint32_t index = 0; index < count; index++) {
        if (entries[index].key == key) {
            return entries[index].arrival;
        }
    }
    return NOT_LISTED;
}

static bool in_order(const TrackKey *entries, uint32_t count)
{
    for (uint32_t index = 1; index < count; index++) {
        if (entries[index - 1].key > entries[index].key ||
            (entries[index - 1].key == entries[index].key &&
             entries[index - 1].track >= entries[index].track)) {
            return false;
        }
    }
    return true;
}

static void check_table_matches(const Library *library)
{
    uint32_t count = read_table(table);
    CHECK_EQUAL(library_music_count(library, LIBRARY_TRACKS), count);
    CHECK(in_order(table, count));
    for (uint32_t index = 0; index < count; index++) {
        CHECK_EQUAL(key_of(library, table[index].track), table[index].key);
    }
}

static void the_first_update_records_every_song_as_the_starting_point(void)
{
    start();
    Library *library = fixture_library_open(&fixture);
    CHECK(!track_keys_current(&keys, library));
    CHECK_EQUAL(STORE_DONE, update(library));
    CHECK(track_keys_current(&keys, library));
    CHECK_EQUAL(0, keys.latest);
    check_table_matches(library);
    for (uint32_t index = 0; index < keys.count; index++) {
        CHECK_EQUAL(0, table[index].arrival);
    }
}

static void the_table_is_found_again_after_reopening(void)
{
    start();
    Library *library = fixture_library_open(&fixture);
    CHECK_EQUAL(STORE_DONE, update(library));
    uint32_t generation = keys.generation;
    CHECK_EQUAL(STORE_READY, track_keys_open(&keys, keys_disk_file(&disk)));
    CHECK(track_keys_current(&keys, library));
    CHECK_EQUAL(generation, keys.generation);
    check_table_matches(library);
}

static void another_library_makes_the_table_out_of_date(void)
{
    BigLibrary big;
    start();
    CHECK_EQUAL(STORE_DONE, update(fixture_library_open(&fixture)));
    Library *library = big_library_open(&big, 30, TRACKS_PER_ALBUM);
    CHECK(!track_keys_current(&keys, library));
    CHECK_EQUAL(STORE_DONE, update(library));
    CHECK(track_keys_current(&keys, library));
    CHECK(!track_keys_current(&keys, fixture_library_open(&fixture)));
    big_library_close(&big);
}

static void check_arrivals(const Library *library, uint32_t from, uint32_t to, uint32_t arrival)
{
    uint32_t count = read_table(table);
    for (uint32_t track = from; track < to; track++) {
        CHECK_EQUAL(arrival, arrival_of(table, count, key_of(library, (uint16_t)track)));
    }
}

static void songs_that_appear_later_get_the_next_arrival(void)
{
    BigLibrary big;
    start();
    CHECK_EQUAL(STORE_DONE, update(big_library_open(&big, 300, TRACKS_PER_ALBUM)));
    big_library_close(&big);
    Library *library = big_library_open(&big, 340, TRACKS_PER_ALBUM);
    CHECK_EQUAL(STORE_DONE, update(library));
    CHECK_EQUAL(1, keys.latest);
    check_table_matches(library);
    check_arrivals(library, 0, 300, 0);
    check_arrivals(library, 300, 340, 1);
    big_library_close(&big);
    library = big_library_open(&big, 360, TRACKS_PER_ALBUM);
    CHECK_EQUAL(STORE_DONE, update(library));
    CHECK_EQUAL(2, keys.latest);
    check_arrivals(library, 0, 300, 0);
    check_arrivals(library, 300, 340, 1);
    check_arrivals(library, 340, 360, 2);
    big_library_close(&big);
}

static void removing_songs_keeps_the_latest_arrival_and_a_returning_song_is_new(void)
{
    BigLibrary big;
    start();
    CHECK_EQUAL(STORE_DONE, update(big_library_open(&big, 340, TRACKS_PER_ALBUM)));
    big_library_close(&big);
    Library *library = big_library_open(&big, 300, TRACKS_PER_ALBUM);
    CHECK_EQUAL(STORE_DONE, update(library));
    CHECK_EQUAL(0, keys.latest);
    CHECK_EQUAL(300, keys.count);
    big_library_close(&big);
    library = big_library_open(&big, 340, TRACKS_PER_ALBUM);
    CHECK_EQUAL(STORE_DONE, update(library));
    CHECK_EQUAL(1, keys.latest);
    check_arrivals(library, 0, 300, 0);
    check_arrivals(library, 300, 340, 1);
    big_library_close(&big);
}

static void a_small_work_buffer_merges_many_runs_into_the_same_table(void)
{
    BigLibrary big;
    Library *library = big_library_open(&big, 1000, TRACKS_PER_ALBUM);
    start();
    CHECK_EQUAL(STORE_DONE, update_with(library, 32));
    uint32_t count = read_table(other);
    start();
    CHECK_EQUAL(STORE_DONE, update(library));
    CHECK_EQUAL(count, read_table(table));
    CHECK_EQUAL(0, memcmp(table, other, count * sizeof table[0]));
    check_table_matches(library);
    big_library_close(&big);
}

static void a_work_buffer_too_small_for_the_library_is_refused(void)
{
    BigLibrary big;
    Library *library = big_library_open(&big, 1000, TRACKS_PER_ALBUM);
    start();
    CHECK(!track_keys_update_begin(&job, &keys, library, (TrackKeysWork){work, 8}));
    CHECK(!track_keys_update_begin(&job, &keys, library, (TrackKeysWork){work, 0}));
    CHECK(!track_keys_update_begin(&job, &keys, library, (TrackKeysWork){NULL, 64}));
    big_library_close(&big);
}

static void a_failed_write_keeps_the_published_table(void)
{
    BigLibrary big;
    start();
    Library *first = fixture_library_open(&fixture);
    CHECK_EQUAL(STORE_DONE, update(first));
    Library *library = big_library_open(&big, 300, TRACKS_PER_ALBUM);
    int cut = 1;
    for (; cut < 100; cut++) {
        disk.cut_write = disk.writes + cut;
        if (update_with(library, 64) == STORE_DONE) {
            break;
        }
        CHECK_EQUAL(STORE_READY, track_keys_open(&keys, keys_disk_file(&disk)));
        CHECK(track_keys_current(&keys, first));
    }
    CHECK(cut > 8 && cut < 100);
    CHECK(track_keys_current(&keys, library));
    big_library_close(&big);
}

static void an_abandoned_update_keeps_the_published_table(void)
{
    BigLibrary big;
    start();
    Library *first = fixture_library_open(&fixture);
    CHECK_EQUAL(STORE_DONE, update(first));
    Library *library = big_library_open(&big, 300, TRACKS_PER_ALBUM);
    for (int steps = 1; steps < 46; steps += 6) {
        CHECK(track_keys_update_begin(&job, &keys, library, (TrackKeysWork){work, 64}));
        for (int step = 0; step < steps; step++) {
            CHECK_EQUAL(STORE_WORKING, track_keys_update_step(&job));
        }
        track_keys_update_abort(&job);
        CHECK_EQUAL(STORE_FAILED, track_keys_update_step(&job));
        CHECK_EQUAL(STORE_READY, track_keys_open(&keys, keys_disk_file(&disk)));
        CHECK(track_keys_current(&keys, first));
    }
    big_library_close(&big);
}

static void the_parts_of_a_cue_file_get_their_own_entries(void)
{
    start();
    Library *library = fixture_cue_library_open(&fixture);
    CHECK_EQUAL(STORE_DONE, update(library));
    check_table_matches(library);
    for (uint32_t index = 1; index < keys.count; index++) {
        CHECK(table[index - 1].key != table[index].key);
    }
}

static void progress_rises_to_one_hundred(void)
{
    BigLibrary big;
    Library *library = big_library_open(&big, 300, TRACKS_PER_ALBUM);
    start();
    CHECK(track_keys_update_begin(&job, &keys, library, (TrackKeysWork){work, 64}));
    uint8_t previous = 0;
    while (track_keys_update_step(&job) == STORE_WORKING) {
        uint8_t progress = track_keys_update_progress(&job);
        CHECK(progress >= previous && progress < 100);
        previous = progress;
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    CHECK_EQUAL(100, track_keys_update_progress(&job));
    big_library_close(&big);
}

static void a_missing_or_short_file_is_not_updated(void)
{
    StoreFile file = keys_disk_file(&disk);
    file.size = TRACK_KEYS_FILE_BYTES - 1;
    keys_disk_reset(&disk);
    CHECK_EQUAL(STORE_UNREADABLE, track_keys_open(&keys, file));
    CHECK(!track_keys_update_begin(&job, &keys, fixture_library_open(&fixture),
                                   (TrackKeysWork){work, WORK_ENTRIES}));
    file.size = TRACK_KEYS_FILE_BYTES;
    file.source.read = NULL;
    CHECK_EQUAL(STORE_UNREADABLE, track_keys_open(&keys, file));
}

static void a_later_version_is_never_overwritten(void)
{
    start();
    CHECK_EQUAL(STORE_DONE, update(fixture_library_open(&fixture)));
    store_put16(disk.bytes + 4, 2);
    store_put32(disk.bytes + 24, hash_update(HASH_START, disk.bytes, 24));
    CHECK_EQUAL(STORE_UNSUPPORTED, track_keys_open(&keys, keys_disk_file(&disk)));
    CHECK(!track_keys_update_begin(&job, &keys, fixture_library_open(&fixture),
                                   (TrackKeysWork){work, WORK_ENTRIES}));
}

static void malformed_entry_counts_are_rejected_before_payload_reads(void)
{
    static const uint32_t bytes[] = {1, TRACK_KEYS_CAPACITY * TRACK_KEYS_ENTRY_BYTES};
    for (uint32_t index = 0; index < sizeof bytes / sizeof bytes[0]; index++) {
        keys_disk_reset(&disk);
        keys_disk_publish(&disk, NULL, 0, 0);
        store_put32(disk.bytes + 12, bytes[index]);
        store_put32(disk.bytes + 24, hash_update(HASH_START, disk.bytes, 24));
        CHECK_EQUAL(STORE_CORRUPT, track_keys_open(&keys, keys_disk_file(&disk)));
    }
}

static void a_read_failure_fails_the_update(void)
{
    BigLibrary big;
    start();
    CHECK_EQUAL(STORE_DONE, update(fixture_library_open(&fixture)));
    Library *library = big_library_open(&big, 300, TRACKS_PER_ALBUM);
    disk.refuse_reads_after = disk.reads + 3;
    CHECK_EQUAL(STORE_FAILED, update_with(library, 64));
    big_library_close(&big);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_first_update_records_every_song_as_the_starting_point),
        TEST_CASE(the_table_is_found_again_after_reopening),
        TEST_CASE(another_library_makes_the_table_out_of_date),
        TEST_CASE(songs_that_appear_later_get_the_next_arrival),
        TEST_CASE(removing_songs_keeps_the_latest_arrival_and_a_returning_song_is_new),
        TEST_CASE(a_small_work_buffer_merges_many_runs_into_the_same_table),
        TEST_CASE(a_work_buffer_too_small_for_the_library_is_refused),
        TEST_CASE(a_failed_write_keeps_the_published_table),
        TEST_CASE(an_abandoned_update_keeps_the_published_table),
        TEST_CASE(the_parts_of_a_cue_file_get_their_own_entries),
        TEST_CASE(progress_rises_to_one_hundred),
        TEST_CASE(a_missing_or_short_file_is_not_updated),
        TEST_CASE(a_later_version_is_never_overwritten),
        TEST_CASE(malformed_entry_counts_are_rejected_before_payload_reads),
        TEST_CASE(a_read_failure_fails_the_update),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
