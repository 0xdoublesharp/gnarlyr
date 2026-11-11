#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "hal/storage.h"
#include "playback/resume_state.h"
#include "player.h"
#include "player_private.h"

enum {
    BACK_IN_BLACK = 0,
    HELLS_BELLS = 0,
    SHOOT_TO_THRILL = 1,
    HELP = 2,
    ARMY_OF_ME = 3,
    LATE_IN_TRACK_MS = 5000,
    EARLY_IN_TRACK_MS = 1000,
    SEED = 7
};

static const char HELLS_BELLS_PATH[] = "/AC DC/Back In Black/1-01 - Hells Bells.flac";
static const char SHOOT_TO_THRILL_PATH[] = "/AC DC/Back In Black/1-02 - Shoot to Thrill.flac";
static const char HELP_PATH[] = "/Various Artists/The Best of Something/01 - Help!.flac";
static const char ARMY_OF_ME_PATH[] = "/Various Artists/The Best of Something/02 - Army of Me.flac";

static FixtureLibrary fixture;
static Player player;

static BrowseQuery back_in_black(void)
{
    return (BrowseQuery){.kind = BROWSE_ALBUM_TRACKS,
                         .group = LIBRARY_ARTISTS,
                         .subject = BACK_IN_BLACK,
                         .artist = LIBRARY_NONE};
}

static Player *fresh_player(void)
{
    fake_hal_reset();
    player_init(&player, fixture_library_open(&fixture));
    return &player;
}

static Player *playing(uint32_t position)
{
    Player *started = fresh_player();
    CHECK(player_play_list(started, back_in_black(), position, SEED));
    return started;
}

static bool path_is(const char *actual, const char *expected)
{
    return actual != NULL && strcmp(expected, actual) == 0;
}

static void playing_a_list_starts_the_selected_track(void)
{
    Player *started = playing(0);
    CHECK(path_is(fake_hal_audio_playing_path(), HELLS_BELLS_PATH));
    CHECK_EQUAL(0, fake_hal_audio_play_start());
    PlayerStatus status = player_status(started);
    CHECK_EQUAL(PLAYER_PLAYING, status.state);
    CHECK(status.has_track);
    CHECK_EQUAL(HELLS_BELLS, status.track);
    CHECK_EQUAL(60000, status.duration_ms);
}

static const HalAudioFormat HI_RES_FLAC = {.codec = HAL_AUDIO_CODEC_FLAC,
                                           .sample_rate = 96000,
                                           .bits_per_sample = 24,
                                           .channels = 2,
                                           .bitrate_kbps = 2304};

static void the_status_carries_the_playing_tracks_format(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_format(HI_RES_FLAC);
    HalAudioFormat format = player_status(started).format;
    CHECK_EQUAL(HAL_AUDIO_CODEC_FLAC, format.codec);
    CHECK_EQUAL(96000, format.sample_rate);
    CHECK_EQUAL(24, format.bits_per_sample);
    CHECK_EQUAL(2304, format.bitrate_kbps);
}

static void the_format_waits_for_the_audio_to_reach_the_track(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_format(HI_RES_FLAC);
    fake_hal_audio_set_token(999);
    CHECK_EQUAL(HAL_AUDIO_CODEC_UNKNOWN, player_status(started).format.codec);
}

static void the_status_counts_the_place_in_the_queue(void)
{
    Player *started = playing(0);
    PlayerStatus status = player_status(started);
    CHECK_EQUAL(1, status.place.number);
    CHECK_EQUAL(2, status.place.total);
}

static void the_following_track_is_queued_for_gapless_playback(void)
{
    playing(0);
    CHECK(path_is(fake_hal_audio_queued_path(), SHOOT_TO_THRILL_PATH));
}

static void a_finished_track_moves_the_queue_on(void)
{
    Player *started = playing(0);
    fake_hal_audio_finish_track();
    player_update(started, 100);
    CHECK_EQUAL(SHOOT_TO_THRILL, player_status(started).track);
    CHECK(fake_hal_audio_queued_path() == NULL);
    CHECK_EQUAL(1, fake_hal_audio_play_count());
}

static void the_end_of_the_queue_stops_playback(void)
{
    Player *started = playing(1);
    fake_hal_audio_finish_track();
    player_update(started, 100);
    CHECK_EQUAL(PLAYER_IDLE, player_status(started).state);
}

static void repeat_one_queues_the_same_track(void)
{
    Player *started = playing(0);
    player_set_repeat(started, REPEAT_ONE);
    CHECK(path_is(fake_hal_audio_queued_path(), HELLS_BELLS_PATH));
}

static void repeat_all_wraps_back_to_the_first_track(void)
{
    Player *started = playing(1);
    player_set_repeat(started, REPEAT_ALL);
    CHECK(path_is(fake_hal_audio_queued_path(), HELLS_BELLS_PATH));
}

static void skipping_forward_plays_the_next_track(void)
{
    Player *started = playing(0);
    player_skip(started, 1);
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(SHOOT_TO_THRILL, player_status(started).track);
}

static void skipping_back_early_plays_the_previous_track(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(EARLY_IN_TRACK_MS);
    player_skip(started, -1);
    CHECK(path_is(fake_hal_audio_playing_path(), HELLS_BELLS_PATH));
}

static void skipping_back_late_restarts_the_track(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(LATE_IN_TRACK_MS);
    player_skip(started, -1);
    CHECK_EQUAL(1, fake_hal_audio_seek_count());
    CHECK_EQUAL(0, fake_hal_audio_last_seek());
    CHECK_EQUAL(SHOOT_TO_THRILL, player_status(started).track);
}

static void skipping_back_from_the_first_track_restarts_it(void)
{
    Player *started = playing(0);
    player_skip(started, -1);
    CHECK_EQUAL(1, fake_hal_audio_seek_count());
    CHECK_EQUAL(HELLS_BELLS, player_status(started).track);
}

static void seeking_moves_the_playing_track(void)
{
    Player *started = playing(0);
    player_seek(started, 42000);
    CHECK_EQUAL(1, fake_hal_audio_seek_count());
    CHECK_EQUAL(42000, fake_hal_audio_last_seek());
    CHECK_EQUAL(42000, player_status(started).position_ms);
}

static void seeking_a_resumed_track_moves_where_it_will_start(void)
{
    Player *started = playing(1);
    CHECK(player_save(started));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    player_seek(&resumed, 9000);
    CHECK_EQUAL(9000, player_status(&resumed).position_ms);
    player_toggle_pause(&resumed);
    CHECK_EQUAL(9000, fake_hal_audio_play_start());
}

