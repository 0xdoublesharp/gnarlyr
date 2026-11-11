#include <stdint.h>
#include <string.h>

#include "check.h"
#include "device/audio_engine.h"

enum {
    TRACK_FRAMES = 613,
    RING_LIMIT = 64,
    UNITY = 65536,
    REFERENCE_CAPACITY = 4 * TRACK_FRAMES + 2 * AUDIO_RESAMPLE_HISTORY,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608
};

typedef enum Layout {
    LAYOUT_SHARED_MONO,
    LAYOUT_COPIED_MONO,
    LAYOUT_STEREO
} Layout;

typedef struct MatrixSource {
    int32_t left[TRACK_FRAMES];
    int32_t right[TRACK_FRAMES];
    uint32_t rate;
    uint32_t block;
    uint32_t sent;
    uint8_t bits;
    Layout layout;
} MatrixSource;

typedef struct Case {
    uint32_t rate;
    uint8_t bits;
    Layout layout;
    uint32_t replay_gain;
    uint8_t volume;
    uint32_t ring_frames;
} Case;

static MatrixSource matrix;
static uint32_t ring[2 * RING_LIMIT];
static uint32_t expected[2 * REFERENCE_CAPACITY];
static uint32_t received[2 * REFERENCE_CAPACITY];
static AudioEngine engine;

static int64_t reference_floor_shift(int64_t value, int shift)
{
    return value >= 0 ? value >> shift : -((-value - 1) >> shift) - 1;
}

static int32_t reference_output(int32_t sample, int bits, uint32_t gain)
{
    int64_t value = (int64_t)sample * gain;
    int shift = 16 + bits - 24;
    if (shift > 0) {
        value = reference_floor_shift(value + (INT64_C(1) << (shift - 1)), shift);
    } else if (shift < 0) {
        int64_t factor = INT64_C(1) << -shift;
        if (value > SAMPLE_MAX / factor || value < SAMPLE_MIN / factor) {
            return value < 0 ? SAMPLE_MIN : SAMPLE_MAX;
        }
        value *= factor;
    }
    return value > SAMPLE_MAX ? SAMPLE_MAX : value < SAMPLE_MIN ? SAMPLE_MIN : (int32_t)value;
}

static bool matrix_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    (void)path;
    (void)start_ms;
    MatrixSource *source = context;
    source->sent = 0;
    *track = (AudioTrackInfo){.stream = {.sample_rate = source->rate,
                                         .channels = source->layout == LAYOUT_STEREO ? 2 : 1,
                                         .bits_per_sample = source->bits,
                                         .total_samples = TRACK_FRAMES,
                                         .max_block = (uint16_t)source->block}};
    return true;
}

static AudioFrameResult matrix_next(void *context, AudioFrame *frame)
{
    MatrixSource *source = context;
    if (source->sent == TRACK_FRAMES) {
        return AUDIO_FRAME_END;
    }
    uint32_t count = TRACK_FRAMES - source->sent;
    count = count < source->block ? count : source->block;
    const int32_t *right = source->layout == LAYOUT_SHARED_MONO ? source->left : source->right;
    *frame = (AudioFrame){.left = source->left + source->sent,
                          .right = right + source->sent,
                          .first_sample = source->sent,
                          .samples = count,
                          .bits = source->bits};
    source->sent += count;
    source->block = source->block % 61 + 7;
    return AUDIO_FRAME_READY;
}

static void matrix_release(void *context)
{
    (void)context;
}

static void matrix_close(void *context)
{
    (void)context;
}

static int32_t awkward_sample(uint32_t *seed, int bits)
{
    static const int32_t EDGES[] = {INT32_MAX, INT32_MIN, 8388607, 8388608,   -8388608,
                                    -8388609,  32767,     32768,   -32768,    -32769,
                                    0,         -1,        1 << 30, -(1 << 30)};
    *seed = *seed * 1664525u + 1013904223u;
    if ((*seed >> 28) == 0) {
        return EDGES[(*seed >> 8) % (sizeof EDGES / sizeof EDGES[0])];
    }
    int32_t value = (int32_t)*seed;
    return bits >= 32 ? value : value >> (32 - bits);
}

static void fill_matrix(const Case *test)
{
    matrix =
        (MatrixSource){.rate = test->rate, .block = 23, .bits = test->bits, .layout = test->layout};
    uint32_t seed = test->rate ^ test->bits * 977u ^ test->replay_gain;
    for (uint32_t index = 0; index < TRACK_FRAMES; index++) {
        matrix.left[index] = awkward_sample(&seed, test->bits);
        matrix.right[index] =
            test->layout == LAYOUT_STEREO ? awkward_sample(&seed, test->bits) : matrix.left[index];
    }
}

