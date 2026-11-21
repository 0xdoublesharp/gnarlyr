#include "check.h"
#include "hal/audio.h"
#include "sim_audio.h"
#include "sim_storage.h"

static const HalAudioTrack TRACK = {.path = "/flac/lpc_16_44k_stereo.flac", .token = 1};
static const HalAudioTrack HIGH_RATE = {.path = "/flac/lpc_24_96k_stereo.flac", .token = 9};

static void start_virtual_audio(void)
{
    sim_storage_configure(ECHO_FIXTURE_DIR, ECHO_FIXTURE_DIR);
    CHECK(sim_audio_start(true));
}

static void start_virtual_playback(void)
{
    start_virtual_audio();
    CHECK(hal_audio_play(&TRACK, 0, NULL, false));
}

static void a_missing_recording_reports_failure_without_leaving_audio_active(void)
{
    HalAudioTrack missing = {.path = "/flac/missing.flac", .token = 42};
    start_virtual_audio();
    CHECK(!hal_audio_play(&missing, 0, NULL, false));
    HalAudioStatus status = hal_audio_status();
    CHECK_EQUAL(HAL_AUDIO_STOPPED, status.state);
    CHECK_EQUAL(42, status.token);
    CHECK(status.failed);
    CHECK(status.idle);
    CHECK_EQUAL(HAL_AUDIO_CODEC_UNKNOWN, status.format.codec);
    CHECK_EQUAL(0, hal_audio_repeat_mark(false));
    CHECK_EQUAL(0, hal_audio_visual_buffer().count);
    sim_audio_stop();
}

static void pause_resume_seek_and_release_preserve_the_public_state(void)
{
    start_virtual_playback();
    HalAudioStatus status = hal_audio_status();
    CHECK_EQUAL(HAL_AUDIO_PLAYING, status.state);
    CHECK_EQUAL(1, status.token);
    CHECK_EQUAL(HAL_AUDIO_CODEC_FLAC, status.format.codec);
    CHECK_EQUAL(44100, status.format.sample_rate);
    CHECK(!status.failed);
    CHECK(!status.idle);
    hal_audio_pause();
    sim_audio_tick(40);
    CHECK_EQUAL(HAL_AUDIO_PAUSED, hal_audio_status().state);
    CHECK_EQUAL(0, hal_audio_status().position_ms);
    hal_audio_resume();
    sim_audio_tick(20);
    CHECK_EQUAL(20, hal_audio_status().position_ms);
    hal_audio_seek(75);
    CHECK_EQUAL(74, hal_audio_status().position_ms);
    hal_audio_release();
    CHECK_EQUAL(HAL_AUDIO_STOPPED, hal_audio_status().state);
    CHECK(hal_audio_status().idle);
    hal_audio_seek(10);
    CHECK_EQUAL(0, hal_audio_status().position_ms);
    sim_audio_stop();
}

static void paused_start_waits_until_resume(void)
{
    start_virtual_audio();
    CHECK(hal_audio_play(&TRACK, 30, NULL, true));
    CHECK_EQUAL(HAL_AUDIO_PAUSED, hal_audio_status().state);
    CHECK_EQUAL(30, hal_audio_status().position_ms);
    sim_audio_tick(50);
    CHECK_EQUAL(30, hal_audio_status().position_ms);
    hal_audio_resume();
    sim_audio_tick(10);
    CHECK_EQUAL(40, hal_audio_status().position_ms);
    sim_audio_stop();
}

static void queued_recording_takes_over_at_the_requested_end(void)
{
    HalAudioTrack first = TRACK;
    HalAudioTrack second = TRACK;
    first.end_ms = 20;
    second.token = 2;
    start_virtual_audio();
    CHECK(hal_audio_play(&first, 0, &second, false));
    sim_audio_tick(25);
    HalAudioStatus status = hal_audio_status();
    CHECK_EQUAL(HAL_AUDIO_PLAYING, status.state);
    CHECK_EQUAL(2, status.token);
    CHECK_EQUAL(0, status.position_ms);
    sim_audio_stop();
}