static void pausing_and_resuming_follow_the_toggle(void)
{
    Player *started = playing(0);
    player_toggle_pause(started);
    CHECK_EQUAL(HAL_AUDIO_PAUSED, fake_hal_audio_state());
    CHECK_EQUAL(PLAYER_PAUSED, player_status(started).state);
    player_toggle_pause(started);
    CHECK_EQUAL(HAL_AUDIO_PLAYING, fake_hal_audio_state());
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void an_unplayable_track_is_skipped(void)
{
    Player *started = playing(0);
    fake_hal_audio_fail_track();
    player_update(started, 100);
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void a_chosen_track_that_cannot_open_is_skipped(void)
{
    Player *started = fresh_player();
    fake_hal_audio_refuse_plays(1);
    CHECK(player_play_list(started, back_in_black(), 0, SEED));
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void a_next_track_that_cannot_open_is_skipped_around_with_repeat_all(void)
{
    Player *started = fresh_player();
    player_set_repeat(started, REPEAT_ALL);
    CHECK(player_play_list(started, back_in_black(), 0, SEED));
    fake_hal_audio_fail_track();
    fake_hal_audio_refuse_plays(1);
    player_update(started, 100);
    CHECK(path_is(fake_hal_audio_playing_path(), HELLS_BELLS_PATH));
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void the_player_stops_when_no_track_can_play(void)
{
    Player *started = fresh_player();
    player_set_repeat(started, REPEAT_ALL);
    fake_hal_audio_refuse_plays(100);
    CHECK(!player_play_list(started, back_in_black(), 0, SEED));
    CHECK_EQUAL(PLAYER_IDLE, player_status(started).state);
    CHECK(fake_hal_audio_play_count() <= 3);
}

static void each_refused_track_is_counted_with_its_reason(void)
{
    Player *started = fresh_player();
    fake_hal_audio_set_refusal(
        (HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_RATE, .sample_rate = 192000});
    fake_hal_audio_refuse_plays(1);
    CHECK(player_play_list(started, back_in_black(), 0, SEED));
    CHECK_EQUAL(1u, started->refusal_count);
    CHECK_EQUAL(HAL_AUDIO_PROBLEM_RATE, started->refusal.problem);
    CHECK_EQUAL(192000u, started->refusal.sample_rate);
}

static void refusals_explain_themselves(void)
{
    char text[48];
    player_refusal_text((HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_RATE, .sample_rate = 192000},
                        text, sizeof text);
    CHECK(path_is(text, "192 kHz isn't supported"));
    player_refusal_text((HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_RATE, .sample_rate = 384000},
                        text, sizeof text);
    CHECK(path_is(text, "384 kHz isn't supported"));
    player_refusal_text((HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_FORMAT}, text, sizeof text);
    CHECK(path_is(text, "File type not supported"));
    player_refusal_text((HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_AAC_ONLY}, text,
                        sizeof text);
    CHECK(path_is(text, "Only AAC-LC and ALAC M4A play"));
    player_refusal_text((HalAudioRefusal){.problem = HAL_AUDIO_PROBLEM_UNREADABLE}, text,
                        sizeof text);
    CHECK(path_is(text, "Couldn't read track"));
}

static void pulling_the_headphones_out_pauses_playback(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_NONE);
    player_update(started, 100);
    CHECK_EQUAL(PLAYER_PAUSED, player_status(started).state);
    CHECK_EQUAL(HAL_AUDIO_PAUSED, fake_hal_audio_state());
}

static void plugging_the_headphones_back_in_does_not_resume(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_NONE);
    player_update(started, 100);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_SINGLE_ENDED);
    player_update(started, 200);
    CHECK_EQUAL(PLAYER_PAUSED, player_status(started).state);
}

static void switching_from_one_jack_to_the_other_keeps_playing(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_BALANCED);
    player_update(started, 100);
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void starting_with_nothing_plugged_in_still_plays(void)
{
    fake_hal_reset();
    fake_hal_audio_set_jack(HAL_AUDIO_JACK_NONE);
    player_init(&player, fixture_library_open(&fixture));
    CHECK(player_play_list(&player, back_in_black(), 0, SEED));
    player_update(&player, 100);
    CHECK_EQUAL(PLAYER_PLAYING, player_status(&player).state);
}

static void volume_changes_are_clamped_and_sent(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_DEFAULT_VOLUME, player_status(started).volume);
    player_change_volume(started, 100);
    CHECK_EQUAL(HAL_AUDIO_VOLUME_MAX, player_status(started).volume);
    CHECK_EQUAL(HAL_AUDIO_VOLUME_MAX, fake_hal_audio_volume());
    player_change_volume(started, -100);
    CHECK_EQUAL(0, fake_hal_audio_volume());
}

static void changing_shuffle_keeps_the_current_track_playing(void)
{
    Player *started = playing(0);
    player_set_shuffle(started, SHUFFLE_SONGS, SEED);
    CHECK_EQUAL(1, fake_hal_audio_play_count());
    CHECK_EQUAL(HELLS_BELLS, player_status(started).track);
    CHECK_EQUAL(SHUFFLE_SONGS, player_status(started).shuffle);
    CHECK(fake_hal_audio_queued_path() != NULL);
}

static void a_pause_longer_than_thirty_seconds_releases_the_audio(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(42000);
    player_toggle_pause(started);
    player_update(started, 1000);
    player_update(started, 1000 + PLAYER_PAUSE_RELEASE_MS - 1);
    CHECK_EQUAL(0, fake_hal_audio_release_count());
    player_update(started, 1000 + PLAYER_PAUSE_RELEASE_MS);
    CHECK_EQUAL(1, fake_hal_audio_release_count());
    CHECK(hal_audio_status().idle);
    CHECK_EQUAL(PLAYER_PAUSED, player_status(started).state);
}

static void playing_again_after_a_released_pause_resumes_in_place(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(42000);
    player_toggle_pause(started);
    player_update(started, 1000);
    player_update(started, 1000 + PLAYER_PAUSE_RELEASE_MS);
    int plays_before = fake_hal_audio_play_count();
    player_toggle_pause(started);
    CHECK_EQUAL(plays_before + 1, fake_hal_audio_play_count());
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(42000, fake_hal_audio_play_start());
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void resuming_before_the_release_restarts_the_pause_timer(void)
{
    Player *started = playing(1);
    player_toggle_pause(started);
    player_update(started, 1000);
    player_update(started, 20000);
    player_toggle_pause(started);
    player_update(started, 21000);
    player_toggle_pause(started);
    player_update(started, 22000);
    player_update(started, 22000 + PLAYER_PAUSE_RELEASE_MS - 1);
    CHECK_EQUAL(0, fake_hal_audio_release_count());
}

static void a_saved_session_resumes_paused_at_its_position(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(42000);
    CHECK(player_save(started));
    int plays_before = fake_hal_audio_play_count();
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    PlayerStatus status = player_status(&resumed);
    CHECK_EQUAL(PLAYER_PAUSED, status.state);
    CHECK_EQUAL(SHOOT_TO_THRILL, status.track);
    CHECK_EQUAL(42000, status.position_ms);
    CHECK_EQUAL(plays_before, fake_hal_audio_play_count());
    player_toggle_pause(&resumed);
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(42000, fake_hal_audio_play_start());
}

static void a_session_from_another_library_is_ignored(void)
{
    fresh_player();
    ResumeState foreign = {.library_fingerprint = 1, .plan = {.source = back_in_black()}};
    uint8_t bytes[RESUME_STATE_SIZE];
    resume_state_encode(&foreign, bytes);
    hal_file_write("/" FIRMWARE_DATA_DIRECTORY "/resume.bin", bytes, sizeof bytes);
    CHECK(!player_restore(&player));
    CHECK_EQUAL(PLAYER_IDLE, player_status(&player).state);
}

static void the_session_is_saved_regularly_while_playing(void)
{
    Player *started = playing(0);
    player_update(started, 1000);
    fake_hal_audio_set_position(12345);
    player_update(started, 1000 + PLAYER_SAVE_INTERVAL_MS);
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(12345, player_status(&resumed).position_ms);
}

static void finish_playing(Player *started)
{
    fake_hal_audio_finish_track();
    player_update(started, 100);
}

static void an_added_song_plays_after_the_current_track(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_add_to_up_next(started, HELP));
    CHECK(path_is(fake_hal_audio_queued_path(), HELP_PATH));
    finish_playing(started);
    CHECK_EQUAL(HELP, player_status(started).track);
    CHECK_EQUAL(0, started->up_next.count);
    CHECK(path_is(fake_hal_audio_queued_path(), SHOOT_TO_THRILL_PATH));
    finish_playing(started);
    CHECK_EQUAL(SHOOT_TO_THRILL, player_status(started).track);
}

static void play_next_goes_before_earlier_added_songs(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_play_next(started, ARMY_OF_ME));
    CHECK(path_is(fake_hal_audio_queued_path(), ARMY_OF_ME_PATH));
    finish_playing(started);
    CHECK_EQUAL(ARMY_OF_ME, player_status(started).track);
    CHECK(path_is(fake_hal_audio_queued_path(), HELP_PATH));
}

static void skipping_forward_takes_the_next_added_song(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_skip(started, 1);
    CHECK(path_is(fake_hal_audio_playing_path(), HELP_PATH));
    player_skip(started, 1);
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
}

static void skipping_back_from_an_added_song_returns_to_the_list(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_skip(started, 1);
    fake_hal_audio_set_position(EARLY_IN_TRACK_MS);
    player_skip(started, -1);
    CHECK(path_is(fake_hal_audio_playing_path(), HELLS_BELLS_PATH));
    CHECK(path_is(fake_hal_audio_queued_path(), SHOOT_TO_THRILL_PATH));
}

