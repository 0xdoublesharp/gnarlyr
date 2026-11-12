#include "check.h"
#include "hal/audio.h"
#include "sim_audio.h"
#include "sim_storage.h"

static const HalAudioTrack TRACK = {.path = "/flac/lpc_16_44k_stereo.flac", .token = 1};

static void start_virtual_playback(void)
{
    sim_storage_configure(ECHO_FIXTURE_DIR, ECHO_FIXTURE_DIR);
    CHECK(sim_audio_start(true));
    CHECK(hal_audio_play(&TRACK, 0, NULL));
}

static void virtual_playback_moves_through_the_recording_at_the_chosen_speed(void)
{
    start_virtual_playback();
    hal_audio_set_speed(200);
    sim_audio_tick(20);
    CHECK_EQUAL(40, hal_audio_status().position_ms);
    hal_audio_set_speed(50);
    sim_audio_tick(20);
    CHECK_EQUAL(50, hal_audio_status().position_ms);
    sim_audio_stop();
}

static void normal_speed_follows_the_clock(void)
{
    start_virtual_playback();
    hal_audio_set_speed(100);
    sim_audio_tick(30);
    CHECK_EQUAL(30, hal_audio_status().position_ms);
    sim_audio_stop();
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(virtual_playback_moves_through_the_recording_at_the_chosen_speed),
        TEST_CASE(normal_speed_follows_the_clock),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
