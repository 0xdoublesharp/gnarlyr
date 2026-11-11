#include "check.h"
#include "playback/track_transition.h"

static TransitionRequest shuffled(void)
{
    return (TransitionRequest){
        .fade = true,
        .tenths = 15,
        .shuffle = SHUFFLE_SONGS,
        .from =
            {.record = {.path = 100, .album = 1, .disc = 1, .number = 3, .duration_ms = 200000}},
        .to = {.record = {.path = 200, .album = 2, .disc = 1, .number = 7, .duration_ms = 180000}}};
}

static void shuffled_songs_from_different_albums_fade_for_the_chosen_length(void)
{
    TransitionRequest request = shuffled();
    CHECK_EQUAL(1500, track_transition_fade_ms(&request));
    request.tenths = 60;
    CHECK_EQUAL(6000, track_transition_fade_ms(&request));
}

static void album_shuffle_fades_between_albums(void)
{
    TransitionRequest request = shuffled();
    request.shuffle = SHUFFLE_ALBUMS;
    CHECK_EQUAL(1500, track_transition_fade_ms(&request));
}

static void nothing_fades_when_the_setting_is_off(void)
{
    TransitionRequest request = shuffled();
    request.fade = false;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
}

static void nothing_fades_without_shuffle(void)
{
    TransitionRequest request = shuffled();
    request.shuffle = SHUFFLE_OFF;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
}

static void the_next_track_of_the_same_album_plays_gapless(void)
{
    TransitionRequest request = shuffled();
    request.shuffle = SHUFFLE_ALBUMS;
    request.to.record.album = 1;
    request.to.record.number = 4;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
}

static void the_first_track_of_the_next_disc_plays_gapless(void)
{
    TransitionRequest request = shuffled();
    request.to.record.album = 1;
    request.to.record.disc = 2;
    request.to.record.number = 1;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
}

static void unnumbered_tracks_of_one_album_play_gapless(void)
{
    TransitionRequest request = shuffled();
    request.from.record.number = 0;
    request.to.record.album = 1;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
}

static void a_shuffled_jump_within_an_album_fades(void)
{
    TransitionRequest request = shuffled();
    request.to.record.album = 1;
    request.to.record.number = 9;
    CHECK_EQUAL(1500, track_transition_fade_ms(&request));
    request.to.record.number = 2;
    CHECK_EQUAL(1500, track_transition_fade_ms(&request));
}

static void parts_of_one_file_never_fade(void)
{
    TransitionRequest request = shuffled();
    request.to.record.path = request.from.record.path;
    request.to.record.start_ms = 300000;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
}

static void spoken_word_never_fades(void)
{
    TransitionRequest request = shuffled();
    request.from.spoken = true;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
    request = shuffled();
    request.to.spoken = true;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
}

static void a_fade_takes_at_most_a_third_of_the_shorter_track(void)
{
    TransitionRequest request = shuffled();
    request.tenths = 60;
    request.to.record.duration_ms = 9000;
    CHECK_EQUAL(3000, track_transition_fade_ms(&request));
    request.from.record.duration_ms = 2400;
    CHECK_EQUAL(800, track_transition_fade_ms(&request));
}

static void very_short_or_unknown_tracks_do_not_fade(void)
{
    TransitionRequest request = shuffled();
    request.to.record.duration_ms = 1999;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
    request.to.record.duration_ms = 0;
    CHECK_EQUAL(0, track_transition_fade_ms(&request));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(shuffled_songs_from_different_albums_fade_for_the_chosen_length),
        TEST_CASE(album_shuffle_fades_between_albums),
        TEST_CASE(nothing_fades_when_the_setting_is_off),
        TEST_CASE(nothing_fades_without_shuffle),
        TEST_CASE(the_next_track_of_the_same_album_plays_gapless),
        TEST_CASE(the_first_track_of_the_next_disc_plays_gapless),
        TEST_CASE(unnumbered_tracks_of_one_album_play_gapless),
        TEST_CASE(a_shuffled_jump_within_an_album_fades),
        TEST_CASE(parts_of_one_file_never_fade),
        TEST_CASE(spoken_word_never_fades),
        TEST_CASE(a_fade_takes_at_most_a_third_of_the_shorter_track),
        TEST_CASE(very_short_or_unknown_tracks_do_not_fade),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
