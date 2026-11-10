#include <string.h>

#include "check.h"
#include "fixture_library.h"
#include "listening_fixture.h"
#include "listening_store.h"

static FixtureLibrary fixture;
static ListeningDisk disk;
static ListeningStore store;

static void reset(void)
{
    listening_disk_reset(&disk);
    listening_store_attach(&store, fixture_library_open(&fixture),
                           listening_disk_file(&disk, sizeof disk.bytes));
}

static ListeningObservation observed(uint32_t token, uint32_t position, PlayerState state)
{
    return (ListeningObservation){
        .status = {.state = state,
                   .has_track = true,
                   .track = 0,
                   .position_ms = position,
                   .duration_ms = 60000,
                   .format = {.codec = HAL_AUDIO_CODEC_FLAC,
                              .sample_rate = 44100,
                              .bits_per_sample = 16}},
        .token = token,
        .output = LISTENING_OUTPUT_SINGLE_ENDED,
    };
}

static void paused_time_and_seek_distance_are_never_counted_as_listening(void)
{
    reset();
    listening_store_follow(&store, observed(10, 1000, PLAYER_PLAYING), 1000);
    listening_store_follow(&store, observed(10, 2000, PLAYER_PLAYING), 2000);
    listening_store_follow(&store, observed(10, 3000, PLAYER_PAUSED), 3000);
    listening_store_follow(&store, observed(10, 3000, PLAYER_PAUSED), 9000);
    listening_store_follow(&store, observed(10, 3000, PLAYER_PLAYING), 10000);
    listening_store_follow(&store, observed(10, 4000, PLAYER_PLAYING), 11000);
    listening_store_follow(&store, observed(10, 44000, PLAYER_PLAYING), 12000);
    listening_store_follow(&store, observed(10, 5000, PLAYER_PLAYING), 13000);
    listening_store_flush(&store, observed(10, 6000, PLAYER_PAUSED), 14000);
    CHECK_EQUAL(1, store.log.root.counters.totals.plays);
    CHECK_EQUAL(6000, store.log.root.counters.totals.listened_ms);
    ListeningSeen seen;
    CHECK_EQUAL(0, listening_fixture_read_all(&store.log, &seen));
    CHECK_EQUAL(1, seen.count);
    CHECK_EQUAL(0, seen.first.play.clock_s);
    CHECK_EQUAL(1000, seen.first.play.start_ms);
    CHECK_EQUAL(6000, seen.first.play.end_ms);
}

static void repeating_the_same_track_starts_a_distinct_play_without_a_skip(void)
{
    reset();
    ListeningObservation play = observed(30, 0, PLAYER_PLAYING);
    play.status.duration_ms = 6000;
    listening_store_follow(&store, play, 0);
    play.status.position_ms = 5000;
    listening_store_follow(&store, play, 5000);
    play.status.position_ms = 0;
    play.token = 31;
    listening_store_follow(&store, play, 6000);
    play.status.position_ms = 500;
    listening_store_flush(&store, play, 6500);
    CHECK_EQUAL(2, store.log.root.counters.totals.plays);
    CHECK_EQUAL(6500, store.log.root.counters.totals.listened_ms);
    CHECK_EQUAL(0, store.log.root.counters.totals.skips);
    ListeningSeen seen;
    listening_fixture_read_all(&store.log, &seen);
    CHECK_EQUAL(LISTENING_FINISHED, seen.first.play.end);
    CHECK_EQUAL(LISTENING_STOPPED, seen.last.play.end);
}

static void the_last_song_finishing_is_not_reported_as_stopped(void)
{
    reset();
    ListeningObservation play = observed(30, 0, PLAYER_PLAYING);
    play.status.duration_ms = 6000;
    listening_store_follow(&store, play, 0);
    play.status.position_ms = 5000;
    listening_store_follow(&store, play, 5000);
    listening_store_follow(&store, (ListeningObservation){0}, 6000);
    ListeningSeen seen;
    listening_fixture_read_all(&store.log, &seen);
    CHECK_EQUAL(1, seen.count);
    CHECK_EQUAL(6000, seen.listened_ms);
    CHECK_EQUAL(LISTENING_FINISHED, seen.first.play.end);
}

static void a_failed_write_is_retried_once_without_duplicate_counts(void)
{
    reset();
    listening_store_follow(&store, observed(1, 0, PLAYER_PLAYING), 0);
    disk.cut_write = disk.writes + 1;
    disk.cut_bytes = 0;
    listening_store_flush(&store, observed(1, 1000, PLAYER_PAUSED), 1000);
    CHECK(store.waiting);
    CHECK_EQUAL(0, store.log.root.counters.totals.plays);
    disk.cut_write = 0;
    listening_store_follow(&store, (ListeningObservation){0}, 2100);
    CHECK(!store.waiting);
    CHECK_EQUAL(1, store.log.root.counters.totals.plays);
    CHECK_EQUAL(1000, store.log.root.counters.totals.listened_ms);
    listening_store_follow(&store, (ListeningObservation){0}, 4000);
    CHECK_EQUAL(1, store.log.root.counters.totals.plays);
}

static void an_uncertain_commit_is_reloaded_before_a_retry(void)
{
    reset();
    listening_store_follow(&store, observed(1, 0, PLAYER_PLAYING), 0);
    disk.blind_from_write = disk.writes + 2;
    listening_store_flush(&store, observed(1, 1000, PLAYER_PAUSED), 1000);
    CHECK(store.waiting);
    CHECK(store.log.needs_reload);
    disk.blind_from_write = 0;
    listening_store_follow(&store, (ListeningObservation){0}, 2100);
    CHECK(!store.waiting);
    CHECK_EQUAL(1, store.log.root.counters.totals.plays);
    CHECK_EQUAL(1000, store.log.root.counters.totals.listened_ms);
    CHECK_EQUAL(1, store.log.session.plays);
}

static void export_lookup_rejects_an_identity_that_now_names_another_track(void)
{
    reset();
    ListeningTrack identity;
    CHECK_EQUAL(STORE_READY, listening_track_read(store.library, 0, &identity));
    ListeningText text;
    CHECK(listening_store_text(&store, &identity, &text));
    identity.key ^= 1;
    CHECK(!listening_store_text(&store, &identity, &text));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(paused_time_and_seek_distance_are_never_counted_as_listening),
        TEST_CASE(repeating_the_same_track_starts_a_distinct_play_without_a_skip),
        TEST_CASE(the_last_song_finishing_is_not_reported_as_stopped),
        TEST_CASE(a_failed_write_is_retried_once_without_duplicate_counts),
        TEST_CASE(an_uncertain_commit_is_reloaded_before_a_retry),
        TEST_CASE(export_lookup_rejects_an_identity_that_now_names_another_track),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
