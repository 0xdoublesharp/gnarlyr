#include <string.h>

#include "check.h"
#include "device/audio_engine.h"

enum {
    BLOCK = 48,
    RING_FRAMES = 192,
    SOURCE_FRAMES = 480
};

static const AudioEngineTrack FIRST = {.path = "/first.flac", .token = 11};
static const AudioEngineTrack SECOND = {.path = "/second.flac", .token = 22};

typedef struct FaultSource {
    uint32_t position;
    uint32_t fail_next;
    uint32_t calls;
    uint32_t faults;
    bool fail_open;
    bool open;
    bool held;
    AudioFrameResult fault;
    int32_t samples[BLOCK];
} FaultSource;

static FaultSource source;
static AudioEngine engine;
static uint32_t ring[2 * RING_FRAMES];

static bool open_file(void *context, const char *path, uint32_t position, AudioTrackInfo *info)
{
    FaultSource *file = context;
    CHECK(!file->open);
    if (file->fail_open) {
        file->fail_open = false;
        return false;
    }
    bool first = strcmp(path, FIRST.path) == 0;
    if (!first && strcmp(path, SECOND.path) != 0) {
        return false;
    }
    uint32_t rate = first ? 48000 : 96000;
    file->open = true;
    file->position = position * (rate / 1000);
    *info = (AudioTrackInfo){.stream = {.sample_rate = rate, .total_samples = SOURCE_FRAMES}};
    for (uint32_t index = 0; index < BLOCK; index++) {
        file->samples[index] = first ? 0x1111 : 0x2222;
    }
    return true;
}

static AudioFrameResult next_frame(void *context, AudioFrame *frame)
{
    FaultSource *file = context;
    CHECK(file->open && !file->held);
    if (file->calls++ == file->fail_next) {
        file->faults++;
        return file->fault;
    }
    if (file->position >= SOURCE_FRAMES) {
        return AUDIO_FRAME_END;
    }
    file->held = true;
    *frame = (AudioFrame){file->samples, file->samples, file->position, BLOCK, 16};
    file->position += BLOCK;
    return AUDIO_FRAME_READY;
}

static void release_frame(void *context)
{
    FaultSource *file = context;
    CHECK(file->held);
    file->held = false;
}

static void close_file(void *context)
{
    FaultSource *file = context;
    CHECK(file->open && !file->held);
    file->open = false;
}

static void start(void)
{
    source = (FaultSource){.fail_next = UINT32_MAX};
    AudioSource input = {open_file, next_frame, release_frame, close_file, &source};
    audio_engine_init(
        &engine, (AudioEngineConfig){.source = input, .ring = ring, .ring_words = 2 * RING_FRAMES});
    CHECK(audio_engine_play(&engine, &FIRST, 0));
}

static uint32_t frame_at(uint32_t position)
{
    return ring[2 * (position % RING_FRAMES)];
}

static void check_pcm(uint32_t position, uint32_t expected)
{
    for (uint32_t frame = 0; frame < BLOCK; frame++) {
        CHECK_EQUAL(expected, frame_at(position + frame));
    }
}

static void decode_faults_stop_and_recover_at_every_frame(void)
{
    for (uint32_t fault_at = 0; fault_at < SOURCE_FRAMES / BLOCK; fault_at++) {
        start();
        source.fail_next = fault_at;
        source.fault = AUDIO_FRAME_ERROR;
        uint32_t consumed = 0;
        for (uint32_t index = 0; index <= fault_at; index++) {
            audio_engine_pump(&engine, consumed);
            if (index < fault_at) {
                check_pcm(consumed, 0x11110000);
                consumed += BLOCK;
            }
        }
        audio_engine_observe(&engine, consumed);
        AudioEngineStatus status = audio_engine_status(&engine);
        CHECK(status.failed);
        CHECK_EQUAL(AUDIO_ENGINE_STOPPED, status.state);
        CHECK(!source.open && !source.held);
        CHECK(audio_engine_seek(&engine, 1));
        audio_engine_pump(&engine, 0);
        check_pcm(0, 0x11110000);
        CHECK_EQUAL(1, audio_engine_status(&engine).position_ms);
        CHECK(!audio_engine_status(&engine).failed);
        audio_engine_stop(&engine);
    }
}

