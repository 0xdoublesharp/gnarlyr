#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "keys_disk.h"
#include "listening_fixture.h"
#include "playback/smart_list.h"

enum {
    SONGS = 400,
    TALLIES = 512,
    LENGTH_MS = 200000,
    UNLISTED = 9000
};

static KeysDisk keys_disk;
static ListeningDisk log_disk;
static TrackKeys keys;
static ListeningLog log_book;
static SmartList job;
static SmartTally tallies[TALLIES];
static TrackKey entries[SONGS + 2];
static uint16_t arrivals[SONGS];
static SmartFavoriteTrack favorite_tracks[8];
static SmartFavoriteAlbum favorite_albums[4];
static SmartFavorites favorites;

static uint32_t key_of(uint32_t song)
{
    return (song + 1) * UINT32_C(2654435761);
}

static int by_key(const void *first, const void *second)
{
    const TrackKey *one = first;
    const TrackKey *other = second;
    if (one->key != other->key) {
        return one->key < other->key ? -1 : 1;
    }
    return one->track < other->track ? -1 : one->track > other->track;
}

static void publish(uint32_t count)
{
    qsort(entries, count, sizeof entries[0], by_key);
    keys_disk_publish(&keys_disk, entries, count, 0);
    CHECK_EQUAL(STORE_READY, track_keys_open(&keys, keys_disk_file(&keys_disk)));
}

static void start(uint32_t songs)
{
    keys_disk_reset(&keys_disk);
    listening_disk_reset(&log_disk);
    listening_log_open(&log_book, listening_disk_config(&log_disk));
    for (uint32_t song = 0; song < songs; song++) {
        entries[song] = (TrackKey){key_of(song), (uint16_t)song, arrivals[song]};
    }
    publish(songs);
    favorites = (SmartFavorites){0};
}

static void play_key(uint32_t key, uint32_t listened_ms)
{
    ListeningPlay play = {.track = {.key = key, .index = 0},
                          .length_ms = LENGTH_MS,
                          .listened_ms = listened_ms,
                          .end_ms = listened_ms,
                          .end = listened_ms >= LENGTH_MS ? LISTENING_FINISHED : LISTENING_SKIPPED,
                          .output = LISTENING_OUTPUT_SINGLE_ENDED,
                          .format = LISTENING_FORMAT_LOSSLESS};
    CHECK_EQUAL(STORE_DONE, listening_log_append(&log_book, &play));
}

static void listen(uint32_t song)
{
    play_key(key_of(song), LENGTH_MS);
}

static void skip(uint32_t song)
{
    play_key(key_of(song), 1000);
}

static StoreResult run_with(SmartListKind kind, uint16_t capacity)
{
    SmartListConfig config = {.kind = kind,
                              .log = &log_book,
                              .keys = &keys,
                              .favorites = favorites,
                              .scratch = {tallies, capacity}};
    CHECK(smart_list_begin(&job, config));
    StoreResult result = STORE_WORKING;
    for (int step = 0; step < 100000 && result == STORE_WORKING; step++) {
        result = smart_list_step(&job);
    }
    return result;
}

static void expect(SmartListKind kind, const uint16_t *tracks, uint16_t count)
{
    CHECK_EQUAL(STORE_DONE, run_with(kind, TALLIES));
    CHECK_EQUAL(count, job.count);
    for (uint16_t position = 0; position < count && position < job.count; position++) {
        CHECK_EQUAL(tracks[position], smart_list_track(&job, position));
    }
}

static void recently_played_lists_distinct_songs_newest_first(void)
{
    start(10);
    listen(1);
    listen(2);
    listen(3);
    listen(2);
    listen(4);
    static const uint16_t expected[] = {4, 2, 3, 1};
    expect(SMART_LIST_RECENTLY_PLAYED, expected, 4);
}

static void recently_played_counts_skipped_plays(void)
{
    start(10);
    listen(1);
    skip(5);
    static const uint16_t expected[] = {5, 1};
    expect(SMART_LIST_RECENTLY_PLAYED, expected, 2);
}

static void a_list_stops_at_one_hundred_songs(void)
{
    start(150);
    for (uint32_t song = 0; song < 150; song++) {
        listen(song);
    }
    CHECK_EQUAL(STORE_DONE, run_with(SMART_LIST_RECENTLY_PLAYED, TALLIES));
    CHECK_EQUAL(SMART_LIST_CAPACITY, job.count);
    for (uint16_t position = 0; position < SMART_LIST_CAPACITY; position++) {
        CHECK_EQUAL(149 - position, smart_list_track(&job, position));
    }
    CHECK_EQUAL(LIBRARY_NONE, smart_list_track(&job, SMART_LIST_CAPACITY));
}

