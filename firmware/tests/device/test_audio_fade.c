#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/audio_engine.h"
#include "device/audio_fade.h"

enum {
    RING_FRAMES = 8192,
    RING_WORDS = 2 * RING_FRAMES,
    STRETCH_WORDS = 6000,
    FULL_VOLUME = 60,
    BLOCK = 256,
    LEVEL = 16384,
    RATE = 48000,
    LOW_RATE = 16000,
    LOW_OUTPUT_RATE = 32000,
    CHUNK = 1024,
    SMALL_CHUNK = 37,
    CAPTURE_FRAMES = 9 * RATE,
    TRACK_SECONDS = 4,
    FADE_MS = 1000,
    UNITY_Q16 = 65536
};

static const double RAMP_TOLERANCE = 0.0003;

typedef struct ToneSource {
    int32_t left[BLOCK];
    int32_t right[BLOCK];
    uint32_t rate;
    uint32_t total;
    uint32_t next_sample;
    bool unknown_length;
} ToneSource;

static ToneSource tone;
static uint32_t ring[RING_WORDS];
static int32_t captured[CAPTURE_FRAMES][2];
static AudioEngine engine;
static uint32_t consumed;
static uint32_t captured_frames;

static bool tone_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    ToneSource *source = context;
    source->rate = path[0] == 'l' ? LOW_RATE : RATE;
    source->total = TRACK_SECONDS * source->rate;
    uint32_t start = (uint32_t)((uint64_t)start_ms * source->rate / 1000);
    source->next_sample = start - start % BLOCK;
    *track =
        (AudioTrackInfo){.stream = {.sample_rate = source->rate,
                                    .channels = 2,
                                    .bits_per_sample = 16,
                                    .max_block = BLOCK,
                                    .total_samples = source->unknown_length ? 0 : source->total}};
    return true;
}

static AudioFrameResult tone_next(void *context, AudioFrame *frame)
{
    ToneSource *source = context;
    if (source->next_sample >= source->total) {
        return AUDIO_FRAME_END;
    }
    uint32_t left = source->total - source->next_sample;
    *frame = (AudioFrame){.left = source->left,
                          .right = source->right,
                          .first_sample = source->next_sample,
                          .samples = left < BLOCK ? left : BLOCK,
                          .bits = 16};
    source->next_sample += frame->samples;
    return AUDIO_FRAME_READY;
}

static void tone_release(void *context)
{
    (void)context;
}

static void tone_close(void *context)
{
    (void)context;
}

static void start_engine(void)
{
    tone = (ToneSource){0};
    for (int index = 0; index < BLOCK; index++) {
        tone.left[index] = LEVEL;
        tone.right[index] = -LEVEL;
    }
    AudioSource source = {tone_open, tone_next, tone_release, tone_close, &tone};
    audio_engine_init(&engine, (AudioEngineConfig){.source = source,
                                                   .ring = ring,
                                                   .ring_words = RING_WORDS,
                                                   .stretch_words = STRETCH_WORDS,
                                                   .resample_render = audio_resample_render});
    audio_engine_set_volume(&engine, FULL_VOLUME);
    consumed = 0;
    captured_frames = 0;
}

static void play(const char *path, uint32_t replay_gain)
{
    AudioEngineTrack track = {.path = path, .token = 1, .replay_gain = replay_gain};
    CHECK(audio_engine_play(&engine, &track, 0));
}

static void queue(const char *path, uint32_t fade_ms, uint32_t replay_gain)
{
    AudioEngineTrack track = {
        .path = path, .token = 2, .replay_gain = replay_gain, .fade_ms = fade_ms};
    audio_engine_queue_next(&engine, &track);
}

static void capture_in(uint32_t frames, uint32_t chunk)
{
    uint32_t end = captured_frames + frames;
    while (captured_frames < end && captured_frames < CAPTURE_FRAMES) {
        for (int round = 0; round < 64; round++) {
            audio_engine_pump(&engine, consumed);
        }
        for (uint32_t index = 0; index < chunk && captured_frames < CAPTURE_FRAMES; index++) {
            uint32_t slot = 2 * ((consumed + index) % (engine.config.ring_words / 2));
            captured[captured_frames][0] = (int32_t)ring[slot] >> 8;
            captured[captured_frames][1] = (int32_t)ring[slot + 1] >> 8;
            captured_frames++;
        }
        consumed += chunk;
    }
}

static void capture(uint32_t frames)
{
    capture_in(frames, CHUNK);
}

