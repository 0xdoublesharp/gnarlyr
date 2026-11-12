#include <string.h>

#include "check.h"
#include "device/core1_ape.h"
#include "m4a_fixture_files.h"

static StreamControl control;
static int32_t pcm[STREAM_SLOTS][FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK];
static uint8_t ring[APE_RING_BYTES];
static uint8_t snapshot[APE_INPUT_BYTES];
static Core1Ape decoder;
static Core1Stream stream;
static Core1Source source;
static ApeStream adapter;
static FlacDecoder unused;
static M4aFile file;
static uint32_t closes;

static bool open_file(void *context, const char *path, FlacInput *input)
{
    (void)context;
    (void)path;
    *input = m4a_file_input(&file);
    return true;
}

static void close_file(void *context)
{
    (void)context;
    closes++;
}

static void setup(void)
{
    memset(&control, 0, sizeof control);
    StreamRegion region = {&control, pcm, ring, sizeof ring};
    core1_source_init(&source, region, (FlacFileOpener){open_file, close_file, NULL}, &unused,
                      4096);
    source.ape = &adapter;
    core1_stream_init(&stream, region, core1_ape_decoder(&decoder, snapshot, region));
    closes = 0;
}

static void decode_at(uint32_t start_ms)
{
    AudioSource audio = core1_source_interface(&source, CORE1_FORMAT_APE);
    AudioTrackInfo track;
    CHECK(audio.open(audio.context, "seek.ape", start_ms, &track));
    CHECK_EQUAL(AUDIO_CODEC_APE, track.codec);
    CHECK_EQUAL(8000, track.stream.sample_rate);
    CHECK_EQUAL(295936, track.stream.total_samples);
    uint32_t position = start_ms * 8;
    AudioFrameResult result = AUDIO_FRAME_WAIT;
    for (uint32_t step = 0; step < 10000; step++) {
        core1_source_fill(&source);
        (void)core1_stream_step(&stream);
        AudioFrame frame;
        result = audio.next(audio.context, &frame);
        if (result == AUDIO_FRAME_END || result == AUDIO_FRAME_ERROR) {
            break;
        }
        if (result == AUDIO_FRAME_READY) {
            CHECK_EQUAL(position, frame.first_sample);
            CHECK_EQUAL(16, frame.bits);
            CHECK(frame.left == frame.right);
            for (uint32_t index = 0; index < frame.samples; index++) {
                int32_t expected = (int32_t)(((position + index) * 31u) % 30001u) - 15000;
                if (frame.left[index] != expected) {
                    CHECK_EQUAL(expected, frame.left[index]);
                    return;
                }
            }
            position += frame.samples;
            audio.release(audio.context);
        }
    }
    CHECK_EQUAL(AUDIO_FRAME_END, result);
    CHECK_EQUAL(track.stream.total_samples, position);
    audio.close(audio.context);
}

static void real_ring_streaming_has_exact_length_and_sample_precise_seeks(void)
{
    file = m4a_file_load("ape/seek16.ape");
    CHECK(file.size > 0);
    setup();
    decode_at(0);
    decode_at(36900);
    decode_at(123);
    CHECK_EQUAL(3, closes);
    m4a_file_free(&file);
}

static void invalid_files_close_without_publishing_a_generation(void)
{
    file = m4a_file_load("ape/fast16.ape");
    CHECK(file.size > 0);
    file.bytes[0] ^= 1;
    setup();
    AudioSource audio = core1_source_interface(&source, CORE1_FORMAT_APE);
    AudioTrackInfo track;
    CHECK(!audio.open(audio.context, "bad.ape", 0, &track));
    CHECK_EQUAL(1, closes);
    CHECK_EQUAL(0, control.generation);
    m4a_file_free(&file);
}

static void expensive_levels_and_high_rate_high_compression_are_refused_before_streaming(void)
{
    static const char *const names[] = {"ape/extra16.ape", "ape/insane16.ape",
                                        "ape/high24stereo.ape"};
    for (uint32_t index = 0; index < sizeof names / sizeof names[0]; index++) {
        file = m4a_file_load(names[index]);
        CHECK(file.size > 76);
        if (index == 2) {
            uint32_t rate = 192000;
            memcpy(file.bytes + 72, &rate, sizeof rate);
        }
        setup();
        source.clock_mhz = 200;
        AudioSource audio = core1_source_interface(&source, CORE1_FORMAT_APE);
        AudioTrackInfo track;
        CHECK(!audio.open(audio.context, "expensive.ape", 0, &track));
        CHECK_EQUAL(0, control.generation);
        CHECK_EQUAL(1, closes);
        m4a_file_free(&file);
    }
}

static void queued_tracks_add_no_gap_or_extra_output_frames(void)
{
    static uint32_t output_ring[4096];
    static AudioEngine engine;
    file = m4a_file_load("ape/fast16.ape");
    CHECK(file.size > 0);
    setup();
    AudioSource audio = core1_source_interface(&source, CORE1_FORMAT_APE);
    audio_engine_init(
        &engine, (AudioEngineConfig){.source = audio, .ring = output_ring, .ring_words = 4096});
    AudioEngineTrack first = {.path = "first", .token = 1};
    AudioEngineTrack second = {.path = "second", .token = 2};
    CHECK(audio_engine_play(&engine, &first, 0));
    audio_engine_queue_next(&engine, &second);
    uint32_t taken = 0;
    for (uint32_t step = 0; step < 20000 && engine.state != AUDIO_ENGINE_STOPPED; step++) {
        (void)core1_stream_step(&stream);
        audio_engine_pump(&engine, taken);
        taken = engine.ended ? engine.end_at : engine.produced;
    }
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, engine.state);
    CHECK_EQUAL(12288, taken);
    CHECK_EQUAL(2, closes);
    m4a_file_free(&file);
}

static void opener_admission_changes_with_the_reported_core_clock(void)
{
    file = m4a_file_load("ape/normal24stereo96.ape");
    CHECK(file.size > 0);
    setup();
    AudioSource audio = core1_source_interface(&source, CORE1_FORMAT_APE);
    AudioTrackInfo track;
    CHECK(!audio.open(audio.context, "normal96.ape", 0, &track));
    CHECK_EQUAL(0, control.generation);
    source.clock_mhz = 200;
    CHECK(audio.open(audio.context, "normal96.ape", 0, &track));
    CHECK_EQUAL(96000, track.stream.sample_rate);
    CHECK(control.generation != 0);
    audio.close(audio.context);
    CHECK_EQUAL(2, closes);
    m4a_file_free(&file);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(opener_admission_changes_with_the_reported_core_clock),
        TEST_CASE(queued_tracks_add_no_gap_or_extra_output_frames),
        TEST_CASE(real_ring_streaming_has_exact_length_and_sample_precise_seeks),
        TEST_CASE(invalid_files_close_without_publishing_a_generation),
        TEST_CASE(expensive_levels_and_high_rate_high_compression_are_refused_before_streaming),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