static void most_played_ranks_by_listens_then_the_latest_play(void)
{
    start(10);
    listen(1);
    listen(1);
    listen(1);
    listen(2);
    skip(2);
    skip(2);
    skip(2);
    listen(2);
    listen(3);
    listen(3);
    skip(4);
    skip(4);
    static const uint16_t expected[] = {1, 3, 2};
    expect(SMART_LIST_MOST_PLAYED, expected, 3);
}

static void never_played_puts_the_newest_arrivals_first_then_library_order(void)
{
    memset(arrivals, 0, sizeof arrivals);
    arrivals[3] = 2;
    arrivals[4] = 2;
    arrivals[7] = 1;
    start(10);
    listen(4);
    skip(1);
    static const uint16_t expected[] = {3, 7, 0, 2, 5, 6, 8, 9};
    expect(SMART_LIST_NEVER_PLAYED, expected, 8);
    memset(arrivals, 0, sizeof arrivals);
}

static void recently_added_lists_arrivals_newest_first_without_the_starting_point(void)
{
    memset(arrivals, 0, sizeof arrivals);
    arrivals[3] = 2;
    arrivals[4] = 2;
    arrivals[7] = 1;
    start(10);
    listen(4);
    static const uint16_t expected[] = {3, 4, 7};
    expect(SMART_LIST_RECENTLY_ADDED, expected, 3);
    memset(arrivals, 0, sizeof arrivals);
    start(10);
    expect(SMART_LIST_RECENTLY_ADDED, NULL, 0);
}

static void an_empty_history_leaves_only_never_played_and_recently_added(void)
{
    start(5);
    expect(SMART_LIST_RECENTLY_PLAYED, NULL, 0);
    expect(SMART_LIST_MOST_PLAYED, NULL, 0);
    expect(SMART_LIST_FORGOTTEN, NULL, 0);
    static const uint16_t all[] = {0, 1, 2, 3, 4};
    expect(SMART_LIST_NEVER_PLAYED, all, 5);
}

static void use_favorites(void)
{
    favorite_tracks[0] = (SmartFavoriteTrack){2, 0};
    favorite_tracks[1] = (SmartFavoriteTrack){5, 1};
    favorite_tracks[2] = (SmartFavoriteTrack){9, 2};
    favorite_albums[0] = (SmartFavoriteAlbum){10, 3, 256};
    favorites = (SmartFavorites){favorite_tracks, 3, favorite_albums, 1};
}

static void forgotten_favorites_put_the_longest_unplayed_first_then_never_played(void)
{
    start(30);
    use_favorites();
    listen(2);
    listen(10);
    for (int play = 0; play < SMART_LIST_FORGOTTEN_WINDOW; play++) {
        listen(20);
    }
    listen(5);
    static const uint16_t expected[] = {2, 10, 9, 11, 12};
    expect(SMART_LIST_FORGOTTEN, expected, 5);
}

static void a_favorite_played_within_the_last_500_plays_is_not_forgotten(void)
{
    start(30);
    use_favorites();
    listen(2);
    for (int play = 0; play < SMART_LIST_FORGOTTEN_WINDOW - 1; play++) {
        listen(20);
    }
    static const uint16_t recent[] = {5, 9, 10, 11, 12};
    expect(SMART_LIST_FORGOTTEN, recent, 5);
    listen(20);
    static const uint16_t forgotten[] = {2, 5, 9, 10, 11, 12};
    expect(SMART_LIST_FORGOTTEN, forgotten, 6);
}

static void plays_of_songs_no_longer_in_the_library_are_left_out(void)
{
    start(10);
    listen(3);
    for (uint32_t song = 0; song < 150; song++) {
        play_key(key_of(UNLISTED + song), LENGTH_MS);
    }
    listen(1);
    play_key(key_of(UNLISTED), LENGTH_MS);
    static const uint16_t recent[] = {1, 3};
    expect(SMART_LIST_RECENTLY_PLAYED, recent, 2);
    expect(SMART_LIST_MOST_PLAYED, recent, 2);
}

static void a_song_listed_twice_in_the_library_appears_once_with_its_first_index(void)
{
    start(4);
    entries[4] = (TrackKey){key_of(2), 7, 0};
    publish(5);
    listen(2);
    static const uint16_t recent[] = {2};
    expect(SMART_LIST_RECENTLY_PLAYED, recent, 1);
    static const uint16_t never[] = {0, 1, 3};
    expect(SMART_LIST_NEVER_PLAYED, never, 3);
}