static double smooth(double x)
{
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * (3 - 2 * x);
}

static double gain_at(uint32_t frame)
{
    return (double)captured[frame][0] / (LEVEL * 256.0);
}

static bool near(double expected, double actual, double tolerance)
{
    double difference = expected - actual;
    return difference <= tolerance && difference >= -tolerance;
}

static bool channels_match(uint32_t first, uint32_t count)
{
    bool same = true;
    for (uint32_t frame = first; same && frame < first + count; frame++) {
        same = captured[frame][1] == -captured[frame][0];
    }
    return same;
}

static bool follows_ramp(uint32_t first, uint32_t count, bool falling, double length)
{
    double worst = 0;
    for (uint32_t index = 0; index < count; index++) {
        double position = falling ? (double)(count - index) : (double)index;
        double error = smooth(position / length) - gain_at(first + index);
        error = error < 0 ? -error : error;
        worst = error > worst ? error : worst;
    }
    return worst <= RAMP_TOLERANCE;
}

static double step_limit(uint32_t length)
{
    return 1.5 / length + 4.0 / AUDIO_FADE_UNITY;
}

static double largest_gain_step(uint32_t first, uint32_t count)
{
    double largest = 0;
    for (uint32_t frame = first + 1; frame < first + count; frame++) {
        double step = gain_at(frame) - gain_at(frame - 1);
        step = step < 0 ? -step : step;
        largest = step > largest ? step : largest;
    }
    return largest;
}

static bool exactly_level(uint32_t first, uint32_t count, int32_t level)
{
    bool exact = true;
    for (uint32_t frame = first; exact && frame < first + count; frame++) {
        exact = captured[frame][0] == level && captured[frame][1] == -level;
    }
    return exact;
}

static void fill(uint32_t *words, uint32_t frames, int32_t sample)
{
    for (uint32_t frame = 0; frame < frames; frame++) {
        words[2 * frame] = (uint32_t)sample << 8;
        words[2 * frame + 1] = (uint32_t)-sample << 8;
    }
}

static AudioFadeSpan rising(uint32_t length)
{
    return (AudioFadeSpan){
        .first = 0, .step = 1, .length = length, .reciprocal = UINT32_MAX / length};
}

static void the_ramp_rises_from_silence_to_exact_unity_along_a_smooth_curve(void)
{
    static uint32_t words[2 * 1100];
    fill(words, 1100, 8388607);
    audio_fade_run(words, 1100, rising(1000));
    CHECK_EQUAL(0, (int32_t)words[0] >> 8);
    CHECK(near(0.5, ((int32_t)words[2 * 500] >> 8) / 8388607.0, 0.0002));
    CHECK(near(0.15625, ((int32_t)words[2 * 250] >> 8) / 8388607.0, 0.0002));
    CHECK_EQUAL(8388607, (int32_t)words[2 * 1000] >> 8);
    CHECK_EQUAL(8388607, (int32_t)words[2 * 1099] >> 8);
    bool monotonic = true;
    for (int frame = 1; frame < 1100; frame++) {
        monotonic = monotonic && (int32_t)words[2 * frame] >= (int32_t)words[2 * frame - 2];
        int32_t sum = ((int32_t)words[2 * frame + 1] >> 8) + ((int32_t)words[2 * frame] >> 8);
        monotonic = monotonic && sum >= -1 && sum <= 1;
    }
    CHECK(monotonic);
}

static void the_ramp_handles_full_scale_negative_samples_without_overflow(void)
{
    static uint32_t words[2 * 64];
    fill(words, 64, -8388608);
    audio_fade_run(words, 64, rising(32));
    CHECK_EQUAL(-8388608, (int32_t)words[2 * 63] >> 8);
    CHECK(((int32_t)words[2 * 16] >> 8) < 0);
    CHECK(((int32_t)words[2 * 16] >> 8) > -8388608);
    CHECK_EQUAL(0, words[0] & 0xFF);
}

static void a_falling_ramp_mirrors_the_rising_one(void)
{
    static uint32_t up[2 * 400];
    static uint32_t down[2 * 400];
    fill(up, 400, 1000000);
    fill(down, 400, 1000000);
    audio_fade_run(up, 400, rising(400));
    AudioFadeSpan falling = rising(400);
    falling.first = 400;
    falling.step = -1;
    audio_fade_run(down, 400, falling);
    bool mirrored = true;
    for (int frame = 1; frame < 400; frame++) {
        mirrored = mirrored && up[2 * frame] == down[2 * (400 - frame)];
    }
    CHECK(mirrored);
}

