#include <math.h>
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "device/audio_engine.h"

enum {
    CAPACITY = 96000,
    OUTPUT_CAPACITY = 4 * CAPACITY,
    RING_WORDS = 28000,
    STRETCH_WORDS = 20000,
    CHUNK = 127,
    FULL_FRAMES = 6002
};

static AudioEngine engine;
static AudioTempo reference_tempo;
static uint8_t reference_buffer[FULL_FRAMES * AUDIO_TEMPO_FRAME_BYTES];
static uint32_t ring[RING_WORDS];
static int32_t input[CAPACITY][2];
static int32_t stretched_input[2 * CAPACITY][2];
static int32_t expected[OUTPUT_CAPACITY][2];
static int32_t actual[OUTPUT_CAPACITY][2];
static uint32_t frame_count;
static uint32_t source_at;
static uint32_t source_rate;
static uint32_t second_rate;
static uint32_t first_ms;

static bool source_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    (void)context;
    uint32_t rate = path[0] == 's' && second_rate != 0 ? second_rate : source_rate;
    source_at = (uint32_t)((uint64_t)start_ms * rate / 1000);
    first_ms = start_ms;
    *track = (AudioTrackInfo){.stream = {.sample_rate = rate,
                                         .total_samples = frame_count,
                                         .channels = 2,
                                         .bits_per_sample = 24}};
    return true;
}

static AudioFrameResult source_next(void *context, AudioFrame *frame)
{
    (void)context;
    if (source_at >= frame_count) {
        return AUDIO_FRAME_END;
    }
    uint32_t count = frame_count - source_at;
    count = count < CHUNK ? count : CHUNK;
    static int32_t left[CHUNK];
    static int32_t right[CHUNK];
    for (uint32_t index = 0; index < count; index++) {
        left[index] = input[source_at + index][0];
        right[index] = input[source_at + index][1];
    }
    *frame = (AudioFrame){left, right, source_at, count, 24};
    source_at += count;
    return AUDIO_FRAME_READY;
}

static void source_release(void *context)
{
    (void)context;
}

static void setup(uint32_t rate, uint32_t frames, uint16_t speed)
{
    source_rate = rate;
    second_rate = 0;
    frame_count = frames;
    source_at = 0;
    for (uint32_t index = 0; index < CAPACITY; index++) {
        double phase = 6.283185307179586 * index / rate;
        input[index][0] = (int32_t)lround(2000000 * sin(137 * phase) + 700000 * sin(523 * phase));
        input[index][1] = (int32_t)lround(-1800000 * sin(181 * phase));
    }
    AudioSource source = {source_open, source_next, source_release, source_release, NULL};
    audio_engine_init(&engine, (AudioEngineConfig){.source = source,
                                                   .ring = ring,
                                                   .ring_words = RING_WORDS,
                                                   .stretch_words = STRETCH_WORDS,
                                                   .resample_render = audio_resample_render});
    audio_engine_set_speed(&engine, speed);
}

static uint32_t stretch(uint32_t rate, uint16_t speed, uint32_t start, uint32_t frames)
{
    CHECK(audio_tempo_start(&reference_tempo,
                            (AudioTempoConfig){.buffer = {reference_buffer, NULL, FULL_FRAMES, 0},
                                               .rate = rate,
                                               .speed = speed}));
    uint32_t made = 0;
    for (uint32_t index = start; index <= start + frames; index++) {
        if (index == start + frames) {
            audio_tempo_finish(&reference_tempo);
        } else if (!audio_tempo_push(&reference_tempo, input[index][0], input[index][1])) {
            while (audio_tempo_pull(&reference_tempo, &stretched_input[made][0],
                                    &stretched_input[made][1])) {
                made++;
            }
            CHECK(audio_tempo_push(&reference_tempo, input[index][0], input[index][1]));
        }
        while (audio_tempo_pull(&reference_tempo, &stretched_input[made][0],
                                &stretched_input[made][1])) {
            made++;
        }
    }
    return made;
}

static uint32_t convert(uint32_t rate, uint32_t frames)
{
    AudioResample resample;
    audio_resample_start(&resample, rate);
    uint32_t pushed = 0;
    uint32_t made = 0;
    while (true) {
        pushed += audio_resample_push(&resample, stretched_input[pushed], frames - pushed);
        if (pushed == frames) {
            audio_resample_finish(&resample);
        }
        uint32_t got = audio_resample_render(&resample, (uint32_t *)expected[made], 256);
        made += got;
        if (got == 0 && pushed == frames) {
            return made;
        }
    }
}

static uint32_t reference(uint32_t rate, uint16_t speed, uint32_t start, uint32_t frames)
{
    uint32_t made = stretch(rate, speed, start, frames);
    if (audio_resample_rate(rate) != rate) {
        uint32_t converted = convert(rate, made);
        for (uint32_t frame = 0; frame < converted; frame++) {
            expected[frame][0] >>= 8;
            expected[frame][1] >>= 8;
        }
        return converted;
    }
    memcpy(expected, stretched_input, made * sizeof expected[0]);
    return made;
}