static void adding_with_nothing_playing_starts_the_song(void)
{
    Player *idle = fresh_player();
    CHECK_EQUAL(PLAYER_UP_NEXT_STARTED, player_add_to_up_next(idle, HELP));
    CHECK(path_is(fake_hal_audio_playing_path(), HELP_PATH));
    CHECK_EQUAL(PLAYER_PLAYING, player_status(idle).state);
    finish_playing(idle);
    CHECK_EQUAL(PLAYER_IDLE, player_status(idle).state);
}

static void a_full_up_next_refuses_more(void)
{
    Player *started = playing(0);
    for (int index = 0; index < UP_NEXT_CAPACITY; index++) {
        CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_add_to_up_next(started, HELP));
    }
    CHECK_EQUAL(PLAYER_UP_NEXT_FULL, player_add_to_up_next(started, ARMY_OF_ME));
    CHECK_EQUAL(PLAYER_UP_NEXT_FULL, player_play_next(started, ARMY_OF_ME));
}

static void a_new_list_keeps_up_next(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    CHECK(player_play_list(started, back_in_black(), 1, SEED));
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
    CHECK(path_is(fake_hal_audio_queued_path(), HELP_PATH));
}

static void repeat_one_repeats_an_added_song(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_skip(started, 1);
    player_set_repeat(started, REPEAT_ONE);
    CHECK(path_is(fake_hal_audio_queued_path(), HELP_PATH));
    finish_playing(started);
    CHECK_EQUAL(HELP, player_status(started).track);
    CHECK(path_is(fake_hal_audio_queued_path(), HELP_PATH));
}

static void jumping_to_an_added_song_skips_the_ones_before_it(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    CHECK(player_jump_to_up_next(started, 1));
    CHECK(path_is(fake_hal_audio_playing_path(), ARMY_OF_ME_PATH));
    CHECK_EQUAL(0, started->up_next.count);
    CHECK(path_is(fake_hal_audio_queued_path(), SHOOT_TO_THRILL_PATH));
}

static void jumping_into_the_list_clears_up_next(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    CHECK(player_jump_to_queue(started, (PlayQueueCursor){1, 0}));
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(0, started->up_next.count);
}

static void up_next_survives_a_restart(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(2, resumed.up_next.count);
    CHECK_EQUAL(HELP, resumed.up_next.tracks[0]);
    CHECK_EQUAL(ARMY_OF_ME, resumed.up_next.tracks[1]);
}

static void a_session_resumes_inside_an_added_song(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_skip(started, 1);
    fake_hal_audio_set_position(42000);
    CHECK(player_save(started));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(HELP, player_status(&resumed).track);
    player_toggle_pause(&resumed);
    CHECK(path_is(fake_hal_audio_playing_path(), HELP_PATH));
    CHECK_EQUAL(42000, fake_hal_audio_play_start());
    CHECK(path_is(fake_hal_audio_queued_path(), SHOOT_TO_THRILL_PATH));
}

static void releasing_the_output_pauses_where_the_track_was(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_position(42000);
    player_release_output(started);
    PlayerStatus status = player_status(started);
    CHECK_EQUAL(PLAYER_PAUSED, status.state);
    CHECK_EQUAL(42000, status.position_ms);
    CHECK_EQUAL(HELLS_BELLS, status.track);
}

static void play_after_a_release_starts_the_output_again(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_position(42000);
    player_release_output(started);
    player_toggle_pause(started);
    CHECK_EQUAL(2, fake_hal_audio_play_count());
    CHECK_EQUAL(42000, fake_hal_audio_play_start());
    CHECK(path_is(fake_hal_audio_playing_path(), HELLS_BELLS_PATH));
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void initialization_clears_a_previous_queue_binding(void)
{
    fake_hal_reset();
    Player dirty;
    memset(&dirty, 0xA5, sizeof dirty);
    player_init(&dirty, fixture_library_open(&fixture));
    CHECK(dirty.queue.stored_order == NULL);
    if (dirty.queue.stored_order != NULL) {
        return;
    }
    CHECK_EQUAL(PLAYER_IDLE, player_status(&dirty).state);
    CHECK_EQUAL(0, player_status(&dirty).place.total);
    CHECK_EQUAL(0, dirty.queue.stored_generation);
}

static PlayerQueueEditResult edit_up_next(Player *started, uint16_t index, UpNextEdit edit)
{
    return player_edit_up_next(started, started->up_next_revision, index, edit);
}

static void moving_a_song_preserves_playback_and_survives_a_restart(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    fake_hal_audio_set_position(12345);
    int plays = fake_hal_audio_play_count();
    CHECK_EQUAL(PLAYER_QUEUE_EDITED, edit_up_next(started, 1, UP_NEXT_MOVE_FIRST));
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK_EQUAL(12345u, player_status(started).position_ms);
    CHECK_EQUAL(HELLS_BELLS, player_status(started).track);
    CHECK_EQUAL(HAL_AUDIO_PLAYING, fake_hal_audio_state());
    CHECK(path_is(fake_hal_audio_queued_path(), ARMY_OF_ME_PATH));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(ARMY_OF_ME, resumed.up_next.tracks[0]);
    CHECK_EQUAL(HELP, resumed.up_next.tracks[1]);
}

static void moving_earlier_and_later_updates_the_prepared_song(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    CHECK_EQUAL(PLAYER_QUEUE_EDITED, edit_up_next(started, 0, UP_NEXT_MOVE_LATER));
    CHECK(path_is(fake_hal_audio_queued_path(), ARMY_OF_ME_PATH));
    CHECK_EQUAL(PLAYER_QUEUE_EDITED, edit_up_next(started, 1, UP_NEXT_MOVE_EARLIER));
    CHECK(path_is(fake_hal_audio_queued_path(), HELP_PATH));
    player_skip(started, 1);
    CHECK_EQUAL(HELP, player_status(started).track);
    CHECK_EQUAL(ARMY_OF_ME, started->up_next.tracks[0]);
}

static void clearing_added_songs_returns_to_the_album_and_persists(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    CHECK_EQUAL(PLAYER_QUEUE_EDITED, edit_up_next(started, 0, UP_NEXT_CLEAR));
    CHECK_EQUAL(0, started->up_next.count);
    CHECK(path_is(fake_hal_audio_queued_path(), SHOOT_TO_THRILL_PATH));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(0, resumed.up_next.count);
}

static void removing_an_added_song_preserves_a_paused_track(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    fake_hal_audio_set_position(5000);
    player_toggle_pause(started);
    CHECK_EQUAL(PLAYER_QUEUE_EDITED, edit_up_next(started, 0, UP_NEXT_REMOVE));
    CHECK_EQUAL(PLAYER_PAUSED, started->state);
    CHECK_EQUAL(HAL_AUDIO_PAUSED, fake_hal_audio_state());
    CHECK_EQUAL(5000u, player_status(started).position_ms);
    CHECK(path_is(fake_hal_audio_queued_path(), ARMY_OF_ME_PATH));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(1, resumed.up_next.count);
    CHECK_EQUAL(ARMY_OF_ME, resumed.up_next.tracks[0]);
}

static void a_failed_queue_save_leaves_ram_disk_and_playback_unchanged(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    uint32_t revision = started->up_next_revision;
    uint32_t next_token = started->next_token;
    int writes = fake_hal_write_count();
    fake_hal_audio_set_position(5000);
    fake_hal_fail_music_writes(true);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_SAVE_FAILED, edit_up_next(started, 0, UP_NEXT_REMOVE));
    CHECK_EQUAL(revision, started->up_next_revision);
    CHECK_EQUAL(next_token, started->next_token);
    CHECK_EQUAL(2, started->up_next.count);
    CHECK_EQUAL(HELP, started->up_next.tracks[0]);
    CHECK_EQUAL(writes, fake_hal_write_count());
    CHECK_EQUAL(5000u, player_status(started).position_ms);
    CHECK_EQUAL(HAL_AUDIO_PLAYING, fake_hal_audio_state());
    CHECK(path_is(fake_hal_audio_queued_path(), HELP_PATH));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(HELP, resumed.up_next.tracks[0]);
    CHECK_EQUAL(2, resumed.up_next.count);
}

static void a_consumed_queue_entry_invalidates_old_edit_selections(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    uint32_t revision = started->up_next_revision;
    finish_playing(started);
    int writes = fake_hal_write_count();
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_STALE, player_edit_up_next(started, revision, 0, UP_NEXT_REMOVE));
    CHECK_EQUAL(writes, fake_hal_write_count());
    CHECK_EQUAL(1, started->up_next.count);
    CHECK_EQUAL(ARMY_OF_ME, started->up_next.tracks[0]);
}

