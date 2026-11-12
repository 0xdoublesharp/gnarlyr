#include <math.h>
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "device/audio_tempo.h"

enum {
    CAPACITY = 48000,
    OUTPUT_CAPACITY = 2 * CAPACITY,
    RING_TAIL_FRAMES = 6006,
    FULL_FRAMES = 6002
};

static AudioTempo reference_state;
static uint8_t reference_buffer[FULL_FRAMES * AUDIO_TEMPO_FRAME_BYTES];
static uint8_t tail[RING_TAIL_FRAMES * AUDIO_TEMPO_FRAME_BYTES];
static int32_t input[CAPACITY][2];
static int32_t expected[OUTPUT_CAPACITY][2];
static int32_t actual[OUTPUT_CAPACITY][2];

static void fill_input(uint32_t rate)
{
    for (uint32_t index = 0; index < CAPACITY; index++) {
        double phase = 6.283185307179586 * index / rate;
        input[index][0] = (int32_t)lround(1500000 * sin(173 * phase) + 900000 * sin(611 * phase));
        input[index][1] = (int32_t)lround(1700000 * sin(229 * phase));
    }
}

static uint32_t reference(uint32_t rate, uint16_t speed, uint32_t frames)
{
    AudioTempoConfig config = {
        .buffer = {reference_buffer, NULL, FULL_FRAMES, 0}, .rate = rate, .speed = speed};
    CHECK(audio_tempo_start(&reference_state, config));
    uint32_t made = 0;
    for (uint32_t index = 0; index <= frames; index++) {
        if (index == frames) {
            audio_tempo_finish(&reference_state);
        } else if (!audio_tempo_push(&reference_state, input[index][0], input[index][1])) {
            while (audio_tempo_pull(&reference_state, &expected[made][0], &expected[made][1])) {
                made++;
            }
            CHECK(audio_tempo_push(&reference_state, input[index][0], input[index][1]));
        }
        while (audio_tempo_pull(&reference_state, &expected[made][0], &expected[made][1])) {
            made++;
        }
    }
    return made;
}

static uint32_t staged(uint32_t frames, uint32_t push_chunk, uint32_t pull_chunk)
{
    uint32_t pushed = 0;
    uint32_t made = 0;
    bool finished = false;
    for (int round = 0; round < 1000000 && !(finished && audio_tempo_stage_pending(0) == 0);
         round++) {
        uint32_t chunk = frames - pushed < push_chunk ? frames - pushed : push_chunk;
        pushed += audio_tempo_stage_push(input[pushed], chunk);
        if (pushed == frames && !finished) {
            audio_tempo_stage_finish();
            finished = true;
        }
        made += audio_tempo_stage_pull(actual[made], pull_chunk, 0);
    }
    return made;
}

static void block_handoff_matches_the_frame_kernel_at_every_supported_rate(void)
{
    static const uint32_t RATES[] = {8000, 16000, 22050, 44100, 48000};
    static const uint16_t SPEEDS[] = {50, 75, 90, 110, 125, 150, 175, 200, 250, 300};
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        fill_input(RATES[rate]);
        for (size_t speed = 0; speed < sizeof SPEEDS / sizeof SPEEDS[0]; speed++) {
            uint32_t frames = RATES[rate] / 2 + 777;
            uint32_t count = reference(RATES[rate], SPEEDS[speed], frames);
            CHECK(audio_tempo_stage_start(tail, RING_TAIL_FRAMES, RATES[rate], SPEEDS[speed]));
            CHECK_EQUAL(count, audio_tempo_stage_pending(frames));
            CHECK_EQUAL(count, staged(frames, 41, 37 + (uint32_t)speed));
            CHECK(memcmp(expected, actual, count * sizeof actual[0]) == 0);
        }
    }
}