static uint32_t render_from(uint32_t consumed, uint32_t limit)
{
    uint32_t taken = 0;
    for (uint32_t round = 0; round < 200000 && !engine.ended && taken < limit; round++) {
        audio_engine_pump(&engine, consumed);
        uint32_t end = engine.ended ? engine.end_at : engine.produced;
        while (consumed < end && taken < limit) {
            uint32_t slot = 2 * (consumed % (engine.config.ring_words / 2));
            actual[taken][0] = (int32_t)ring[slot] >> 8;
            actual[taken][1] = (int32_t)ring[slot + 1] >> 8;
            consumed++;
            taken++;
        }
    }
    return taken;
}

static uint32_t render(void)
{
    uint32_t count = render_from(0, OUTPUT_CAPACITY);
    CHECK(engine.ended);
    return count;
}

static void play(const char *path, uint32_t token)
{
    AudioEngineTrack track = {.path = path, .token = token};
    CHECK(audio_engine_play(&engine, &track, 0));
}

static void queue(const char *path, uint32_t token)
{
    AudioEngineTrack track = {.path = path, .token = token};
    audio_engine_queue_next(&engine, &track);
}

static uint32_t rounded(uint32_t frames, uint16_t speed)
{
    return (uint32_t)((200ull * frames + speed) / (2u * speed));
}

static void native_rates_stretch_through_the_kernel_with_the_rounded_duration(void)
{
    static const uint32_t RATES[] = {44100, 48000};
    static const uint16_t SPEEDS[] = {50, 75, 125, 150, 175, 200, 250, 300};
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        for (size_t speed = 0; speed < sizeof SPEEDS / sizeof SPEEDS[0]; speed++) {
            setup(RATES[rate], 30011, SPEEDS[speed]);
            play("first", 1);
            CHECK_EQUAL(STRETCH_WORDS, engine.config.ring_words);
            uint32_t count = render();
            CHECK_EQUAL(rounded(30011, SPEEDS[speed]), count);
            CHECK_EQUAL(reference(RATES[rate], SPEEDS[speed], 0, 30011), count);
            CHECK(memcmp(expected, actual, count * sizeof actual[0]) == 0);
        }
    }
}

static void speech_is_stretched_at_its_own_rate_before_conversion(void)
{
    static const uint32_t RATES[] = {8000, 16000, 22050};
    static const uint16_t SPEEDS[] = {50, 150, 200, 300};
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        for (size_t speed = 0; speed < sizeof SPEEDS / sizeof SPEEDS[0]; speed++) {
            setup(RATES[rate], 12007, SPEEDS[speed]);
            play("first", 1);
            uint32_t count = render();
            uint32_t factor = audio_resample_rate(RATES[rate]) / RATES[rate];
            CHECK_EQUAL(factor * rounded(12007, SPEEDS[speed]), count);
            CHECK_EQUAL(reference(RATES[rate], SPEEDS[speed], 0, 12007), count);
            CHECK(memcmp(expected, actual, count * sizeof actual[0]) == 0);
        }
    }
}

static void normal_speed_keeps_the_whole_ring_and_the_exact_samples(void)
{
    setup(48000, 20000, 100);
    play("first", 1);
    CHECK_EQUAL(RING_WORDS, engine.config.ring_words);
    CHECK_EQUAL(20000, render());
    CHECK(memcmp(input, actual, 20000 * sizeof actual[0]) == 0);
}

static void rates_above_48_khz_play_at_normal_speed_with_the_whole_ring(void)
{
    setup(96000, 48000, 150);
    play("first", 1);
    CHECK_EQUAL(RING_WORDS, engine.config.ring_words);
    CHECK_EQUAL(48000, render());
    CHECK(memcmp(input, actual, 48000 * sizeof actual[0]) == 0);
    setup(96000, 48000, 150);
    play("first", 1);
    render_from(0, 9600);
    audio_engine_observe(&engine, 9600);
    CHECK_EQUAL(100, audio_engine_status(&engine).position_ms);
}

static void positions_follow_the_recording_not_the_output(void)
{
    static const uint16_t SPEEDS[] = {50, 150, 200, 300};
    static const uint32_t EXPECTED_MS[] = {50, 150, 200, 300};
    for (size_t at = 0; at < sizeof SPEEDS / sizeof SPEEDS[0]; at++) {
        setup(48000, 48000, SPEEDS[at]);
        play("first", 1);
        render_from(0, 4800);
        audio_engine_observe(&engine, 4800);
        CHECK_EQUAL(EXPECTED_MS[at], audio_engine_status(&engine).position_ms);
    }
}