static void a_transition_pending_player_update_rejects_queue_edits(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    fake_hal_audio_finish_track();
    int writes = fake_hal_write_count();
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_STALE, edit_up_next(started, 0, UP_NEXT_CLEAR));
    CHECK_EQUAL(PLAYER_UP_NEXT_CHANGED, player_add_to_up_next(started, ARMY_OF_ME));
    CHECK_EQUAL(writes, fake_hal_write_count());
    CHECK_EQUAL(1, started->up_next.count);
    player_update(started, 100);
    CHECK_EQUAL(HELP, player_status(started).track);
    CHECK_EQUAL(0, started->up_next.count);
}

static void invalid_queue_edits_do_not_write_or_change_revision(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    int writes = fake_hal_write_count();
    uint32_t revision = started->up_next_revision;
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_INVALID, edit_up_next(started, 1, UP_NEXT_REMOVE));
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_INVALID, edit_up_next(started, 0, UP_NEXT_MOVE_EARLIER));
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_INVALID, edit_up_next(started, 0, UP_NEXT_MOVE_LATER));
    CHECK_EQUAL(writes, fake_hal_write_count());
    CHECK_EQUAL(revision, started->up_next_revision);
}

static void adding_and_play_next_report_failed_saves(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    fake_hal_fail_music_writes(true);
    CHECK_EQUAL(PLAYER_UP_NEXT_SAVE_FAILED, player_add_to_up_next(started, ARMY_OF_ME));
    CHECK_EQUAL(PLAYER_UP_NEXT_SAVE_FAILED, player_play_next(started, ARMY_OF_ME));
    CHECK_EQUAL(1, started->up_next.count);
    CHECK_EQUAL(HELP, started->up_next.tracks[0]);
    CHECK(path_is(fake_hal_audio_queued_path(), HELP_PATH));
}

static void editing_a_released_queue_does_not_reclaim_audio_output(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_release_output(started);
    int plays = fake_hal_audio_play_count();
    CHECK_EQUAL(PLAYER_QUEUE_EDITED, edit_up_next(started, 0, UP_NEXT_CLEAR));
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK(!started->loaded);
    CHECK_EQUAL(PLAYER_PAUSED, started->state);
}

static void finish_queue_edit(Player *started)
{
    for (uint32_t step = 0; step < 100 && player_queue_busy(started); step++) {
        player_update(started, step);
    }
    CHECK(!player_queue_busy(started));
}

static void removing_a_generated_song_preserves_current_audio_and_survives_restart(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_position(1234);
    uint32_t token = started->current_token;
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING,
                player_edit_queue(started, started->up_next_revision, 1, UP_NEXT_REMOVE));
    finish_queue_edit(started);
    CHECK_EQUAL(1, started->queue.count);
    CHECK(started->queue.stored_order != NULL);
    CHECK_EQUAL(HELLS_BELLS, started->track);
    CHECK_EQUAL(token, started->current_token);
    CHECK_EQUAL(1234, player_status(started).position_ms);
    CHECK_EQUAL(PLAYER_PLAYING, started->state);
    CHECK(fake_hal_audio_queued_path() == NULL);
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(1, resumed.queue.count);
    CHECK_EQUAL(HELLS_BELLS, resumed.track);
    CHECK_EQUAL(1234, player_status(&resumed).position_ms);
}

static void expect_stored(Player *started, const uint16_t *tracks, uint16_t count, uint16_t current)
{
    uint16_t actual[16] = {0};
    CHECK(count <= 16);
    CHECK_EQUAL(count, started->queue.count);
    CHECK(started->queue.stored_order != NULL);
    if (started->queue.stored_order == NULL) {
        return;
    }
    CHECK(track_order_read(started->queue.stored_order, 0, count, actual));
    CHECK(memcmp(tracks, actual, (size_t)count * sizeof *tracks) == 0);
    CHECK_EQUAL(current, started->queue.cursor.position);
    CHECK_EQUAL(tracks[current], started->track);
}

static void snapshot_merges_added_songs_and_supports_more_inserts_and_edits(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    finish_queue_edit(started);
    CHECK_EQUAL(PLAYER_QUEUE_EDITED, started->editing.result);
    expect_stored(started, (uint16_t[]){0, 2, 3, 1}, 4, 0);
    CHECK_EQUAL(0, started->up_next.count);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING,
                player_edit_queue(started, started->up_next_revision, 3, UP_NEXT_MOVE_FIRST));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){0, 1, 2, 3}, 4, 0);
    CHECK_EQUAL(PLAYER_UP_NEXT_WORKING, player_play_next(started, HELP));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){0, 2, 1, 2, 3}, 5, 0);
    CHECK_EQUAL(PLAYER_UP_NEXT_WORKING, player_add_to_up_next(started, ARMY_OF_ME));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){0, 2, 1, 2, 3, 3}, 6, 0);
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    expect_stored(&resumed, (uint16_t[]){0, 2, 1, 2, 3, 3}, 6, 0);
}

static void a_manually_started_song_and_history_are_merged_at_the_right_cursor(void)
{
    Player *started = playing(0);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    player_skip(started, 1);
    CHECK_EQUAL(HELP, started->track);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){0, 2, 3, 1}, 4, 1);
    player_skip(started, 1);
    CHECK_EQUAL(ARMY_OF_ME, started->track);
    player_skip(started, 1);
    CHECK_EQUAL(SHOOT_TO_THRILL, started->track);
}

static void wrapped_edits_rebase_the_cursor_without_changing_the_current_song(void)
{
    Player *started = playing(1);
    player_set_repeat(started, REPEAT_ALL);
    player_add_to_up_next(started, HELP);
    player_add_to_up_next(started, ARMY_OF_ME);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){0, 1, 2, 3}, 4, 1);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING,
                player_edit_queue(started, started->up_next_revision, 3, UP_NEXT_MOVE_FIRST));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){1, 0, 2, 3}, 4, 0);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING,
                player_edit_queue(started, started->up_next_revision, 1, UP_NEXT_MOVE_LATER));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){1, 2, 0, 3}, 4, 0);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING,
                player_edit_queue(started, started->up_next_revision, 3, UP_NEXT_MOVE_EARLIER));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){1, 2, 3, 0}, 4, 0);
}

static void cancelling_an_edit_keeps_the_old_order_and_resumes_audio(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    player_update(started, 1);
    player_update(started, 2);
    player_queue_cancel(started);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_CANCELLED, started->editing.result);
    CHECK_EQUAL(PLAYER_PLAYING, started->state);
    CHECK(started->queue.stored_order == NULL);
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK(resumed.queue.stored_order == NULL);
    CHECK_EQUAL(2, resumed.queue.count);
}

static void a_failed_root_save_leaves_an_orphan_that_restore_and_retry_ignore(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    finish_queue_edit(started);
    uint32_t generation = started->queue.stored_generation;
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING,
                player_edit_queue(started, started->up_next_revision, 1, UP_NEXT_REMOVE));
    fake_hal_fail_file_writes(PLAYER_SESSION_PATH);
    finish_queue_edit(started);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_SAVE_FAILED, started->editing.result);
    expect_stored(started, (uint16_t[]){0, 1}, 2, 0);
    CHECK_EQUAL(generation, started->queue.stored_generation);
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    expect_stored(&resumed, (uint16_t[]){0, 1}, 2, 0);
    fake_hal_fail_file_writes(NULL);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING,
                player_edit_queue(started, started->up_next_revision, 1, UP_NEXT_REMOVE));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){0}, 1, 0);
}

static void a_damaged_new_data_bank_falls_back_to_the_previous_session(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    finish_queue_edit(started);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING,
                player_edit_queue(started, started->up_next_revision, 1, UP_NEXT_REMOVE));
    finish_queue_edit(started);
    uint8_t bad = 0xFF;
    uint32_t offset =
        (uint32_t)started->storage.order.bank * TRACK_ORDER_BANK_BYTES + STORE_BLOCK_BYTES;
    CHECK(hal_file_write_at(PLAYER_ORDER_PATH, offset, &bad, 1));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    expect_stored(&resumed, (uint16_t[]){0, 1}, 2, 0);
}