static void clearing_the_queue_stops_at_the_requested_end(void)
{
    HalAudioTrack first = TRACK;
    HalAudioTrack second = TRACK;
    first.end_ms = 10;
    second.token = 2;
    start_virtual_audio();
    CHECK(hal_audio_play(&first, 0, &second, false));
    hal_audio_queue_next(NULL);
    sim_audio_tick(15);
    CHECK_EQUAL(HAL_AUDIO_STOPPED, hal_audio_status().state);
    sim_audio_stop();
}

static void a_replacement_queue_entry_is_the_one_that_plays(void)
{
    HalAudioTrack first = TRACK;
    HalAudioTrack second = TRACK;
    HalAudioTrack replacement = TRACK;
    first.end_ms = 10;
    second.token = 2;
    replacement.token = 3;
    start_virtual_audio();
    CHECK(hal_audio_play(&first, 0, &second, false));
    hal_audio_queue_next(&replacement);
    sim_audio_tick(15);
    CHECK_EQUAL(3, hal_audio_status().token);
    sim_audio_stop();
}

static void repeat_marks_loop_between_two_playback_positions(void)
{
    start_virtual_playback();
    sim_audio_tick(10);
    CHECK_EQUAL(1, hal_audio_repeat_mark(false));
    sim_audio_tick(20);
    CHECK_EQUAL(2, hal_audio_repeat_mark(false));
    CHECK_EQUAL(10, hal_audio_status().position_ms);
    sim_audio_tick(55);
    uint32_t position = hal_audio_status().position_ms;
    CHECK(position >= 10 && position < 30);
    CHECK_EQUAL(0, hal_audio_repeat_mark(false));
    CHECK_EQUAL(1, hal_audio_repeat_mark(false));
    CHECK_EQUAL(0, hal_audio_repeat_mark(true));
    sim_audio_stop();
}

static void a_visual_read_preserves_position_and_reports_the_stream(void)
{
    start_virtual_playback();
    sim_audio_tick(20);
    uint32_t before = hal_audio_status().position_ms;
    AudioVisualBuffer visual = hal_audio_visual_buffer();
    CHECK(visual.words != NULL);
    CHECK_EQUAL(1024, visual.frames);
    CHECK(visual.count > 0);
    CHECK_EQUAL(44100, visual.rate);
    CHECK_EQUAL(before, hal_audio_status().position_ms);
    sim_audio_stop();
}

static void zero_volume_silences_the_visual_stream(void)
{
    start_virtual_playback();
    hal_audio_set_volume(0);
    AudioVisualBuffer visual = hal_audio_visual_buffer();
    CHECK(visual.count > 0);
    for (uint32_t sample = 0; sample < visual.count * 2; sample++) {
        CHECK_EQUAL(0, visual.words[sample]);
    }
    sim_audio_stop();
}

static void a_headphone_profile_is_applied_and_returned(void)
{
    HeadphoneAudio profile = {
        .left = REPLAY_GAIN_UNITY / 2, .right = REPLAY_GAIN_UNITY, .ceiling = 12};
    start_virtual_playback();
    hal_audio_set_headphones(profile);
    HeadphoneAudio active = hal_audio_headphones();
    CHECK_EQUAL(profile.left, active.left);
    CHECK_EQUAL(profile.right, active.right);
    CHECK_EQUAL(profile.ceiling, active.ceiling);
    hal_audio_set_volume(HAL_AUDIO_VOLUME_MAX);
    hal_audio_set_replay_gain(TRACK.token, REPLAY_GAIN_UNITY / 2);
    hal_audio_set_replay_gain(77, REPLAY_GAIN_UNITY);
    CHECK(hal_audio_visual_buffer().count > 0);
    sim_audio_stop();
}

