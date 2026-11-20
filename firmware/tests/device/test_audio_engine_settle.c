#include <string.h>

#include "check.h"
#include "device/audio_engine_stage.h"

enum {
    RING_FRAMES = 4,
    RING_WORDS = AUDIO_ENGINE_WORDS_PER_FRAME * RING_FRAMES
};

static const uint32_t FILLED = UINT32_C(0xA5A5A5A5);
static uint32_t ring[RING_WORDS];
static AudioEngine engine;

static void start(uint32_t frames)
{
    for (int index = 0; index < RING_WORDS; index++) {
        ring[index] = FILLED;
    }
    engine = (AudioEngine){
        .config = {.ring = ring, .ring_words = AUDIO_ENGINE_WORDS_PER_FRAME * frames}};
}

static bool frame_is(uint32_t frame, uint32_t value)
{
    return ring[AUDIO_ENGINE_WORDS_PER_FRAME * frame] == value &&
           ring[AUDIO_ENGINE_WORDS_PER_FRAME * frame + 1] == value;
}

static void no_dma_progress_leaves_the_ring_untouched(void)
{
    start(RING_FRAMES);
    engine.consumed = 7;
    audio_engine_settle(&engine, 7);
    for (int frame = 0; frame < RING_FRAMES; frame++) {
        CHECK(frame_is((uint32_t)frame, FILLED));
    }
}

static void an_old_dma_position_cannot_clear_future_audio(void)
{
    start(RING_FRAMES);
    engine.consumed = 6;
    audio_engine_settle(&engine, 7);
    for (int frame = 0; frame < RING_FRAMES; frame++) {
        CHECK(frame_is((uint32_t)frame, FILLED));
    }
}

static void a_zero_length_ring_is_safe(void)
{
    start(0);
    engine.consumed = 2;
    audio_engine_settle(&engine, 0);
    for (int frame = 0; frame < RING_FRAMES; frame++) {
        CHECK(frame_is((uint32_t)frame, FILLED));
    }
}

static void played_frames_are_cleared_without_touching_future_audio(void)
{
    start(RING_FRAMES);
    engine.consumed = 3;
    audio_engine_settle(&engine, 1);
    CHECK(frame_is(0, FILLED));
    CHECK(frame_is(1, 0));
    CHECK(frame_is(2, 0));
    CHECK(frame_is(3, FILLED));
}

static void clearing_wraps_at_the_end_of_the_ring(void)
{
    start(RING_FRAMES);
    engine.consumed = 6;
    audio_engine_settle(&engine, 3);
    CHECK(frame_is(0, 0));
    CHECK(frame_is(1, 0));
    CHECK(frame_is(2, FILLED));
    CHECK(frame_is(3, 0));
}

static void a_large_dma_jump_clears_each_ring_frame_once(void)
{
    start(RING_FRAMES);
    engine.consumed = 20;
    audio_engine_settle(&engine, 1);
    for (int frame = 0; frame < RING_FRAMES; frame++) {
        CHECK(frame_is((uint32_t)frame, 0));
    }
}

static void settled_skip_marks_are_credited_to_their_segments(void)
{
    start(RING_FRAMES);
    engine.segment_count = 2;
    engine.segments[0].start = 0;
    engine.segments[1].start = 100;
    engine.effects.skip.marks = 2;
    engine.effects.skip.mark[0] = (AudioSkipMark){50, 3};
    engine.effects.skip.mark[1] = (AudioSkipMark){120, 5};
    engine.consumed = 120;
    audio_engine_settle(&engine, 120);
    CHECK_EQUAL(3, engine.segments[0].skipped);
    CHECK_EQUAL(5, engine.segments[1].skipped);
    CHECK_EQUAL(0, engine.effects.skip.marks);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(no_dma_progress_leaves_the_ring_untouched),
        TEST_CASE(an_old_dma_position_cannot_clear_future_audio),
        TEST_CASE(a_zero_length_ring_is_safe),
        TEST_CASE(played_frames_are_cleared_without_touching_future_audio),
        TEST_CASE(clearing_wraps_at_the_end_of_the_ring),
        TEST_CASE(a_large_dma_jump_clears_each_ring_frame_once),
        TEST_CASE(settled_skip_marks_are_credited_to_their_segments),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