static void an_empty_base_queue_can_resume_and_become_an_editable_queue(void)
{
    Player *started = fresh_player();
    CHECK_EQUAL(PLAYER_UP_NEXT_STARTED, player_add_to_up_next(started, HELP));
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_add_to_up_next(started, ARMY_OF_ME));
    CHECK(player_save(started));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(HELP, resumed.track);
    CHECK_EQUAL(1, resumed.up_next.count);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){2, 3}, 2, 0);
}

static void keeping_only_current_and_changing_shuffle_preserve_the_edit(void)
{
    Player *started = playing(1);
    player_add_to_up_next(started, HELP);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING,
                player_edit_queue(started, started->up_next_revision, 0, UP_NEXT_CLEAR));
    finish_queue_edit(started);
    expect_stored(started, (uint16_t[]){1}, 1, 0);
    player_set_shuffle(started, SHUFFLE_SONGS, 555);
    expect_stored(started, (uint16_t[]){1}, 1, 0);
}

static void generated_playback_advancement_rejects_an_old_edit_selection(void)
{
    Player *started = playing(0);
    uint32_t revision = started->up_next_revision;
    fake_hal_audio_finish_track();
    player_update(started, 100);
    int writes = fake_hal_write_count();
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_STALE, player_edit_queue(started, revision, 1, UP_NEXT_REMOVE));
    CHECK_EQUAL(writes, fake_hal_write_count());
}

static void saving_during_an_edit_cancels_it_and_retains_the_old_queue(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    player_update(started, 1);
    CHECK(player_save(started));
    CHECK(!player_queue_busy(started));
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_CANCELLED, started->editing.result);
    CHECK(started->queue.stored_order == NULL);
    CHECK_EQUAL(PLAYER_PLAYING, fake_hal_audio_state());
}

static void edited_queue_shuffle_preferences_survive_restart_without_reordering(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    finish_queue_edit(started);
    player_set_shuffle(started, SHUFFLE_SONGS, 55);
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(player_restore(&resumed));
    CHECK_EQUAL(SHUFFLE_SONGS, resumed.shuffle);
    expect_stored(&resumed, (uint16_t[]){0, 1}, 2, 0);
}

static void all_wrapped_moves_and_removals_preserve_the_future_sequence(void)
{
    for (uint16_t current = 0; current < 4; current++) {
        for (uint16_t row = 1; row < 4; row++) {
            for (int action = UP_NEXT_MOVE_FIRST; action <= UP_NEXT_REMOVE; action++) {
                Player *started = playing(0);
                player_add_to_up_next(started, HELP);
                player_add_to_up_next(started, ARMY_OF_ME);
                CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
                finish_queue_edit(started);
                CHECK(player_jump_to_queue(started, (PlayQueueCursor){.position = current}));
                player_set_repeat(started, REPEAT_ALL);
                UpNext expected = {0};
                uint16_t current_track = started->track;
                PlayQueueCursor cursor = started->queue.cursor;
                for (uint16_t index = 1; index < 4; index++) {
                    PlayQueueCursor next;
                    uint16_t track;
                    CHECK(play_queue_step(&started->queue, cursor, 1, true, &next));
                    cursor = next;
                    CHECK(play_queue_track_at(&started->queue, cursor, &track));
                    CHECK(up_next_add(&expected, track));
                }
                bool valid = up_next_edit(&expected, (uint16_t)(row - 1), (UpNextEdit)action) &&
                             !(row == 1 && action == UP_NEXT_MOVE_FIRST);
                CHECK_EQUAL(
                    PLAYER_QUEUE_EDIT_WORKING,
                    player_edit_queue(started, started->up_next_revision, row, (UpNextEdit)action));
                finish_queue_edit(started);
                CHECK_EQUAL(valid ? PLAYER_QUEUE_EDITED : PLAYER_QUEUE_EDIT_INVALID,
                            started->editing.result);
                CHECK_EQUAL(current_track, started->track);
                CHECK(play_queue_current(&started->queue, &current_track));
                CHECK_EQUAL(started->track, current_track);
                cursor = started->queue.cursor;
                for (uint16_t index = 0; index < expected.count; index++) {
                    PlayQueueCursor next;
                    uint16_t track;
                    CHECK(play_queue_step(&started->queue, cursor, 1, true, &next));
                    cursor = next;
                    CHECK(play_queue_track_at(&started->queue, cursor, &track));
                    CHECK_EQUAL(expected.tracks[index], track);
                }
            }
        }
    }
}

static void a_semantically_invalid_session_does_not_prevent_starting_a_new_song(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    finish_queue_edit(started);
    SessionState state;
    SessionJournal *journal = &started->storage.journal;
    CHECK_EQUAL(STORE_READY, session_journal_open(journal, journal->config, &state));
    state.resume.cursor.position = 999;
    CHECK_EQUAL(STORE_DONE, session_journal_save(journal, &state));
    CHECK_EQUAL(STORE_DONE, session_journal_save(journal, &state));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(!player_restore(&resumed));
    CHECK_EQUAL(PLAYER_UP_NEXT_STARTED, player_add_to_up_next(&resumed, HELP));
    CHECK_EQUAL(HELP, resumed.track);
    CHECK(resumed.queue.stored_order == NULL);
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_add_to_up_next(&resumed, ARMY_OF_ME));
    CHECK(player_save(&resumed));
    Player again;
    player_init(&again, &fixture.library);
    CHECK(player_restore(&again));
    CHECK_EQUAL(HELP, again.track);
    CHECK_EQUAL(1, again.up_next.count);
}

static void legacy_resume_and_manual_queue_migrate_without_modifying_original_files(void)
{
    Player *started = playing(0);
    uint8_t resume_bytes[4096] = {0};
    uint8_t queue_bytes[4096] = {0};
    ResumeState state = {.library_fingerprint = started->fingerprint,
                         .plan = started->queue.plan,
                         .track = started->track,
                         .cursor = started->queue.cursor,
                         .volume = 12,
                         .position_ms = 5432};
    resume_state_encode(&state, resume_bytes);
    UpNext pending = {0};
    up_next_add(&pending, HELP);
    up_next_encode(&pending, started->fingerprint, queue_bytes);
    fake_hal_reset();
    fake_hal_add_file(PLAYER_RESUME_PATH, resume_bytes, sizeof resume_bytes);
    fake_hal_add_file(PLAYER_UP_NEXT_PATH, queue_bytes, sizeof queue_bytes);
    Player migrated;
    player_init(&migrated, &fixture.library);
    CHECK(player_restore(&migrated));
    CHECK_EQUAL(5432, player_status(&migrated).position_ms);
    CHECK_EQUAL(HELP, migrated.up_next.tracks[0]);
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_add_to_up_next(&migrated, ARMY_OF_ME));
    uint32_t length = 0;
    const uint8_t *original = fake_hal_file_bytes(PLAYER_RESUME_PATH, &length);
    CHECK(original != NULL);
    if (original != NULL) {
        CHECK_EQUAL(sizeof resume_bytes, length);
        CHECK(memcmp(resume_bytes, original, sizeof resume_bytes) == 0);
    }
    original = fake_hal_file_bytes(PLAYER_UP_NEXT_PATH, &length);
    CHECK(original != NULL);
    if (original != NULL) {
        CHECK_EQUAL(sizeof queue_bytes, length);
        CHECK(memcmp(queue_bytes, original, sizeof queue_bytes) == 0);
    }
    Player restored;
    player_init(&restored, &fixture.library);
    CHECK(player_restore(&restored));
    CHECK_EQUAL(2, restored.up_next.count);
    CHECK_EQUAL(HELP, restored.up_next.tracks[0]);
    CHECK_EQUAL(ARMY_OF_ME, restored.up_next.tracks[1]);
    CHECK_EQUAL(5432, player_status(&restored).position_ms);
}