static void an_interrupted_seek_can_be_retried_on_the_same_track(void)
{
    start();
    audio_engine_pump(&engine, 0);
    audio_engine_pause(&engine);
    source.fail_open = true;
    CHECK(!audio_engine_seek_on(&engine, FIRST.token, 3));
    CHECK(audio_engine_status(&engine).failed);
    CHECK(!source.open);
    CHECK(audio_engine_seek_on(&engine, FIRST.token, 3));
    CHECK_EQUAL(3, audio_engine_status(&engine).position_ms);
    audio_engine_resume(&engine);
    audio_engine_pump(&engine, 0);
    check_pcm(0, 0x11110000);
    audio_engine_stop(&engine);
}

static void intermittent_waits_across_pause_repeat_and_rate_changes(void)
{
    start();
    audio_engine_pause(&engine);
    CHECK_EQUAL(AUDIO_ENGINE_PAUSED, audio_engine_status(&engine).state);
    CHECK(audio_engine_seek(&engine, 1));
    CHECK_EQUAL(AUDIO_ENGINE_PAUSED, audio_engine_status(&engine).state);
    audio_engine_resume(&engine);
    CHECK_EQUAL(1, audio_engine_repeat_mark(&engine, false));
    audio_engine_pump(&engine, 0);
    audio_engine_observe(&engine, BLOCK);
    CHECK_EQUAL(2, audio_engine_repeat_mark(&engine, false));

    uint32_t consumed = 0;
    for (uint32_t loop = 0; loop < 12; loop++) {
        uint32_t expected_faults = source.faults;
        if (loop % 3 == 0) {
            source.fault = AUDIO_FRAME_WAIT;
            source.fail_next = source.calls;
            expected_faults++;
        }
        for (uint32_t retry = 0; retry < 4 && audio_engine_buffered(&engine, consumed) < BLOCK;
             retry++) {
            audio_engine_pump(&engine, consumed);
        }
        CHECK_EQUAL(expected_faults, source.faults);
        CHECK_EQUAL(AUDIO_ENGINE_PLAYING, audio_engine_status(&engine).state);
        CHECK_EQUAL(BLOCK, audio_engine_buffered(&engine, consumed));
        check_pcm(consumed, 0x11110000);
        consumed += BLOCK;
        audio_engine_observe(&engine, consumed);
        CHECK_EQUAL(BLOCK, audio_engine_status(&engine).position_samples);
    }

    CHECK_EQUAL(0, audio_engine_repeat_mark(&engine, true));
    audio_engine_queue_next(&engine, &SECOND);
    consumed = 0;
    uint32_t rate = 0;
    for (uint32_t step = 0; step < 32 && !audio_engine_wants_rate(&engine, &rate); step++) {
        if (step % 3 == 0) {
            source.fail_next = source.calls;
        }
        audio_engine_pump(&engine, consumed);
        consumed += audio_engine_buffered(&engine, consumed);
        audio_engine_observe(&engine, consumed);
    }
    CHECK_EQUAL(96000, rate);
    audio_engine_restart(&engine);
    audio_engine_pump(&engine, 0);
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(SECOND.token, audio_engine_status(&engine).token);
    CHECK_EQUAL(96000, audio_engine_rate(&engine));
    CHECK(!audio_engine_status(&engine).failed);
    check_pcm(0, 0x22220000);
    audio_engine_stop(&engine);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(decode_faults_stop_and_recover_at_every_frame),
        TEST_CASE(an_interrupted_seek_can_be_retried_on_the_same_track),
        TEST_CASE(intermittent_waits_across_pause_repeat_and_rate_changes),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