static void a_faded_transition_fades_out_the_end_and_fades_in_the_next_track(void)
{
    start_engine();
    play("a", 0);
    queue("b", FADE_MS, 0);
    capture(CAPTURE_FRAMES);
    uint32_t length = FADE_MS * RATE / 1000;
    uint32_t boundary = TRACK_SECONDS * RATE;
    CHECK(exactly_level(0, boundary - length, LEVEL * 256));
    CHECK(follows_ramp(boundary - length, length, true, length));
    CHECK(follows_ramp(boundary, length, false, length));
    CHECK(exactly_level(boundary + length, 3 * RATE - length, LEVEL * 256));
    CHECK(channels_match(0, CAPTURE_FRAMES));
}

static void the_gain_changes_by_a_tiny_step_on_every_sample(void)
{
    start_engine();
    play("a", 0);
    queue("b", FADE_MS, 0);
    capture(CAPTURE_FRAMES);
    uint32_t length = FADE_MS * RATE / 1000;
    CHECK(largest_gain_step(0, 2 * TRACK_SECONDS * RATE) <= step_limit(length));
}

static void a_transition_without_a_fade_stays_gapless_and_bit_exact(void)
{
    start_engine();
    play("a", 0);
    queue("b", 0, 0);
    capture(CAPTURE_FRAMES);
    CHECK(exactly_level(0, CAPTURE_FRAMES - RATE, LEVEL * 256));
}

static void parts_of_one_file_never_fade(void)
{
    start_engine();
    AudioEngineTrack first = {.path = "a", .end_ms = 2000, .token = 1};
    CHECK(audio_engine_play(&engine, &first, 0));
    AudioEngineTrack second = {.path = "a", .start_ms = 2000, .token = 2, .fade_ms = FADE_MS};
    audio_engine_queue_next(&engine, &second);
    capture(3 * RATE);
    CHECK(exactly_level(0, 3 * RATE, LEVEL * 256));
}

static void replay_gain_and_the_fade_multiply(void)
{
    start_engine();
    play("a", UNITY_Q16 / 2);
    queue("b", FADE_MS, UNITY_Q16 / 4);
    capture(CAPTURE_FRAMES);
    uint32_t length = FADE_MS * RATE / 1000;
    uint32_t boundary = TRACK_SECONDS * RATE;
    CHECK_EQUAL(LEVEL * 128, captured[RATE][0]);
    CHECK(near(0.5 * smooth(0.5), gain_at(boundary - length / 2), 0.0002));
    CHECK(near(0.25 * smooth(0.5), gain_at(boundary + length / 2), 0.0002));
    CHECK_EQUAL(LEVEL * 64, captured[boundary + 2 * length][0]);
}

static void seeking_into_the_fade_out_continues_from_that_point_of_the_curve(void)
{
    start_engine();
    play("a", 0);
    queue("b", FADE_MS, 0);
    capture(RATE);
    CHECK(audio_engine_seek(&engine, 3500));
    consumed = 0;
    captured_frames = 0;
    capture(RATE);
    CHECK(near(smooth(0.5), gain_at(0), 0.001));
    CHECK(near(smooth(0.25), gain_at(RATE / 4), 0.001));
}

static void seeking_during_the_fade_in_plays_the_next_track_at_full_level(void)
{
    start_engine();
    play("a", 0);
    queue("b", FADE_MS, 0);
    capture(TRACK_SECONDS * RATE + RATE / 4);
    CHECK_EQUAL(2u, audio_engine_status(&engine).token);
    CHECK(audio_engine_seek(&engine, 500));
    consumed = 0;
    captured_frames = 0;
    capture(RATE);
    CHECK(exactly_level(0, RATE, LEVEL * 256));
}

static void pausing_during_a_fade_keeps_its_shape(void)
{
    start_engine();
    play("a", 0);
    queue("b", FADE_MS, 0);
    capture(TRACK_SECONDS * RATE - RATE / 2);
    audio_engine_pause(&engine);
    for (int round = 0; round < 200; round++) {
        audio_engine_pump(&engine, consumed);
    }
    audio_engine_resume(&engine);
    capture(2 * RATE);
    uint32_t length = FADE_MS * RATE / 1000;
    uint32_t boundary = TRACK_SECONDS * RATE;
    CHECK(follows_ramp(boundary - length, length, true, length));
    CHECK(follows_ramp(boundary, length, false, length));
}