static void a_same_layout_rescan_cannot_restore_ids_for_different_songs(void)
{
    for (int stored = 0; stored < 2; stored++) {
        Player *started = playing(0);
        CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_add_to_up_next(started, HELP));
        if (stored != 0) {
            CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
            finish_queue_edit(started);
        }
        fake_hal_audio_set_position(12345);
        CHECK(player_save(started));
        LibrarySectionInfo tracks = fixture.library.sections[LIBRARY_TRACKS];
        uint8_t temporary[32];
        CHECK_EQUAL(sizeof temporary, tracks.record_size);
        memcpy(temporary, fixture.bytes + tracks.offset, sizeof temporary);
        memcpy(fixture.bytes + tracks.offset, fixture.bytes + tracks.offset + tracks.record_size,
               sizeof temporary);
        memcpy(fixture.bytes + tracks.offset + tracks.record_size, temporary, sizeof temporary);
        CHECK_EQUAL(LIBRARY_OK,
                    library_open(&fixture.library, memory_source(&fixture.memory), fixture.size));
        Player resumed;
        player_init(&resumed, &fixture.library);
        CHECK(!player_restore(&resumed));
        CHECK(!resumed.has_track);
        CHECK_EQUAL(0, resumed.up_next.count);
    }
}

static void changed_cue_boundaries_do_not_reuse_the_old_saved_position(void)
{
    Player *started = playing(0);
    CHECK_EQUAL(PLAYER_QUEUE_EDIT_WORKING, player_snapshot_queue(started));
    finish_queue_edit(started);
    CHECK(player_save(started));
    LibrarySectionInfo tracks = fixture.library.sections[LIBRARY_TRACKS];
    fixture.bytes[tracks.offset + 24] = 1;
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&fixture.library, memory_source(&fixture.memory), fixture.size));
    Player resumed;
    player_init(&resumed, &fixture.library);
    CHECK(!player_restore(&resumed));
    CHECK(!resumed.has_track);
}

static bool imported_tracks(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    const uint16_t *ids = context;
    for (uint16_t index = 0; index < count; index++) {
        tracks[index] = ids[first + index];
    }
    return true;
}

static TrackOrder imported_order(Player *owner)
{
    static uint16_t ids[] = {HELP, HELLS_BELLS, ARMY_OF_ME, SHOOT_TO_THRILL};
    TrackOrder order;
    StoreLoad load = player_storage_order(&owner->storage, &order, NULL);
    CHECK(load == STORE_EMPTY || load == STORE_READY);
    TrackOrderJob writer;
    CHECK(track_order_job_begin(&writer, &order, (TrackOrderSource){imported_tracks, ids, 4},
                                (TrackOrderEdit){.operation = TRACK_ORDER_COPY}));
    StoreResult result;
    do {
        result = track_order_job_step(&writer);
    } while (result == STORE_WORKING);
    CHECK_EQUAL(STORE_DONE, result);
    return order;
}

static void a_result_order_starts_the_selected_original_track_and_keeps_its_neighbors(void)
{
    Player *started = playing(0);
    TrackOrder order = imported_order(started);
    CHECK(player_use_order(started, &order, 2));
    CHECK_EQUAL(ARMY_OF_ME, player_status(started).track);
    CHECK_EQUAL(PLAYER_PAUSED, player_status(started).state);
    CHECK_EQUAL(2, started->queue.cursor.position);
    player_toggle_pause(started);
    CHECK(path_is(fake_hal_audio_playing_path(), ARMY_OF_ME_PATH));
    player_skip(started, 1);
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
}

static void a_failed_result_order_publication_keeps_the_old_playback(void)
{
    Player *started = playing(0);
    TrackOrder order = imported_order(started);
    fake_hal_fail_file_writes(PLAYER_SESSION_PATH);
    CHECK(!player_use_order(started, &order, 2));
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
    CHECK_EQUAL(HELLS_BELLS, player_status(started).track);
    CHECK(path_is(fake_hal_audio_playing_path(), HELLS_BELLS_PATH));
    fake_hal_fail_file_writes(NULL);
}

static void a_result_order_survives_restart_at_the_selected_row(void)
{
    Player *started = playing(0);
    TrackOrder order = imported_order(started);
    CHECK(player_use_order(started, &order, 2));
    Player restored;
    player_init(&restored, &fixture.library);
    CHECK(player_restore(&restored));
    CHECK_EQUAL(ARMY_OF_ME, player_status(&restored).track);
    CHECK_EQUAL(2, restored.queue.cursor.position);
    CHECK_EQUAL(4, restored.queue.count);
}

