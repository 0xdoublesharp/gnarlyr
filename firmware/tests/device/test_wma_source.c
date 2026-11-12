#include <string.h>

#include "check.h"
#include "device/core1_wma.h"
#include "m4a_fixture_files.h"

static StreamControl control;
static int32_t pcm[STREAM_SLOTS][FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK];
static uint8_t ring[WMA_RING_BYTES];
static uint8_t packet[WMA_PACKET_BYTES + 8];
static WMADecodeContext decode_context;
static Core1Wma decoder;
static Core1Stream stream;
static Core1Source source;
static AsfStream adapter;
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
    StreamRegion region = {&control, pcm, ring, sizeof ring, NULL};
    core1_source_init(&source, region, (FlacFileOpener){open_file, close_file, NULL}, &unused,
                      1024);
    source.asf = &adapter;
    core1_stream_init(&stream, region,
                      core1_wma_decoder(&decoder, &decode_context, packet, region));
    closes = 0;
}

static void decode_at(uint32_t start_ms)
{
    AudioSource audio = core1_source_interface(&source, CORE1_FORMAT_WMA);
    AudioTrackInfo track;
    CHECK(audio.open(audio.context, "seek.wma", start_ms, &track));
    CHECK_EQUAL(AUDIO_CODEC_WMA, track.codec);
    CHECK_EQUAL(44100, track.stream.sample_rate);
    CHECK_EQUAL(88064, track.stream.total_samples);
    uint32_t position = (uint32_t)((uint64_t)start_ms * 44100 / 1000);
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
            CHECK_EQUAL(29, frame.bits);
            CHECK(frame.left != frame.right);
            CHECK(frame.samples > 0 && frame.samples <= 2048);
            position += frame.samples;
            audio.release(audio.context);
        }
    }
    CHECK_EQUAL(AUDIO_FRAME_END, result);
    CHECK_EQUAL(track.stream.total_samples, position);
    audio.close(audio.context);
}

static void real_ring_streaming_and_seek_pre_roll_keep_exact_positions_and_lengths(void)
{
    file = m4a_file_load("wma/v2seek.wma");
    CHECK(file.size > 0);
    setup();
    decode_at(0);
    decode_at(1300);
    decode_at(700);
    CHECK_EQUAL(3, closes);
    m4a_file_free(&file);
}

static void a_storage_failure_is_not_reported_as_clean_end(void)
{
    file = m4a_file_load("wma/v2seek.wma");
    CHECK(file.size > 0);
    setup();
    AudioSource audio = core1_source_interface(&source, CORE1_FORMAT_WMA);
    AudioTrackInfo track;
    CHECK(audio.open(audio.context, "bad.wma", 0, &track));
    file.size = 0;
    AudioFrameResult result = AUDIO_FRAME_WAIT;
    for (uint32_t step = 0; step < 1000 && result == AUDIO_FRAME_WAIT; step++) {
        core1_source_fill(&source);
        (void)core1_stream_step(&stream);
        AudioFrame frame;
        result = audio.next(audio.context, &frame);
        if (result == AUDIO_FRAME_READY) {
            audio.release(audio.context);
            result = AUDIO_FRAME_WAIT;
        }
    }
    CHECK_EQUAL(AUDIO_FRAME_ERROR, result);
    audio.close(audio.context);
    CHECK_EQUAL(1, closes);
    m4a_file_free(&file);
}

static void queued_tracks_add_no_gap_or_extra_output_frames(void)
{
    static uint32_t output_ring[4096];
    static AudioEngine engine;
    file = m4a_file_load("wma/v2seek.wma");
    CHECK(file.size > 0);
    setup();
    AudioSource audio = core1_source_interface(&source, CORE1_FORMAT_WMA);
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
    CHECK_EQUAL(176128, taken);
    CHECK_EQUAL(2, closes);
    m4a_file_free(&file);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(queued_tracks_add_no_gap_or_extra_output_frames),
        TEST_CASE(real_ring_streaming_and_seek_pre_roll_keep_exact_positions_and_lengths),
        TEST_CASE(a_storage_failure_is_not_reported_as_clean_end),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