static void a_fade_out_that_has_begun_finishes_and_fades_in_when_the_queue_changes(void)
{
    start_engine();
    play("a", 0);
    queue("b", FADE_MS, 0);
    capture(TRACK_SECONDS * RATE - RATE / 2);
    queue("b", 0, 0);
    capture(2 * RATE);
    uint32_t length = FADE_MS * RATE / 1000;
    uint32_t boundary = TRACK_SECONDS * RATE;
    CHECK(follows_ramp(boundary - length, length, true, length));
    CHECK(follows_ramp(boundary, length, false, length));
    CHECK(exactly_level(boundary + length, RATE / 4, LEVEL * 256));
}

static void a_fade_in_mirrors_its_ramp_over_the_tail_before_its_start(void)
{
    static uint32_t words[2 * 64];
    fill(words, 64, 1000000);
    AudioFadeRamp ramp = {.start = 20, .length = 32, .reciprocal = UINT32_MAX / 32, .active = true};
    audio_fade_in((AudioFadeBlock){.ring = words, .ring_frames = 64, .first = 0, .frames = 64},
                  &ramp);
    bool mirrored = true;
    for (int frame = 0; frame < 20; frame++) {
        mirrored = mirrored && words[2 * frame] == words[2 * (40 - frame)];
    }
    CHECK(mirrored);
    CHECK_EQUAL(0, (int32_t)words[2 * 20] >> 8);
    CHECK_EQUAL(1000000, (int32_t)words[2 * 52] >> 8);
    CHECK(!ramp.active);
}

static void a_track_of_unknown_length_only_fades_the_next_one_in(void)
{
    start_engine();
    tone.unknown_length = true;
    play("a", 0);
    queue("b", FADE_MS, 0);
    capture(TRACK_SECONDS * RATE + 2 * RATE);
    uint32_t length = FADE_MS * RATE / 1000;
    uint32_t boundary = TRACK_SECONDS * RATE;
    CHECK(exactly_level(0, boundary, LEVEL * 256));
    CHECK(follows_ramp(boundary, length, false, length));
}

static void a_converted_low_rate_track_fades_on_the_output_clock(void)
{
    start_engine();
    play("low", 0);
    queue("low2", FADE_MS, 0);
    capture(CAPTURE_FRAMES);
    CHECK_EQUAL(LOW_OUTPUT_RATE, audio_engine_rate(&engine));
    uint32_t length = FADE_MS * LOW_OUTPUT_RATE / 1000;
    uint32_t boundary = TRACK_SECONDS * LOW_OUTPUT_RATE;
    CHECK(near(1.0, gain_at(boundary - length - LOW_OUTPUT_RATE / 4), 0.001));
    CHECK(near(smooth(0.25), gain_at(boundary - length / 4), 0.002));
    CHECK(near(smooth(0.75), gain_at(boundary - 3 * length / 4), 0.002));
    CHECK(near(smooth(0.5), gain_at(boundary + length / 2), 0.002));
    CHECK(near(1.0, gain_at(boundary + length + LOW_OUTPUT_RATE / 4), 0.001));
    CHECK(largest_gain_step(LOW_OUTPUT_RATE, 6 * LOW_OUTPUT_RATE) <= step_limit(length));
}

static void a_converted_fade_stays_smooth_when_the_ring_frees_a_few_frames_at_a_time(void)
{
    start_engine();
    play("low", 0);
    queue("low2", FADE_MS, 0);
    capture_in(CAPTURE_FRAMES, SMALL_CHUNK);
    uint32_t length = FADE_MS * LOW_OUTPUT_RATE / 1000;
    uint32_t boundary = TRACK_SECONDS * LOW_OUTPUT_RATE;
    CHECK(largest_gain_step(boundary - length, 2 * length) <= step_limit(length));
    CHECK(near(smooth(0.5), gain_at(boundary - length / 2), 0.002));
}

static uint32_t stretched(uint32_t frames, uint16_t speed)
{
    return (uint32_t)((200ull * frames + speed) / (2u * speed));
}