static void invalid_result_orders_cannot_replace_the_current_song(void)
{
    Player *started = playing(0);
    TrackOrder order = imported_order(started);
    CHECK(!player_use_order(started, &order, 4));
    order.config.fingerprint++;
    CHECK(!player_use_order(started, &order, 0));
    CHECK_EQUAL(HELLS_BELLS, player_status(started).track);
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static Player *tagged_player(void)
{
    Player *started = fresh_player();
    uint32_t fingerprint = library_fingerprint(&fixture.library);
    uint32_t count = library_count(&fixture.library, LIBRARY_TRACKS);
    uint8_t *header = fixture.bytes + fixture.size;
    memset(header, 0, 20 + count * 20);
    memcpy(header, "DSRG", 4);
    header[4] = 1;
    header[6] = 20;
    hash_store(header + 8, count);
    hash_store(header + 12, fingerprint);
    hash_store(header + 16, hash_update(HASH_START, header, 16));
    for (uint32_t index = 0; index < count; index++) {
        uint8_t prefix[8];
        hash_store(prefix, fingerprint);
        hash_store(prefix + 4, index);
        uint8_t *record = header + 20 + index * 20;
        hash_store(record, 65536 + (index + 1) * 10000);
        hash_store(record + 4, 65536 - index * 1000);
        hash_store(record + 8, 32768 + index);
        hash_store(record + 12, 32768 + index);
        hash_store(record + 16,
                   hash_update(hash_update(HASH_START, prefix, sizeof prefix), record, 16));
    }
    fixture.size += 20 + count * 20;
    fixture.memory.size = fixture.size;
    CHECK_EQUAL(LIBRARY_OK,
                library_open(&fixture.library, memory_source(&fixture.memory), fixture.size));
    return started;
}

static void normalization_defaults_off_and_applies_to_both_initial_tracks_when_enabled(void)
{
    Player *started = tagged_player();
    CHECK(player_play_list(started, back_in_black(), 0, SEED));
    CHECK_EQUAL(65536, fake_hal_audio_replay_gain());
    CHECK_EQUAL(65536, fake_hal_audio_queued_replay_gain());
    player_release_output(started);
    player_set_replay_gain(started, REPLAY_GAIN_TRACK, true);
    CHECK(player_play_list(started, back_in_black(), 0, SEED));
    CHECK_EQUAL(32768, fake_hal_audio_replay_gain());
    CHECK_EQUAL(32769, fake_hal_audio_queued_replay_gain());
}

static void live_normalization_updates_current_and_prefetched_tracks_without_restarting(void)
{
    Player *started = tagged_player();
    CHECK(player_play_list(started, back_in_black(), 0, SEED));
    int plays = fake_hal_audio_play_count();
    int seeks = fake_hal_audio_seek_count();
    player_set_replay_gain(started, REPLAY_GAIN_TRACK, false);
    CHECK_EQUAL(75536, fake_hal_audio_replay_gain());
    CHECK_EQUAL(85536, fake_hal_audio_queued_replay_gain());
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK_EQUAL(seeks, fake_hal_audio_seek_count());
    fake_hal_audio_finish_track();
    player_set_replay_gain(started, REPLAY_GAIN_ALBUM, false);
    CHECK_EQUAL(64536, fake_hal_audio_replay_gain());
    player_update(started, 1);
    CHECK_EQUAL(SHOOT_TO_THRILL, player_status(started).track);
    player_set_replay_gain(started, REPLAY_GAIN_OFF, true);
    CHECK_EQUAL(65536, fake_hal_audio_replay_gain());
}

static void normalization_keeps_pause_state_and_damaged_metadata_falls_back_to_unity(void)
{
    Player *started = tagged_player();
    player_set_replay_gain(started, REPLAY_GAIN_TRACK, false);
    uint32_t base = fixture.size - 20 - FIXTURE_TRACKS * 20;
    fixture.bytes[base + 20 + 16] ^= 1;
    CHECK(player_play_list(started, back_in_black(), 0, SEED));
    CHECK_EQUAL(65536, fake_hal_audio_replay_gain());
    CHECK_EQUAL(85536, fake_hal_audio_queued_replay_gain());
    player_toggle_pause(started);
    int plays = fake_hal_audio_play_count();
    player_set_replay_gain(started, REPLAY_GAIN_ALBUM, true);
    CHECK_EQUAL(HAL_AUDIO_PAUSED, fake_hal_audio_state());
    CHECK_EQUAL(PLAYER_PAUSED, player_status(started).state);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
}

static Player *paused_with_rewind(uint32_t position_ms, uint32_t paused_at_ms)
{
    Player *started = playing(1);
    player_set_rewind(started, true);
    fake_hal_audio_set_position(position_ms);
    fake_hal_set_milliseconds(paused_at_ms);
    player_toggle_pause(started);
    return started;
}

static void resuming_after_a_short_pause_does_not_rewind(void)
{
    Player *started = paused_with_rewind(42000, 1000);
    fake_hal_set_milliseconds(11000);
    player_toggle_pause(started);
    CHECK_EQUAL(0, fake_hal_audio_seek_count());
    CHECK_EQUAL(42000, player_status(started).position_ms);
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void resuming_after_a_minute_rewinds_three_seconds(void)
{
    Player *started = paused_with_rewind(42000, 1000);
    fake_hal_set_milliseconds(61000);
    player_toggle_pause(started);
    CHECK_EQUAL(1, fake_hal_audio_seek_count());
    CHECK_EQUAL(39000, fake_hal_audio_last_seek());
    CHECK_EQUAL(39000, player_status(started).position_ms);
    CHECK_EQUAL(HAL_AUDIO_PLAYING, fake_hal_audio_state());
}

static void the_pause_is_timed_across_the_tick_counter_wrapping(void)
{
    Player *started = paused_with_rewind(42000, UINT32_MAX - 29999);
    fake_hal_set_milliseconds(30000);
    player_toggle_pause(started);
    CHECK_EQUAL(39000, fake_hal_audio_last_seek());
}

static void resuming_a_released_pause_starts_rewound(void)
{
    Player *started = paused_with_rewind(42000, 1000);
    player_update(started, 1000);
    player_update(started, 1000 + PLAYER_PAUSE_RELEASE_MS);
    CHECK_EQUAL(1, fake_hal_audio_release_count());
    fake_hal_set_milliseconds(1000 + 600000);
    player_toggle_pause(started);
    CHECK_EQUAL(32000, fake_hal_audio_play_start());
}

static void the_rewind_never_goes_before_the_start_of_the_track(void)
{
    Player *started = paused_with_rewind(2000, 1000);
    fake_hal_set_milliseconds(1000 + 3600000);
    player_toggle_pause(started);
    CHECK_EQUAL(0, fake_hal_audio_last_seek());
    CHECK_EQUAL(0, player_status(started).position_ms);
}

static void seeking_while_paused_cancels_the_rewind(void)
{
    Player *started = paused_with_rewind(42000, 1000);
    player_seek(started, 50000);
    fake_hal_set_milliseconds(1000 + 600000);
    player_toggle_pause(started);
    CHECK_EQUAL(1, fake_hal_audio_seek_count());
    CHECK_EQUAL(50000, player_status(started).position_ms);
}

static void rewind_off_resumes_exactly_where_it_paused(void)
{
    Player *started = paused_with_rewind(42000, 1000);
    player_set_rewind(started, false);
    fake_hal_set_milliseconds(1000 + 3600000);
    player_toggle_pause(started);
    CHECK_EQUAL(0, fake_hal_audio_seek_count());
    CHECK_EQUAL(42000, player_status(started).position_ms);
}

static Player *restored_with_rewind(bool rewind, Player *resumed)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(42000);
    CHECK(player_save(started));
    player_init(resumed, &fixture.library);
    player_set_rewind(resumed, rewind);
    CHECK(player_restore(resumed));
    return resumed;
}

static void a_cold_resume_rewinds_twenty_seconds(void)
{
    Player resumed;
    restored_with_rewind(true, &resumed);
    CHECK_EQUAL(42000, player_status(&resumed).position_ms);
    player_toggle_pause(&resumed);
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(42000 - RESUME_REWIND_COLD_MUSIC_MS, fake_hal_audio_play_start());
}

static void a_cold_resume_with_rewind_off_starts_in_place(void)
{
    Player resumed;
    restored_with_rewind(false, &resumed);
    player_toggle_pause(&resumed);
    CHECK_EQUAL(42000, fake_hal_audio_play_start());
}

static void a_cold_resume_rewinds_only_the_first_play(void)
{
    Player resumed;
    restored_with_rewind(true, &resumed);
    player_toggle_pause(&resumed);
    fake_hal_audio_set_position(30000);
    player_toggle_pause(&resumed);
    player_toggle_pause(&resumed);
    CHECK_EQUAL(0, fake_hal_audio_seek_count());
}

static void enter_standby(Player *started, uint32_t paused_at_ms)
{
    player_toggle_pause(started);
    player_update(started, paused_at_ms);
    player_update(started, paused_at_ms + PLAYER_PAUSE_RELEASE_MS);
}

static void a_pause_becomes_standby_only_once_the_output_is_released(void)
{
    Player *started = playing(1);
    CHECK(!player_in_standby(started));
    player_toggle_pause(started);
    player_update(started, 1000);
    CHECK(!player_in_standby(started));
    player_update(started, 1000 + PLAYER_PAUSE_RELEASE_MS);
    CHECK(player_in_standby(started));
    player_toggle_pause(started);
    CHECK(!player_in_standby(started));
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void holding_standby_keeps_a_long_pause_powered_until_it_is_allowed_again(void)
{
    Player *started = playing(1);
    player_hold_standby(started, true);
    player_toggle_pause(started);
    player_update(started, 1000);
    player_update(started, 1000 + 10 * PLAYER_PAUSE_RELEASE_MS);
    CHECK_EQUAL(0, fake_hal_audio_release_count());
    CHECK(!player_in_standby(started));
    player_hold_standby(started, false);
    uint32_t allowed_ms = 2000 + 10 * PLAYER_PAUSE_RELEASE_MS;
    player_update(started, allowed_ms);
    player_update(started, allowed_ms + PLAYER_PAUSE_RELEASE_MS - 1);
    CHECK_EQUAL(0, fake_hal_audio_release_count());
    player_update(started, allowed_ms + PLAYER_PAUSE_RELEASE_MS);
    CHECK_EQUAL(1, fake_hal_audio_release_count());
}

static void an_output_handed_to_a_usb_session_is_never_released_by_a_long_pause(void)
{
    Player *started = playing(1);
    player_release_output(started);
    player_update(started, 1000);
    player_update(started, 1000 + 3 * PLAYER_PAUSE_RELEASE_MS);
    CHECK_EQUAL(0, fake_hal_audio_release_count());
    CHECK_EQUAL(PLAYER_PAUSED, player_status(started).state);
}

static void resuming_from_standby_keeps_the_replay_gain_and_the_following_track(void)
{
    Player *started = tagged_player();
    player_set_replay_gain(started, REPLAY_GAIN_TRACK, false);
    CHECK(player_play_list(started, back_in_black(), 0, SEED));
    uint32_t gain = fake_hal_audio_replay_gain();
    uint32_t queued_gain = fake_hal_audio_queued_replay_gain();
    fake_hal_audio_set_position(61234);
    enter_standby(started, 1000);
    CHECK_EQUAL(61234, player_status(started).position_ms);
    player_toggle_pause(started);
    CHECK_EQUAL(61234, fake_hal_audio_play_start());
    CHECK_EQUAL(gain, fake_hal_audio_replay_gain());
    CHECK_EQUAL(queued_gain, fake_hal_audio_queued_replay_gain());
    CHECK(path_is(fake_hal_audio_queued_path(), SHOOT_TO_THRILL_PATH));
}

static void skipping_in_standby_starts_the_next_track_from_its_beginning(void)
{
    Player *started = playing(0);
    fake_hal_audio_set_position(42000);
    enter_standby(started, 1000);
    player_skip(started, 1);
    CHECK(path_is(fake_hal_audio_playing_path(), SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(0, fake_hal_audio_play_start());
    CHECK_EQUAL(PLAYER_PLAYING, player_status(started).state);
}

static void seeking_in_standby_moves_the_resume_point_without_powering_up(void)
{
    Player *started = playing(1);
    fake_hal_audio_set_position(42000);
    enter_standby(started, 1000);
    int plays = fake_hal_audio_play_count();
    player_seek(started, 10000);
    CHECK_EQUAL(plays, fake_hal_audio_play_count());
    CHECK_EQUAL(0, fake_hal_audio_seek_count());
    CHECK(player_in_standby(started));
    player_toggle_pause(started);
    CHECK_EQUAL(10000, fake_hal_audio_play_start());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_result_order_starts_the_selected_original_track_and_keeps_its_neighbors),
        TEST_CASE(a_failed_result_order_publication_keeps_the_old_playback),
        TEST_CASE(a_result_order_survives_restart_at_the_selected_row),
        TEST_CASE(invalid_result_orders_cannot_replace_the_current_song),
        TEST_CASE(a_same_layout_rescan_cannot_restore_ids_for_different_songs),
        TEST_CASE(changed_cue_boundaries_do_not_reuse_the_old_saved_position),
        TEST_CASE(legacy_resume_and_manual_queue_migrate_without_modifying_original_files),
        TEST_CASE(a_semantically_invalid_session_does_not_prevent_starting_a_new_song),
        TEST_CASE(all_wrapped_moves_and_removals_preserve_the_future_sequence),
        TEST_CASE(generated_playback_advancement_rejects_an_old_edit_selection),
        TEST_CASE(saving_during_an_edit_cancels_it_and_retains_the_old_queue),
        TEST_CASE(edited_queue_shuffle_preferences_survive_restart_without_reordering),
        TEST_CASE(snapshot_merges_added_songs_and_supports_more_inserts_and_edits),
        TEST_CASE(a_manually_started_song_and_history_are_merged_at_the_right_cursor),
        TEST_CASE(wrapped_edits_rebase_the_cursor_without_changing_the_current_song),
        TEST_CASE(cancelling_an_edit_keeps_the_old_order_and_resumes_audio),
        TEST_CASE(a_failed_root_save_leaves_an_orphan_that_restore_and_retry_ignore),
        TEST_CASE(a_damaged_new_data_bank_falls_back_to_the_previous_session),
        TEST_CASE(an_empty_base_queue_can_resume_and_become_an_editable_queue),
        TEST_CASE(keeping_only_current_and_changing_shuffle_preserve_the_edit),
        TEST_CASE(removing_a_generated_song_preserves_current_audio_and_survives_restart),
        TEST_CASE(moving_a_song_preserves_playback_and_survives_a_restart),
        TEST_CASE(moving_earlier_and_later_updates_the_prepared_song),
        TEST_CASE(clearing_added_songs_returns_to_the_album_and_persists),
        TEST_CASE(removing_an_added_song_preserves_a_paused_track),
        TEST_CASE(a_failed_queue_save_leaves_ram_disk_and_playback_unchanged),
        TEST_CASE(a_consumed_queue_entry_invalidates_old_edit_selections),
        TEST_CASE(a_transition_pending_player_update_rejects_queue_edits),
        TEST_CASE(invalid_queue_edits_do_not_write_or_change_revision),
        TEST_CASE(adding_and_play_next_report_failed_saves),
        TEST_CASE(editing_a_released_queue_does_not_reclaim_audio_output),
        TEST_CASE(a_pause_becomes_standby_only_once_the_output_is_released),
        TEST_CASE(holding_standby_keeps_a_long_pause_powered_until_it_is_allowed_again),
        TEST_CASE(an_output_handed_to_a_usb_session_is_never_released_by_a_long_pause),
        TEST_CASE(resuming_from_standby_keeps_the_replay_gain_and_the_following_track),
        TEST_CASE(skipping_in_standby_starts_the_next_track_from_its_beginning),
        TEST_CASE(seeking_in_standby_moves_the_resume_point_without_powering_up),

        TEST_CASE(initialization_clears_a_previous_queue_binding),
        TEST_CASE(playing_a_list_starts_the_selected_track),
        TEST_CASE(normalization_defaults_off_and_applies_to_both_initial_tracks_when_enabled),
        TEST_CASE(live_normalization_updates_current_and_prefetched_tracks_without_restarting),
        TEST_CASE(normalization_keeps_pause_state_and_damaged_metadata_falls_back_to_unity),
        TEST_CASE(the_status_carries_the_playing_tracks_format),
        TEST_CASE(the_format_waits_for_the_audio_to_reach_the_track),
        TEST_CASE(the_status_counts_the_place_in_the_queue),
        TEST_CASE(the_following_track_is_queued_for_gapless_playback),
        TEST_CASE(a_finished_track_moves_the_queue_on),
        TEST_CASE(the_end_of_the_queue_stops_playback),
        TEST_CASE(repeat_one_queues_the_same_track),
        TEST_CASE(repeat_all_wraps_back_to_the_first_track),
        TEST_CASE(skipping_forward_plays_the_next_track),
        TEST_CASE(skipping_back_early_plays_the_previous_track),
        TEST_CASE(skipping_back_late_restarts_the_track),
        TEST_CASE(skipping_back_from_the_first_track_restarts_it),
        TEST_CASE(seeking_moves_the_playing_track),
        TEST_CASE(seeking_a_resumed_track_moves_where_it_will_start),
        TEST_CASE(pausing_and_resuming_follow_the_toggle),
        TEST_CASE(an_unplayable_track_is_skipped),
        TEST_CASE(a_chosen_track_that_cannot_open_is_skipped),
        TEST_CASE(a_next_track_that_cannot_open_is_skipped_around_with_repeat_all),
        TEST_CASE(the_player_stops_when_no_track_can_play),
        TEST_CASE(each_refused_track_is_counted_with_its_reason),
        TEST_CASE(refusals_explain_themselves),
        TEST_CASE(pulling_the_headphones_out_pauses_playback),
        TEST_CASE(plugging_the_headphones_back_in_does_not_resume),
        TEST_CASE(switching_from_one_jack_to_the_other_keeps_playing),
        TEST_CASE(starting_with_nothing_plugged_in_still_plays),
        TEST_CASE(volume_changes_are_clamped_and_sent),
        TEST_CASE(changing_shuffle_keeps_the_current_track_playing),
        TEST_CASE(a_pause_longer_than_thirty_seconds_releases_the_audio),
        TEST_CASE(playing_again_after_a_released_pause_resumes_in_place),
        TEST_CASE(resuming_before_the_release_restarts_the_pause_timer),
        TEST_CASE(a_saved_session_resumes_paused_at_its_position),
        TEST_CASE(resuming_after_a_short_pause_does_not_rewind),
        TEST_CASE(resuming_after_a_minute_rewinds_three_seconds),
        TEST_CASE(the_pause_is_timed_across_the_tick_counter_wrapping),
        TEST_CASE(resuming_a_released_pause_starts_rewound),
        TEST_CASE(the_rewind_never_goes_before_the_start_of_the_track),
        TEST_CASE(seeking_while_paused_cancels_the_rewind),
        TEST_CASE(rewind_off_resumes_exactly_where_it_paused),
        TEST_CASE(a_cold_resume_rewinds_twenty_seconds),
        TEST_CASE(a_cold_resume_with_rewind_off_starts_in_place),
        TEST_CASE(a_cold_resume_rewinds_only_the_first_play),
        TEST_CASE(a_session_from_another_library_is_ignored),
        TEST_CASE(the_session_is_saved_regularly_while_playing),
        TEST_CASE(an_added_song_plays_after_the_current_track),
        TEST_CASE(play_next_goes_before_earlier_added_songs),
        TEST_CASE(skipping_forward_takes_the_next_added_song),
        TEST_CASE(skipping_back_from_an_added_song_returns_to_the_list),
        TEST_CASE(adding_with_nothing_playing_starts_the_song),
        TEST_CASE(a_full_up_next_refuses_more),
        TEST_CASE(a_new_list_keeps_up_next),
        TEST_CASE(repeat_one_repeats_an_added_song),
        TEST_CASE(jumping_to_an_added_song_skips_the_ones_before_it),
        TEST_CASE(jumping_into_the_list_clears_up_next),
        TEST_CASE(up_next_survives_a_restart),
        TEST_CASE(a_session_resumes_inside_an_added_song),
        TEST_CASE(releasing_the_output_pauses_where_the_track_was),
        TEST_CASE(play_after_a_release_starts_the_output_again),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