static void sleep_fade_and_configuration_changes_do_not_interrupt_playback(void)
{
    start_virtual_playback();
    HalAudioEqualizer equalizer = {.band_count = 1, .pre_gain = HAL_AUDIO_UNITY_PRE_GAIN};
    HalAudioEffects effects = {.crossfeed = 2, .skip_silence = true, .speech_boost = true};
    hal_audio_set_sleep_remaining(90000);
    hal_audio_set_sleep_remaining(60000);
    hal_audio_set_sleep_remaining(30000);
    CHECK(hal_audio_set_gain(HAL_AUDIO_GAIN_HIGH));
    CHECK(hal_audio_set_filter(HAL_AUDIO_FILTER_NON_OVERSAMPLING));
    hal_audio_set_output(HAL_AUDIO_OUTPUT_BALANCED);
    hal_audio_set_equalizer(&equalizer);
    hal_audio_set_effects(effects);
    hal_audio_set_bluetooth_volume(10);
    hal_audio_service();
    hal_audio_reset_stats();
    CHECK_EQUAL(HAL_AUDIO_PLAYING, hal_audio_status().state);
    CHECK_EQUAL(0, hal_audio_stats().underruns);
    sim_audio_stop();
}

static void high_rate_playback_ignores_tempo_scaling(void)
{
    start_virtual_audio();
    CHECK(hal_audio_play(&HIGH_RATE, 0, NULL, false));
    hal_audio_set_speed(200);
    sim_audio_tick(20);
    CHECK_EQUAL(20, hal_audio_status().position_ms);
    sim_audio_stop();
}

static void simulator_audio_reports_its_fixed_hardware_contract(void)
{
    start_virtual_audio();
    CHECK_EQUAL(HAL_TONE_PLAYED, hal_audio_test_tone());
    CHECK_EQUAL(0, hal_audio_tone_detail().stage);
    CHECK_EQUAL(DAC_IDENTITY_SIMULATED, hal_audio_dac_identity(0).source);
    CHECK_EQUAL(HAL_AUDIO_JACK_SINGLE_ENDED, hal_audio_jack());
    HalJackPins pins = hal_audio_jack_pins();
    CHECK(!pins.single_ended.high);
    CHECK(pins.balanced.high);
    CHECK_EQUAL(HAL_AUDIO_PROBLEM_UNREADABLE, hal_audio_refusal().problem);
    CHECK_EQUAL(HAL_TONE_PLAYED, hal_audio_dma_tone_start(48000));
    CHECK(hal_audio_dma_tone_status().running);
    CHECK_EQUAL(0, hal_audio_dma_tone_status().position);
    hal_audio_dma_tone_stop();
    CHECK(!hal_audio_dma_tone_status().running);
    sim_audio_stop();
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
        TEST_CASE(a_missing_recording_reports_failure_without_leaving_audio_active),
        TEST_CASE(pause_resume_seek_and_release_preserve_the_public_state),
        TEST_CASE(paused_start_waits_until_resume),
        TEST_CASE(queued_recording_takes_over_at_the_requested_end),
        TEST_CASE(clearing_the_queue_stops_at_the_requested_end),
        TEST_CASE(a_replacement_queue_entry_is_the_one_that_plays),
        TEST_CASE(repeat_marks_loop_between_two_playback_positions),
        TEST_CASE(a_visual_read_preserves_position_and_reports_the_stream),
        TEST_CASE(zero_volume_silences_the_visual_stream),
        TEST_CASE(a_headphone_profile_is_applied_and_returned),
        TEST_CASE(sleep_fade_and_configuration_changes_do_not_interrupt_playback),
        TEST_CASE(high_rate_playback_ignores_tempo_scaling),
        TEST_CASE(simulator_audio_reports_its_fixed_hardware_contract),
        TEST_CASE(virtual_playback_moves_through_the_recording_at_the_chosen_speed),
        TEST_CASE(normal_speed_follows_the_clock),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