static void seeking_empties_the_stage_and_restarts_from_the_source_position(void)
{
    setup(16000, 32000, 150);
    play("first", 1);
    render_from(0, 9000);
    audio_engine_pause(&engine);
    CHECK(audio_engine_seek(&engine, 750));
    CHECK_EQUAL(750, first_ms);
    CHECK_EQUAL(AUDIO_ENGINE_PAUSED, audio_engine_status(&engine).state);
    CHECK_EQUAL(750, audio_engine_status(&engine).position_ms);
    audio_engine_resume(&engine);
    uint32_t count = render();
    CHECK_EQUAL(reference(16000, 150, 12000, 20000), count);
    CHECK(memcmp(expected, actual, count * sizeof actual[0]) == 0);
}

static void a_queued_file_continues_in_the_stage_and_switches_at_the_stretched_boundary(void)
{
    setup(48000, 24000, 150);
    play("first", 1);
    queue("second", 2);
    uint32_t count = render();
    for (uint32_t index = 0; index < 24000; index++) {
        input[24000 + index][0] = input[index][0];
        input[24000 + index][1] = input[index][1];
    }
    CHECK_EQUAL(reference(48000, 150, 0, 48000), count);
    CHECK(memcmp(expected, actual, count * sizeof actual[0]) == 0);
    setup(48000, 24000, 150);
    play("first", 1);
    queue("second", 2);
    uint32_t boundary = rounded(24000, 150);
    render_from(0, boundary - 1);
    audio_engine_observe(&engine, boundary - 1);
    CHECK_EQUAL(1u, audio_engine_status(&engine).token);
    CHECK(engine.produced >= boundary);
    audio_engine_observe(&engine, boundary);
    CHECK_EQUAL(2u, audio_engine_status(&engine).token);
    CHECK_EQUAL(0, audio_engine_status(&engine).position_ms);
}

static void another_input_rate_on_the_same_output_clock_restarts_the_stage(void)
{
    setup(16000, 8000, 200);
    second_rate = 8000;
    play("first", 1);
    queue("second", 2);
    uint32_t count = render();
    CHECK_EQUAL(32000, audio_engine_rate(&engine));
    CHECK_EQUAL(2 * rounded(8000, 200) + 4 * rounded(8000, 200), count);
    uint32_t first = 2 * rounded(8000, 200);
    uint32_t second = reference(8000, 200, 0, 8000);
    CHECK(memcmp(expected, actual[first], second * sizeof actual[0]) == 0);
}

static void the_reserved_ring_tail_is_never_written_after_the_end(void)
{
    setup(48000, 6000, 125);
    play("first", 1);
    render();
    static uint32_t tail[RING_WORDS - STRETCH_WORDS];
    memcpy(tail, &ring[STRETCH_WORDS], sizeof tail);
    for (int round = 0; round < 50; round++) {
        audio_engine_pump(&engine, engine.produced);
    }
    CHECK(memcmp(tail, &ring[STRETCH_WORDS], sizeof tail) == 0);
}

static void a_speed_change_applies_from_the_next_start(void)
{
    setup(48000, 24000, 100);
    play("first", 1);
    audio_engine_set_speed(&engine, 200);
    CHECK_EQUAL(RING_WORDS, engine.config.ring_words);
    CHECK(audio_engine_seek(&engine, 0));
    CHECK_EQUAL(STRETCH_WORDS, engine.config.ring_words);
    CHECK_EQUAL(rounded(24000, 200), render());
    audio_engine_set_speed(&engine, 100);
    CHECK(audio_engine_seek(&engine, 0));
    CHECK_EQUAL(RING_WORDS, engine.config.ring_words);
}

static void without_a_stretch_reserve_every_speed_plays_normally(void)
{
    setup(48000, 12000, 150);
    engine.config.stretch_words = 0;
    play("first", 1);
    CHECK_EQUAL(RING_WORDS, engine.config.ring_words);
    CHECK_EQUAL(12000, render());
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(native_rates_stretch_through_the_kernel_with_the_rounded_duration),
        TEST_CASE(speech_is_stretched_at_its_own_rate_before_conversion),
        TEST_CASE(normal_speed_keeps_the_whole_ring_and_the_exact_samples),
        TEST_CASE(rates_above_48_khz_play_at_normal_speed_with_the_whole_ring),
        TEST_CASE(positions_follow_the_recording_not_the_output),
        TEST_CASE(seeking_empties_the_stage_and_restarts_from_the_source_position),
        TEST_CASE(a_queued_file_continues_in_the_stage_and_switches_at_the_stretched_boundary),
        TEST_CASE(another_input_rate_on_the_same_output_clock_restarts_the_stage),
        TEST_CASE(the_reserved_ring_tail_is_never_written_after_the_end),
        TEST_CASE(a_speed_change_applies_from_the_next_start),
        TEST_CASE(without_a_stretch_reserve_every_speed_plays_normally),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