static uint32_t reference_gain(const Case *test)
{
    AudioEngine probe;
    audio_engine_init(&probe, (AudioEngineConfig){0});
    audio_engine_set_volume(&probe, test->volume);
    uint32_t track = test->replay_gain != 0 ? test->replay_gain : UNITY;
    return (uint32_t)(((uint64_t)probe.gain * track + UNITY / 2) >> 16);
}

static uint32_t reference_words(const Case *test)
{
    uint32_t gain = reference_gain(test);
    AudioResample resample;
    audio_resample_start(&resample, test->rate);
    uint32_t frames = 0;
    for (uint32_t index = 0; index < TRACK_FRAMES; index++) {
        int32_t left = reference_output(matrix.left[index], test->bits, gain);
        int32_t right = reference_output(matrix.right[index], test->bits, gain);
        if (resample.factor == 1) {
            expected[2 * frames] = (uint32_t)left << 8;
            expected[2 * frames + 1] = (uint32_t)right << 8;
            frames++;
            continue;
        }
        const int32_t pair[2] = {left, right};
        CHECK_EQUAL(1u, audio_resample_push(&resample, pair, 1));
        while (audio_resample_render(&resample, &expected[2 * frames], 1) == 1) {
            frames++;
        }
    }
    audio_resample_finish(&resample);
    while (resample.factor > 1 && audio_resample_render(&resample, &expected[2 * frames], 1) == 1) {
        frames++;
    }
    return frames;
}

static uint32_t engine_words(const Case *test)
{
    AudioSource source = {matrix_open, matrix_next, matrix_release, matrix_close, &matrix};
    audio_engine_init(&engine, (AudioEngineConfig){.source = source,
                                                   .ring = ring,
                                                   .ring_words = 2 * test->ring_frames,
                                                   .resample_render = audio_resample_render});
    audio_engine_set_volume(&engine, test->volume);
    AudioEngineTrack track = {.path = "matrix", .token = 1, .replay_gain = test->replay_gain};
    CHECK(audio_engine_play(&engine, &track, 0));
    uint32_t taken = 0;
    uint32_t consumed = 0;
    for (uint32_t step = 0; step < 20000 && !engine.ended; step++) {
        uint32_t lag = step % 5;
        uint32_t target = taken > lag ? taken - lag : 0;
        consumed = target > consumed ? target : consumed;
        audio_engine_pump(&engine, consumed);
        for (; taken < engine.produced && taken < REFERENCE_CAPACITY; taken++) {
            uint32_t slot = taken % test->ring_frames;
            received[2 * taken] = ring[2 * slot];
            received[2 * taken + 1] = ring[2 * slot + 1];
        }
    }
    CHECK(engine.ended && !engine.failed);
    return engine.end_at;
}

static void check_case(const Case *test)
{
    fill_matrix(test);
    uint32_t frames = reference_words(test);
    CHECK_EQUAL(frames, engine_words(test));
    CHECK(memcmp(expected, received, (size_t)frames * 2 * sizeof(uint32_t)) == 0);
}

static void block_conversion_matches_the_per_frame_reference_byte_for_byte(void)
{
    static const uint32_t RATES[] = {44100, 96000, 16000, 8000, 22050};
    static const uint8_t DEPTHS[] = {8, 16, 20, 24, 32};
    static const uint32_t GAINS[] = {0, UNITY, 32768, 131072, 46341};
    static const uint8_t VOLUMES[] = {60, 41};
    static const uint32_t RINGS[] = {64, 37, 29};
    uint32_t cases = 0;
    for (size_t rate = 0; rate < sizeof RATES / sizeof RATES[0]; rate++) {
        for (size_t depth = 0; depth < sizeof DEPTHS / sizeof DEPTHS[0]; depth++) {
            for (int layout = LAYOUT_SHARED_MONO; layout <= LAYOUT_STEREO; layout++) {
                for (size_t gain = 0; gain < sizeof GAINS / sizeof GAINS[0]; gain++) {
                    for (size_t volume = 0; volume < 2; volume++) {
                        Case test = {RATES[rate], DEPTHS[depth],   (Layout)layout,
                                     GAINS[gain], VOLUMES[volume], RINGS[cases % 3]};
                        check_case(&test);
                        cases++;
                    }
                }
            }
        }
    }
    CHECK_EQUAL(750u, cases);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(block_conversion_matches_the_per_frame_reference_byte_for_byte),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