static void stretched_tracks_fade_out_and_in_exactly_at_the_stretched_boundary(void)
{
    static const uint16_t SPEEDS[] = {50, 75, 150, 200, 300};
    for (size_t at = 0; at < sizeof SPEEDS / sizeof SPEEDS[0]; at++) {
        start_engine();
        audio_engine_set_speed(&engine, SPEEDS[at]);
        play("a", 0);
        queue("b", FADE_MS, 0);
        uint32_t length = FADE_MS * RATE / 1000;
        uint32_t boundary = stretched(TRACK_SECONDS * RATE, SPEEDS[at]);
        capture(boundary + length + RATE / 2);
        CHECK_EQUAL(STRETCH_WORDS, engine.config.ring_words);
        CHECK(exactly_level(0, boundary - length, LEVEL * 256));
        CHECK(follows_ramp(boundary - length, length, true, length));
        CHECK(follows_ramp(boundary, length, false, length));
        uint32_t end = captured_frames < 2 * boundary ? captured_frames : 2 * boundary;
        CHECK(exactly_level(boundary + length, end - boundary - length - 1, LEVEL * 256));
        CHECK(channels_match(0, captured_frames));
        CHECK_EQUAL(2u, audio_engine_status(&engine).token);
    }
}

static void stretched_speech_fades_on_the_output_clock_after_conversion(void)
{
    static const uint16_t SPEEDS[] = {50, 150, 200, 300};
    for (size_t at = 0; at < sizeof SPEEDS / sizeof SPEEDS[0]; at++) {
        start_engine();
        audio_engine_set_speed(&engine, SPEEDS[at]);
        play("low", 0);
        queue("low2", FADE_MS, 0);
        uint32_t length = FADE_MS * LOW_OUTPUT_RATE / 1000;
        uint32_t boundary = 2 * stretched(TRACK_SECONDS * LOW_RATE, SPEEDS[at]);
        capture(boundary + length + LOW_OUTPUT_RATE / 2);
        CHECK_EQUAL(LOW_OUTPUT_RATE, audio_engine_rate(&engine));
        CHECK(near(1.0, gain_at(boundary - length - LOW_OUTPUT_RATE / 4), 0.001));
        CHECK(near(smooth(0.25), gain_at(boundary - length / 4), 0.002));
        CHECK(near(smooth(0.75), gain_at(boundary - 3 * length / 4), 0.002));
        CHECK(near(smooth(0.5), gain_at(boundary + length / 2), 0.002));
        CHECK(near(1.0, gain_at(boundary + length + LOW_OUTPUT_RATE / 4), 0.001));
        CHECK(largest_gain_step(LOW_OUTPUT_RATE, boundary) <= step_limit(length));
    }
}

static void a_stretched_track_without_a_queued_fade_ends_at_full_level(void)
{
    start_engine();
    audio_engine_set_speed(&engine, 150);
    play("a", 0);
    queue("b", 0, 0);
    uint32_t boundary = stretched(TRACK_SECONDS * RATE, 150);
    capture(boundary + RATE);
    CHECK(exactly_level(0, boundary + RATE, LEVEL * 256));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_ramp_rises_from_silence_to_exact_unity_along_a_smooth_curve),
        TEST_CASE(the_ramp_handles_full_scale_negative_samples_without_overflow),
        TEST_CASE(a_falling_ramp_mirrors_the_rising_one),
        TEST_CASE(a_faded_transition_fades_out_the_end_and_fades_in_the_next_track),
        TEST_CASE(the_gain_changes_by_a_tiny_step_on_every_sample),
        TEST_CASE(a_transition_without_a_fade_stays_gapless_and_bit_exact),
        TEST_CASE(parts_of_one_file_never_fade),
        TEST_CASE(replay_gain_and_the_fade_multiply),
        TEST_CASE(seeking_into_the_fade_out_continues_from_that_point_of_the_curve),
        TEST_CASE(seeking_during_the_fade_in_plays_the_next_track_at_full_level),
        TEST_CASE(pausing_during_a_fade_keeps_its_shape),
        TEST_CASE(a_fade_out_that_has_begun_finishes_and_fades_in_when_the_queue_changes),
        TEST_CASE(a_fade_in_mirrors_its_ramp_over_the_tail_before_its_start),
        TEST_CASE(a_track_of_unknown_length_only_fades_the_next_one_in),
        TEST_CASE(a_converted_low_rate_track_fades_on_the_output_clock),
        TEST_CASE(a_converted_fade_stays_smooth_when_the_ring_frees_a_few_frames_at_a_time),
        TEST_CASE(stretched_tracks_fade_out_and_in_exactly_at_the_stretched_boundary),
        TEST_CASE(stretched_speech_fades_on_the_output_clock_after_conversion),
        TEST_CASE(a_stretched_track_without_a_queued_fade_ends_at_full_level),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
