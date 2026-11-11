#include <string.h>

#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "player.h"
#include "settings_store.h"

enum {
    BACK_IN_BLACK = 0,
    SHOOT_TO_THRILL = 1,
    HELP = 2,
    SEED = 7,
    TENTHS = 15,
    FADE_MS = 1500
};

static const char HELP_PATH[] = "/Various Artists/The Best of Something/01 - Help!.flac";
static const char SHOOT_TO_THRILL_PATH[] = "/AC DC/Back In Black/1-02 - Shoot to Thrill.flac";

static FixtureLibrary fixture;
static Player player;

static Player *playing(ShuffleMode shuffle, bool fade)
{
    fake_hal_reset();
    player_init(&player, fixture_library_open(&fixture));
    BrowseQuery album = {.kind = BROWSE_ALBUM_TRACKS,
                         .group = LIBRARY_ARTISTS,
                         .subject = BACK_IN_BLACK,
                         .artist = LIBRARY_NONE};
    CHECK(player_play_list(&player, album, 0, SEED));
    player_set_shuffle(&player, shuffle, SEED);
    player_set_transitions(&player, fade, TENTHS);
    return &player;
}

static bool queued(const char *path)
{
    const char *actual = fake_hal_audio_queued_path();
    return actual != NULL && strcmp(actual, path) == 0;
}

static void a_shuffled_song_from_another_album_is_queued_with_a_fade(void)
{
    Player *started = playing(SHUFFLE_SONGS, true);
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_play_next(started, HELP));
    CHECK(queued(HELP_PATH));
    CHECK_EQUAL(FADE_MS, fake_hal_audio_queued_fade_ms());
}

static void the_next_song_of_the_same_album_is_queued_gapless(void)
{
    Player *started = playing(SHUFFLE_ALBUMS, true);
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_play_next(started, SHOOT_TO_THRILL));
    CHECK(queued(SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(0, fake_hal_audio_queued_fade_ms());
}

static void nothing_fades_without_shuffle(void)
{
    Player *started = playing(SHUFFLE_OFF, true);
    CHECK_EQUAL(PLAYER_UP_NEXT_ADDED, player_play_next(started, HELP));
    CHECK(queued(HELP_PATH));
    CHECK_EQUAL(0, fake_hal_audio_queued_fade_ms());
}

static void a_gapless_hand_over_queues_the_following_fade(void)
{
    Player *started = playing(SHUFFLE_SONGS, true);
    player_play_next(started, HELP);
    player_add_to_up_next(started, SHOOT_TO_THRILL);
    fake_hal_audio_finish_track();
    player_update(started, 100);
    CHECK_EQUAL(HELP, player_status(started).track);
    CHECK(queued(SHOOT_TO_THRILL_PATH));
    CHECK_EQUAL(FADE_MS, fake_hal_audio_queued_fade_ms());
}

static void a_manual_skip_starts_the_next_song_without_a_fade(void)
{
    Player *started = playing(SHUFFLE_SONGS, true);
    player_play_next(started, HELP);
    player_skip(started, 1);
    CHECK_EQUAL(2, fake_hal_audio_play_count());
    CHECK_EQUAL(HELP, player_status(started).track);
    CHECK_EQUAL(0, fake_hal_audio_play_fade_ms());
}

static void turning_transitions_off_requeues_without_a_fade(void)
{
    Player *started = playing(SHUFFLE_SONGS, true);
    player_play_next(started, HELP);
    player_set_transitions(started, false, TENTHS);
    CHECK(queued(HELP_PATH));
    CHECK_EQUAL(0, fake_hal_audio_queued_fade_ms());
}

static void a_stored_crossfade_choice_plays_as_a_fade(void)
{
    fake_hal_reset();
    SettingsStore store;
    settings_store_load(&store);
    store.settings.track_transition = TRACK_TRANSITION_CROSSFADE;
    store.settings.transition_tenths = 30;
    player_init(&player, fixture_library_open(&fixture));
    settings_store_restore_playback(&store, &player, SEED);
    CHECK(player.fade_transitions);
    CHECK_EQUAL(30, player.transition_tenths);
    store.settings.track_transition = TRACK_TRANSITION_OFF;
    settings_store_restore_playback(&store, &player, SEED);
    CHECK(!player.fade_transitions);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_shuffled_song_from_another_album_is_queued_with_a_fade),
        TEST_CASE(the_next_song_of_the_same_album_is_queued_gapless),
        TEST_CASE(nothing_fades_without_shuffle),
        TEST_CASE(a_gapless_hand_over_queues_the_following_fade),
        TEST_CASE(a_manual_skip_starts_the_next_song_without_a_fade),
        TEST_CASE(turning_transitions_off_requeues_without_a_fade),
        TEST_CASE(a_stored_crossfade_choice_plays_as_a_fade),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