static void pending_counts_buffered_and_future_input_at_the_requested_speed(void)
{
    fill_input(16000);
    static const uint16_t SPEEDS[] = {50, 150, 200, 300};
    for (size_t speed = 0; speed < sizeof SPEEDS / sizeof SPEEDS[0]; speed++) {
        uint32_t total = reference(16000, SPEEDS[speed], 16000);
        CHECK(audio_tempo_stage_start(tail, RING_TAIL_FRAMES, 16000, SPEEDS[speed]));
        uint32_t pushed = 0;
        uint32_t made = 0;
        for (int round = 0; round < 400; round++) {
            pushed += audio_tempo_stage_push(input[pushed], 41);
            made += audio_tempo_stage_pull(actual[made], 29, 0);
            CHECK_EQUAL(total - made, audio_tempo_stage_pending(16000 - pushed));
        }
        CHECK(made > 0);
    }
}

static void pulled_frames_can_be_shifted_into_ring_words(void)
{
    fill_input(8000);
    uint32_t count = reference(8000, 125, 4000);
    CHECK(audio_tempo_stage_start(tail, RING_TAIL_FRAMES, 8000, 125));
    static uint32_t words[2 * 4000];
    uint32_t pushed = 0;
    uint32_t made = 0;
    for (int round = 0; round < 1000 && pushed < 4000; round++) {
        pushed += audio_tempo_stage_push(input[pushed], 4000 - pushed);
        made += audio_tempo_stage_pull((int32_t *)&words[2 * made], 4000 - made, 8);
    }
    audio_tempo_stage_finish();
    made += audio_tempo_stage_pull((int32_t *)&words[2 * made], 4000 - made, 8);
    CHECK_EQUAL(count, made);
    bool shifted = true;
    for (uint32_t frame = 0; frame < count; frame++) {
        shifted = shifted && words[2 * frame] == (uint32_t)expected[frame][0] << 8 &&
                  words[2 * frame + 1] == (uint32_t)expected[frame][1] << 8;
    }
    CHECK(shifted);
    CHECK_EQUAL(0, audio_tempo_stage_pending(0));
}

static void the_ring_tail_holds_the_whole_window_only_up_to_48_khz(void)
{
    CHECK_EQUAL(FULL_FRAMES, audio_tempo_buffer_frames(48000));
    CHECK(audio_tempo_stage_start(tail, FULL_FRAMES, 48000, 200));
    CHECK(!audio_tempo_stage_start(tail, FULL_FRAMES - 1, 48000, 200));
    CHECK(audio_tempo_stage_start(tail, audio_tempo_buffer_frames(44100), 44100, 200));
    CHECK(!audio_tempo_stage_start(tail, audio_tempo_buffer_frames(44100) - 1, 44100, 200));
    CHECK(audio_tempo_stage_start(tail, RING_TAIL_FRAMES, 48000, 200));
    CHECK(!audio_tempo_stage_start(tail, RING_TAIL_FRAMES, 88200, 150));
    CHECK(!audio_tempo_stage_start(tail, RING_TAIL_FRAMES, 48000, 49));
    CHECK(audio_tempo_stage_start(tail, RING_TAIL_FRAMES, 48000, 300));
    CHECK(!audio_tempo_stage_start(tail, RING_TAIL_FRAMES, 48000, 301));
}

static void a_full_stage_refuses_input_until_it_is_pulled(void)
{
    fill_input(8000);
    CHECK(audio_tempo_stage_start(tail, RING_TAIL_FRAMES, 8000, 50));
    uint32_t taken = audio_tempo_stage_push(input[0], 8000);
    CHECK(taken < 8000);
    CHECK_EQUAL(0, audio_tempo_stage_push(input[taken], 8000 - taken));
    CHECK(audio_tempo_stage_pull(actual[0], 2000, 0) > 0);
    CHECK(audio_tempo_stage_push(input[taken], 8000 - taken) > 0);
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(block_handoff_matches_the_frame_kernel_at_every_supported_rate),
        TEST_CASE(pending_counts_buffered_and_future_input_at_the_requested_speed),
        TEST_CASE(pulled_frames_can_be_shifted_into_ring_words),
        TEST_CASE(the_ring_tail_holds_the_whole_window_only_up_to_48_khz),
        TEST_CASE(a_full_stage_refuses_input_until_it_is_pulled),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
