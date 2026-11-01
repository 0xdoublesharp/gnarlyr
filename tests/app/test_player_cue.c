#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "player.h"

enum {
    CUE_ALBUM = 1,
    OPENING = 2,
    MIDDLE = 3,
    CLOSING = 4,
    MIDDLE_START = 1000,
    MIDDLE_END = 2493,
    MIDDLE_LENGTH = 1493,
    CLOSING_LENGTH = 1507,
    SEED = 5
};

static const char CUE_AUDIO[] = "/Cue Artist/Cue Album/Cue Album.flac";

static FixtureLibrary fixture;
static Player player;

static BrowseQuery cue_album(void)
{
    return (BrowseQuery){.kind = BROWSE_ALBUM_TRACKS,
                         .group = LIBRARY_ARTISTS,
                         .subject = CUE_ALBUM,
                         .artist = LIBRARY_NONE};
}

static Player *playing(uint32_t position)
{
    fake_hal_reset();
    player_init(&player, fixture_cue_library_open(&fixture));
    CHECK(player_play_list(&player, cue_album(), position, SEED));
    return &player;
}

static bool plays_cue_audio(void)
{
    return strcmp(fake_hal_audio_playing_path(), CUE_AUDIO) == 0;
}

static void a_cue_track_plays_its_own_part_of_the_file(void)
{
    Player *started = playing(1);
    CHECK(plays_cue_audio());
    CHECK_EQUAL(MIDDLE_START, fake_hal_audio_play_start());
    CHECK_EQUAL(MIDDLE_END, fake_hal_audio_play_end());
    CHECK_EQUAL(MIDDLE, player_status(started).track);
}

static void the_last_cue_track_runs_to_the_end_of_the_file(void)
{
    playing(2);
    CHECK_EQUAL(MIDDLE_END, fake_hal_audio_play_start());
    CHECK_EQUAL(0, fake_hal_audio_play_end());
}

static void the_status_counts_from_the_start_of_the_cue_track(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(MIDDLE_START + 300);
    PlayerStatus status = player_status(started);
    CHECK_EQUAL(300, status.position_ms);
    CHECK_EQUAL(MIDDLE_LENGTH, status.duration_ms);
}

static void the_following_cue_track_is_queued_for_a_gapless_hand_over(void)
{
    playing(1);
    CHECK(strcmp(fake_hal_audio_queued_path(), CUE_AUDIO) == 0);
    CHECK_EQUAL(MIDDLE_END, fake_hal_audio_queued_start());
    CHECK_EQUAL(0, fake_hal_audio_queued_end());
}

static void a_hand_over_to_the_next_cue_track_restarts_its_clock(void)
{
    Player *started = playing(0);
    fake_hal_audio_finish_track();
    player_update(started, 100);
    PlayerStatus status = player_status(started);
    CHECK_EQUAL(MIDDLE, status.track);
    CHECK_EQUAL(0, status.position_ms);
    fake_hal_audio_set_position(MIDDLE_START + 50);
    CHECK_EQUAL(50, player_status(started).position_ms);
    CHECK_EQUAL(MIDDLE_END, fake_hal_audio_queued_start());
}

static void seeking_moves_inside_the_cue_track_not_the_file(void)
{
    Player *started = playing(1);
    player_seek(started, 400);
    CHECK_EQUAL(MIDDLE_START + 400, fake_hal_audio_last_seek());
    fake_hal_audio_set_position(MIDDLE_START + 400);
    CHECK_EQUAL(400, player_status(started).position_ms);
}

static void skipping_back_late_restarts_at_the_start_of_the_cue_track(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(MIDDLE_START + 3500);
    player_skip(started, -1);
    CHECK_EQUAL(MIDDLE_START, fake_hal_audio_last_seek());
    CHECK_EQUAL(MIDDLE, player_status(started).track);
}

static void jumping_past_the_end_of_the_cue_track_starts_the_next_one(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_position(900);
    player_jump(started, 500);
    CHECK_EQUAL(MIDDLE, player_status(started).track);
    CHECK_EQUAL(MIDDLE_START, fake_hal_audio_play_start());
}

static void a_saved_cue_session_resumes_inside_its_track(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(MIDDLE_START + 600);
    CHECK(player_save(started));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(MIDDLE, player_status(&resumed).track);
    CHECK_EQUAL(600, player_status(&resumed).position_ms);
    player_toggle_pause(&resumed);
    CHECK_EQUAL(MIDDLE_START + 600, fake_hal_audio_play_start());
    CHECK_EQUAL(MIDDLE_END, fake_hal_audio_play_end());
}

static void a_cue_track_added_to_up_next_is_queued_with_its_range(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_play_next(started, CLOSING));
    CHECK_EQUAL(MIDDLE_END, fake_hal_audio_queued_start());
    CHECK_EQUAL(0, fake_hal_audio_queued_end());
    fake_hal_audio_finish_track();
    player_update(started, 100);
    CHECK_EQUAL(CLOSING, player_status(started).track);
    CHECK_EQUAL(CLOSING_LENGTH, player_status(started).duration_ms);
}

static void releasing_the_output_keeps_the_position_inside_the_cue_track(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(MIDDLE_START + 250);
    player_release_output(started);
    CHECK_EQUAL(250, player_status(started).position_ms);
    player_toggle_pause(started);
    CHECK_EQUAL(MIDDLE_START + 250, fake_hal_audio_play_start());
}

static void shuffling_plays_cue_tracks_with_their_own_ranges(void)
{
    Player *started = playing(0);
    player_set_shuffle(started, SHUFFLE_SONGS, SEED);
    uint32_t queued_start = fake_hal_audio_queued_start();
    CHECK(queued_start == MIDDLE_START || queued_start == MIDDLE_END);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_cue_track_plays_its_own_part_of_the_file),
        TEST_CASE(the_last_cue_track_runs_to_the_end_of_the_file),
        TEST_CASE(the_status_counts_from_the_start_of_the_cue_track),
        TEST_CASE(the_following_cue_track_is_queued_for_a_gapless_hand_over),
        TEST_CASE(a_hand_over_to_the_next_cue_track_restarts_its_clock),
        TEST_CASE(seeking_moves_inside_the_cue_track_not_the_file),
        TEST_CASE(skipping_back_late_restarts_at_the_start_of_the_cue_track),
        TEST_CASE(jumping_past_the_end_of_the_cue_track_starts_the_next_one),
        TEST_CASE(a_saved_cue_session_resumes_inside_its_track),
        TEST_CASE(a_cue_track_added_to_up_next_is_queued_with_its_range),
        TEST_CASE(releasing_the_output_keeps_the_position_inside_the_cue_track),
        TEST_CASE(shuffling_plays_cue_tracks_with_their_own_ranges),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