static void fill_history(void)
{
    uint32_t seed = 7;
    for (int play = 0; play < 1500; play++) {
        seed = seed * 1103515245u + 12345u;
        uint32_t song = (seed >> 8) % 300;
        if ((seed >> 4) % 3 == 0) {
            skip(song);
        } else {
            listen(song);
        }
    }
}

static void small_tally_tables_give_the_same_lists(void)
{
    static uint16_t reference[SMART_LIST_CAPACITY];
    start(SONGS);
    use_favorites();
    favorite_albums[0] = (SmartFavoriteAlbum){100, 150, 256};
    fill_history();
    for (int kind = 0; kind < SMART_LIST_KIND_COUNT; kind++) {
        CHECK_EQUAL(STORE_DONE, run_with((SmartListKind)kind, TALLIES));
        uint16_t count = job.count;
        for (uint16_t position = 0; position < count; position++) {
            reference[position] = smart_list_track(&job, position);
        }
        static const uint16_t capacities[] = {1, 7, 64};
        for (int size = 0; size < 3; size++) {
            CHECK_EQUAL(STORE_DONE, run_with((SmartListKind)kind, capacities[size]));
            CHECK_EQUAL(count, job.count);
            for (uint16_t position = 0; position < count; position++) {
                CHECK_EQUAL(reference[position], smart_list_track(&job, position));
            }
        }
    }
}

static void progress_rises_to_one_hundred(void)
{
    start(SONGS);
    fill_history();
    SmartListConfig config = {
        .kind = SMART_LIST_MOST_PLAYED, .log = &log_book, .keys = &keys, .scratch = {tallies, 16}};
    CHECK(smart_list_begin(&job, config));
    uint8_t previous = 0;
    while (smart_list_step(&job) == STORE_WORKING) {
        CHECK(job.percent >= previous && job.percent < 100);
        previous = job.percent;
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    CHECK_EQUAL(100, job.percent);
    CHECK(previous > 50);
}

static void a_failed_read_fails_the_list(void)
{
    start(10);
    listen(1);
    log_disk.refuse_reads = true;
    CHECK_EQUAL(STORE_FAILED, run_with(SMART_LIST_RECENTLY_PLAYED, TALLIES));
    log_disk.refuse_reads = false;
    keys_disk.refuse_reads_after = keys_disk.reads;
    CHECK_EQUAL(STORE_FAILED, run_with(SMART_LIST_RECENTLY_ADDED, TALLIES));
    CHECK_EQUAL(STORE_FAILED, run_with(SMART_LIST_RECENTLY_PLAYED, TALLIES));
}

static void a_list_needs_a_table_and_history_lists_need_a_tally(void)
{
    start(3);
    SmartListConfig config = {.kind = SMART_LIST_RECENTLY_PLAYED, .log = &log_book, .keys = &keys};
    CHECK(!smart_list_begin(&job, config));
    config.kind = SMART_LIST_RECENTLY_ADDED;
    CHECK(smart_list_begin(&job, config));
    TrackKeys missing = {0};
    config.keys = &missing;
    CHECK(!smart_list_begin(&job, config));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(recently_played_lists_distinct_songs_newest_first),
        TEST_CASE(recently_played_counts_skipped_plays),
        TEST_CASE(a_list_stops_at_one_hundred_songs),
        TEST_CASE(most_played_ranks_by_listens_then_the_latest_play),
        TEST_CASE(never_played_puts_the_newest_arrivals_first_then_library_order),
        TEST_CASE(recently_added_lists_arrivals_newest_first_without_the_starting_point),
        TEST_CASE(an_empty_history_leaves_only_never_played_and_recently_added),
        TEST_CASE(forgotten_favorites_put_the_longest_unplayed_first_then_never_played),
        TEST_CASE(a_favorite_played_within_the_last_500_plays_is_not_forgotten),
        TEST_CASE(plays_of_songs_no_longer_in_the_library_are_left_out),
        TEST_CASE(a_song_listed_twice_in_the_library_appears_once_with_its_first_index),
        TEST_CASE(small_tally_tables_give_the_same_lists),
        TEST_CASE(progress_rises_to_one_hundred),
        TEST_CASE(a_failed_read_fails_the_list),
        TEST_CASE(a_list_needs_a_table_and_history_lists_need_a_tally),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
