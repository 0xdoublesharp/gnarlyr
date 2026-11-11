#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/clock.h"
#include "listening_fixture.h"
#include "listening_store.h"

static FixtureLibrary fixture;
static ListeningDisk disk;
static ListeningStore store;
static Player player;

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

static void a_play_started_with_the_clock_set_carries_its_utc_time_and_offset(void)
{
    reset();
    fake_hal_set_wall(1794482460, -480);
    listening_store_follow(&store, observed(20, 0, PLAYER_PLAYING), 1000);
    fake_hal_set_wall(1794490000, 60);
    listening_store_flush(&store, observed(20, 5000, PLAYER_PAUSED), 6000);
    fake_hal_set_wall(0, 0);
    ListeningSeen seen;
    CHECK_EQUAL(0, listening_fixture_read_all(&store.log, &seen));
    CHECK_EQUAL(1, seen.count);
    CHECK_EQUAL(1794482460, seen.first.play.clock_s);
    CHECK_EQUAL(-480, seen.first.play.offset_minutes);
}

static void setting_the_clock_dates_new_plays_without_rewriting_earlier_timestamps(void)
{
    reset();
    fake_hal_set_wall(0, 0);
    listening_store_follow(&store, observed(21, 0, PLAYER_PLAYING), 1000);
    fake_hal_set_rtc_answer((HalRtcRead){.status = HAL_RTC_OK});
    CHECK(hal_clock_set_wall((HalWallTime){1794390060u, -480, HAL_WALL_PLAYER}));
    listening_store_flush(&store, observed(21, 1000, PLAYER_PAUSED), 2000);
    listening_store_follow(&store, observed(22, 0, PLAYER_PLAYING), 3000);
    listening_store_flush(&store, observed(22, 1000, PLAYER_PAUSED), 4000);
    ListeningSeen seen;
    CHECK_EQUAL(0, listening_fixture_read_all(&store.log, &seen));
    CHECK_EQUAL(2, seen.count);
    CHECK_EQUAL(0, seen.first.play.clock_s);
    CHECK_EQUAL(1794390060u, seen.last.play.clock_s);
    CHECK_EQUAL(-480, seen.last.play.offset_minutes);
    fake_hal_set_wall(0, 0);
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

static void start_real_playback(void)
{
    fake_hal_reset();
    reset();
    player_init(&player, &fixture.library);
    BrowseQuery single = {.kind = BROWSE_SINGLE_TRACK,
                          .group = LIBRARY_ARTISTS,
                          .subject = 0,
                          .artist = LIBRARY_NONE};
    CHECK(player_play_list(&player, single, 0, 7));
    CHECK_EQUAL(PLAYER_PLAYING, player_status(&player).state);
}

static void observe_player(uint32_t now_ms)
{
    listening_store_follow(&store,
                           (ListeningObservation){.status = player_status(&player),
                                                  .token = player.current_token,
                                                  .output = LISTENING_OUTPUT_SINGLE_ENDED},
                           now_ms);
}

static void the_real_last_track_is_recorded_finished_when_the_player_becomes_idle(void)
{
    start_real_playback();
    uint32_t duration = player_status(&player).duration_ms;
    CHECK(duration > 1000);
    observe_player(0);
    fake_hal_audio_set_position(duration - 1000);
    observe_player(duration - 1000);
    fake_hal_audio_finish_track();
    player_update(&player, duration);
    CHECK_EQUAL(PLAYER_IDLE, player_status(&player).state);
    CHECK(player_status(&player).has_track);
    observe_player(duration);
    ListeningSeen seen;
    listening_fixture_read_all(&store.log, &seen);
    CHECK_EQUAL(1, seen.count);
    CHECK_EQUAL(LISTENING_FINISHED, seen.last.play.end);
    CHECK_EQUAL(duration, seen.listened_ms);
    CHECK(!store.following);
}

static void an_idle_remembered_track_does_not_start_a_new_history_play(void)
{
    start_real_playback();
    fake_hal_audio_finish_track();
    player_update(&player, 100);
    CHECK_EQUAL(PLAYER_IDLE, player_status(&player).state);
    CHECK(player_status(&player).has_track);
    observe_player(100);
    observe_player(10000);
    CHECK(!store.following);
    CHECK_EQUAL(0, store.log.root.counters.totals.plays);
}

static void time_after_the_audio_endpoint_is_not_counted_as_listening(void)
{
    start_real_playback();
    uint32_t duration = player_status(&player).duration_ms;
    observe_player(0);
    fake_hal_audio_set_position(duration);
    observe_player(duration);
    fake_hal_audio_finish_track();
    player_update(&player, duration + 500);
    observe_player(duration + 500);
    ListeningSeen seen;
    listening_fixture_read_all(&store.log, &seen);
    CHECK_EQUAL(1, seen.count);
    CHECK_EQUAL(duration, seen.listened_ms);
    CHECK_EQUAL(LISTENING_FINISHED, seen.last.play.end);
}

static void restarting_after_idle_keeps_the_finished_play_distinct(void)
{
    start_real_playback();
    uint32_t duration = player_status(&player).duration_ms;
    observe_player(0);
    fake_hal_audio_set_position(duration - 1000);
    observe_player(duration - 1000);
    fake_hal_audio_finish_track();
    player_update(&player, duration);
    observe_player(duration);
    observe_player(duration + 5000);
    player_toggle_pause(&player);
    observe_player(duration + 6000);
    fake_hal_audio_set_position(1000);
    ListeningObservation paused = {.status = player_status(&player),
                                   .token = player.current_token,
                                   .output = LISTENING_OUTPUT_SINGLE_ENDED};
    listening_store_flush(&store, paused, duration + 7000);
    ListeningSeen seen;
    listening_fixture_read_all(&store.log, &seen);
    CHECK_EQUAL(2, seen.count);
    CHECK_EQUAL(LISTENING_FINISHED, seen.first.play.end);
    CHECK_EQUAL(LISTENING_STOPPED, seen.last.play.end);
    CHECK_EQUAL(duration + 1000, seen.listened_ms);
    CHECK_EQUAL(0, store.log.root.counters.totals.skips);
}

static void a_real_paused_track_stays_open_without_counting_the_pause(void)
{
    start_real_playback();
    observe_player(0);
    fake_hal_audio_set_position(1000);
    player_toggle_pause(&player);
    observe_player(1000);
    observe_player(6000);
    CHECK(store.following);
    CHECK_EQUAL(1000, store.active.listened_ms);
    CHECK_EQUAL(0, store.log.root.counters.totals.plays);
    player_toggle_pause(&player);
    observe_player(7000);
    fake_hal_audio_set_position(2000);
    observe_player(8000);
    CHECK_EQUAL(2000, store.active.listened_ms);
}

static void replacing_a_play_at_its_endpoint_does_not_add_extra_elapsed_time(void)
{
    start_real_playback();
    uint32_t duration = player_status(&player).duration_ms;
    observe_player(0);
    fake_hal_audio_set_position(duration);
    observe_player(duration);
    BrowseQuery single = {.kind = BROWSE_SINGLE_TRACK,
                          .group = LIBRARY_ARTISTS,
                          .subject = 0,
                          .artist = LIBRARY_NONE};
    CHECK(player_play_list(&player, single, 0, 7));
    observe_player(duration + 500);
    ListeningSeen seen;
    listening_fixture_read_all(&store.log, &seen);
    CHECK_EQUAL(1, seen.count);
    CHECK_EQUAL(duration, seen.listened_ms);
    CHECK_EQUAL(LISTENING_FINISHED, seen.first.play.end);
}

static void a_decode_failure_to_idle_is_recorded_as_stopped(void)
{
    start_real_playback();
    observe_player(0);
    fake_hal_audio_set_position(1000);
    observe_player(1000);
    fake_hal_audio_fail_track();
    player_update(&player, 1000);
    CHECK_EQUAL(PLAYER_IDLE, player_status(&player).state);
    observe_player(1000);
    ListeningSeen seen;
    listening_fixture_read_all(&store.log, &seen);
    CHECK_EQUAL(1, seen.count);
    CHECK_EQUAL(LISTENING_STOPPED, seen.first.play.end);
    CHECK_EQUAL(1000, seen.listened_ms);
}

static void an_unknown_duration_does_not_discard_elapsed_time_on_a_track_change(void)
{
    reset();
    ListeningObservation play = observed(1, 0, PLAYER_PLAYING);
    play.status.duration_ms = 0;
    listening_store_follow(&store, play, 0);
    play.status.position_ms = 1000;
    listening_store_follow(&store, play, 1000);
    play.token = 2;
    play.status.position_ms = 0;
    listening_store_follow(&store, play, 1500);
    ListeningSeen seen;
    listening_fixture_read_all(&store.log, &seen);
    CHECK_EQUAL(1, seen.count);
    CHECK_EQUAL(1500, seen.listened_ms);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_real_last_track_is_recorded_finished_when_the_player_becomes_idle),
        TEST_CASE(an_idle_remembered_track_does_not_start_a_new_history_play),
        TEST_CASE(time_after_the_audio_endpoint_is_not_counted_as_listening),
        TEST_CASE(restarting_after_idle_keeps_the_finished_play_distinct),
        TEST_CASE(a_real_paused_track_stays_open_without_counting_the_pause),
        TEST_CASE(replacing_a_play_at_its_endpoint_does_not_add_extra_elapsed_time),
        TEST_CASE(a_decode_failure_to_idle_is_recorded_as_stopped),
        TEST_CASE(an_unknown_duration_does_not_discard_elapsed_time_on_a_track_change),
        TEST_CASE(paused_time_and_seek_distance_are_never_counted_as_listening),
        TEST_CASE(a_play_started_with_the_clock_set_carries_its_utc_time_and_offset),
        TEST_CASE(setting_the_clock_dates_new_plays_without_rewriting_earlier_timestamps),
        TEST_CASE(repeating_the_same_track_starts_a_distinct_play_without_a_skip),
        TEST_CASE(the_last_song_finishing_is_not_reported_as_stopped),
        TEST_CASE(a_failed_write_is_retried_once_without_duplicate_counts),
        TEST_CASE(an_uncertain_commit_is_reloaded_before_a_retry),
        TEST_CASE(export_lookup_rejects_an_identity_that_now_names_another_track),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
