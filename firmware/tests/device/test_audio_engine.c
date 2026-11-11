#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/audio_engine.h"
#include "device/flac_source.h"

enum {
    RING_FRAMES = 8192,
    RING_WORDS = 2 * RING_FRAMES,
    FULL_VOLUME = 60
};

typedef struct Fixture {
    uint8_t *bytes;
    uint32_t size;
} Fixture;

static FlacDecoder decoder;
static FlacSource source;
static uint32_t ring[RING_WORDS];
static AudioEngine engine;
static Fixture opened;
static int opens;

static Fixture load(const char *name, const char *extension)
{
    char path[512];
    snprintf(path, sizeof path, "%s/flac/%s.%s", ECHO_FIXTURE_DIR, name, extension);
    Fixture fixture = {NULL, 0};
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return fixture;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    fixture.bytes = malloc((size_t)size);
    if (fixture.bytes != NULL && fread(fixture.bytes, 1, (size_t)size, file) == (size_t)size) {
        fixture.size = (uint32_t)size;
    }
    fclose(file);
    return fixture;
}

static bool read_fixture(void *context, uint32_t offset, void *destination, uint32_t length)
{
    const Fixture *fixture = context;
    if (offset > fixture->size || length > fixture->size - offset) {
        return false;
    }
    memcpy(destination, fixture->bytes + offset, length);
    return true;
}

static bool open_file(void *context, const char *path, FlacInput *input)
{
    (void)context;
    opens++;
    free(opened.bytes);
    opened = load(path, "flac");
    *input = (FlacInput){.read = read_fixture, .context = &opened, .size = opened.size};
    return opened.size > 0;
}

static void close_file(void *context)
{
    (void)context;
}

static bool play_range(const char *path, uint32_t start_ms, uint32_t end_ms, uint32_t token)
{
    AudioEngineTrack track = {.path = path, .start_ms = start_ms, .end_ms = end_ms, .token = token};
    return audio_engine_play(&engine, &track, 0);
}

static bool play_file(const char *path, uint32_t offset_ms, uint32_t token)
{
    AudioEngineTrack track = {.path = path, .start_ms = 0, .end_ms = 0, .token = token};
    return audio_engine_play(&engine, &track, offset_ms);
}

static void queue_range(const char *path, uint32_t start_ms, uint32_t end_ms, uint32_t token)
{
    AudioEngineTrack track = {.path = path, .start_ms = start_ms, .end_ms = end_ms, .token = token};
    audio_engine_queue_next(&engine, &track);
}

static void queue_file(const char *path, uint32_t token)
{
    queue_range(path, 0, 0, token);
}

static void start_engine(void)
{
    opens = 0;
    flac_source_init(&source,
                     (FlacFileOpener){.open = open_file, .close = close_file, .context = NULL},
                     &decoder);
    audio_engine_init(&engine, (AudioEngineConfig){.source = flac_source_interface(&source),
                                                   .ring = ring,
                                                   .ring_words = RING_WORDS,
                                                   .resample_render = audio_resample_render});
    audio_engine_set_volume(&engine, FULL_VOLUME);
}

typedef struct ScriptedSource {
    int32_t left[16];
    int32_t right[16];
    int waits_before_frame;
    int frames_left;
    int frames_per_open;
    int releases;
    int closes;
    uint32_t rate;
    uint32_t next_rate;
    uint8_t bits;
} ScriptedSource;

static ScriptedSource scripted;

static bool scripted_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    (void)start_ms;
    ScriptedSource *source_state = context;
    if (source_state->frames_per_open > 0) {
        source_state->frames_left = source_state->frames_per_open;
    }
    uint32_t rate = source_state->rate == 0 ? 44100 : source_state->rate;
    if (source_state->next_rate != 0 && strcmp(path, "second") == 0) {
        rate = source_state->next_rate;
    }
    *track = (AudioTrackInfo){
        .stream = {.sample_rate = rate,
                   .channels = 2,
                   .bits_per_sample = source_state->bits == 0 ? 16 : source_state->bits,
                   .max_block = 16}};
    return true;
}

static AudioFrameResult scripted_next(void *context, AudioFrame *frame)
{
    ScriptedSource *source_state = context;
    if (source_state->waits_before_frame > 0) {
        source_state->waits_before_frame--;
        return AUDIO_FRAME_WAIT;
    }
    if (source_state->frames_left == 0) {
        return AUDIO_FRAME_END;
    }
    source_state->frames_left--;
    *frame = (AudioFrame){.left = source_state->left,
                          .right = source_state->right,
                          .first_sample = 0,
                          .samples = 16,
                          .bits = source_state->bits == 0 ? 16 : source_state->bits};
    return AUDIO_FRAME_READY;
}

static void scripted_release(void *context)
{
    ((ScriptedSource *)context)->releases++;
}

static void scripted_close(void *context)
{
    ((ScriptedSource *)context)->closes++;
}

static void start_scripted_engine(int waits, int frames)
{
    scripted = (ScriptedSource){.waits_before_frame = waits, .frames_left = frames};
    for (int index = 0; index < 16; index++) {
        scripted.left[index] = index + 1;
        scripted.right[index] = -(index + 1);
    }
    AudioSource interface = {scripted_open, scripted_next, scripted_release, scripted_close,
                             &scripted};
    audio_engine_init(&engine, (AudioEngineConfig){.source = interface,
                                                   .ring = ring,
                                                   .ring_words = RING_WORDS,
                                                   .resample_render = audio_resample_render});
    audio_engine_set_volume(&engine, FULL_VOLUME);
}

static int32_t expected_at(const Fixture *pcm, uint32_t index)
{
    int32_t value;
    memcpy(&value, pcm->bytes + 4 * index, sizeof value);
    return value;
}

static uint32_t word_of(int32_t sample, int bits)
{
    return (uint32_t)sample << (32 - bits);
}

static bool frame_is(uint32_t frame, int32_t left, int32_t right, int bits)
{
    uint32_t slot = frame % RING_FRAMES;
    return ring[2 * slot] == word_of(left, bits) && ring[2 * slot + 1] == word_of(right, bits);
}

static int32_t sixteen_bit_left(uint32_t frame)
{
    return (int32_t)ring[2 * (frame % RING_FRAMES)] / 65536;
}

static bool ring_holds(const char *name, uint32_t first_frame, uint32_t ring_start, uint32_t frames,
                       int bits)
{
    Fixture pcm = load(name, "pcm");
    bool ok = pcm.size > 0;
    for (uint32_t frame = 0; ok && frame < frames; frame++) {
        uint32_t index = 2 * (first_frame + frame);
        ok = frame_is(ring_start + frame, expected_at(&pcm, index), expected_at(&pcm, index + 1),
                      bits);
    }
    free(pcm.bytes);
    return ok;
}

static void pump_until_full(uint32_t consumed)
{
    for (int round = 0; round < 64; round++) {
        audio_engine_pump(&engine, consumed);
    }
}

static void playing_fills_the_ring_with_the_track(void)
{
    start_engine();
    CHECK(play_file("lpc_16_44k_stereo", 0, 7));
    CHECK_EQUAL(44100u, audio_engine_rate(&engine));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 0, 6000, 16));
    AudioEngineStatus status = audio_engine_status(&engine);
    CHECK_EQUAL(AUDIO_ENGINE_PLAYING, status.state);
    CHECK_EQUAL(7u, status.token);
    CHECK_EQUAL(0u, status.position_ms);
}

static void the_position_follows_what_the_dma_consumed(void)
{
    start_engine();
    CHECK(play_file("lpc_16_44k_stereo", 0, 7));
    pump_until_full(0);
    audio_engine_pump(&engine, 4410);
    CHECK_EQUAL(100u, audio_engine_status(&engine).position_ms);
}

static void twenty_four_bit_audio_reaches_the_ring_exactly(void)
{
    start_engine();
    CHECK(play_file("lpc_24_96k_stereo", 0, 1));
    CHECK_EQUAL(96000u, audio_engine_rate(&engine));
    pump_until_full(0);
    CHECK(ring_holds("lpc_24_96k_stereo", 0, 0, 5000, 24));
}

static void the_end_of_a_track_without_a_next_one_stops_and_goes_silent(void)
{
    start_engine();
    CHECK(play_file("odd_blocks_16", 0, 3));
    pump_until_full(0);
    CHECK(frame_is(3001 + 10, 0, 0, 16));
    audio_engine_pump(&engine, 3000);
    CHECK_EQUAL(AUDIO_ENGINE_PLAYING, audio_engine_status(&engine).state);
    audio_engine_pump(&engine, 3001);
    AudioEngineStatus status = audio_engine_status(&engine);
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, status.state);
    CHECK_EQUAL(3u, status.token);
    CHECK(!status.failed);
}

static void a_queued_track_follows_without_a_gap(void)
{
    start_engine();
    CHECK(play_file("odd_blocks_16", 0, 3));
    queue_file("lpc_16_44k_stereo", 4);
    pump_until_full(0);
    CHECK(ring_holds("odd_blocks_16", 0, 0, 3001, 16));
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 3001, 4000, 16));
    audio_engine_pump(&engine, 3000);
    CHECK_EQUAL(3u, audio_engine_status(&engine).token);
    audio_engine_pump(&engine, 3001 + 441);
    AudioEngineStatus status = audio_engine_status(&engine);
    CHECK_EQUAL(4u, status.token);
    CHECK_EQUAL(10u, status.position_ms);
}

static void a_range_ends_at_its_end_and_goes_silent(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 0, 50, 3));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 0, 2205, 16));
    CHECK(frame_is(2205, 0, 0, 16));
    audio_engine_pump(&engine, 2205);
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, audio_engine_status(&engine).state);
}

static void a_range_starts_at_its_start_and_reports_file_positions(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 50, 100, 3));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 2205, 0, 2205, 16));
    CHECK_EQUAL(50u, audio_engine_status(&engine).position_ms);
    audio_engine_pump(&engine, 441);
    CHECK_EQUAL(60u, audio_engine_status(&engine).position_ms);
}

static void a_range_starting_in_a_later_frame_starts_at_its_exact_sample(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 130, 0, 3));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 5733, 0, 267, 16));
    CHECK_EQUAL(130u, audio_engine_status(&engine).position_ms);
}

static void consecutive_ranges_of_one_file_continue_in_the_same_stream(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 0, 50, 3));
    queue_range("lpc_16_44k_stereo", 50, 100, 4);
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 0, 4410, 16));
    CHECK_EQUAL(1, opens);
    audio_engine_pump(&engine, 2204);
    CHECK_EQUAL(3u, audio_engine_status(&engine).token);
    audio_engine_pump(&engine, 2205);
    AudioEngineStatus second = audio_engine_status(&engine);
    CHECK_EQUAL(4u, second.token);
    CHECK_EQUAL(50u, second.position_ms);
}

static void the_last_range_of_a_file_runs_to_its_end_in_the_same_stream(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 50, 100, 4));
    queue_range("lpc_16_44k_stereo", 100, 0, 5);
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 2205, 0, 3795, 16));
    CHECK_EQUAL(1, opens);
    audio_engine_pump(&engine, 2205);
    CHECK_EQUAL(5u, audio_engine_status(&engine).token);
    CHECK_EQUAL(100u, audio_engine_status(&engine).position_ms);
}

static void a_range_of_the_same_file_that_does_not_follow_reopens_it(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 0, 50, 3));
    queue_range("lpc_16_44k_stereo", 20, 0, 4);
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 0, 2205, 16));
    CHECK(ring_holds("lpc_16_44k_stereo", 882, 2205, 3000, 16));
    CHECK_EQUAL(2, opens);
    audio_engine_pump(&engine, 2205);
    CHECK_EQUAL(4u, audio_engine_status(&engine).token);
    CHECK_EQUAL(20u, audio_engine_status(&engine).position_ms);
}

static void seeking_inside_a_range_keeps_its_end(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 0, 50, 3));
    pump_until_full(0);
    CHECK(audio_engine_seek(&engine, 30));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 1323, 0, 882, 16));
    CHECK(frame_is(882, 0, 0, 16));
}

static void a_range_ending_at_the_end_of_the_file_hands_over_to_the_next_track(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 100, 0, 3));
    queue_file("odd_blocks_16", 4);
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 4410, 0, 1590, 16));
    CHECK(ring_holds("odd_blocks_16", 0, 1590, 3001, 16));
}

static void the_status_describes_the_playing_tracks_stream(void)
{
    start_engine();
    CHECK(play_file("lpc_24_96k_stereo", 0, 1));
    AudioTrackInfo track = audio_engine_status(&engine).track;
    CHECK_EQUAL(96000u, track.stream.sample_rate);
    CHECK_EQUAL(24, track.stream.bits_per_sample);
    CHECK_EQUAL(2, track.stream.channels);
    CHECK_EQUAL(5000u, track.stream.total_samples);
    CHECK_EQUAL(25502u, track.audio_bytes);
}

static void the_stream_description_follows_the_audio_into_the_next_track(void)
{
    start_engine();
    CHECK(play_file("odd_blocks_16", 0, 3));
    queue_file("lpc_16_44k_stereo", 4);
    pump_until_full(0);
    CHECK_EQUAL(9451u, audio_engine_status(&engine).track.audio_bytes);
    audio_engine_pump(&engine, 3001 + 441);
    CHECK_EQUAL(18719u, audio_engine_status(&engine).track.audio_bytes);
}

static void a_next_track_at_another_rate_waits_for_the_ring_to_drain(void)
{
    start_engine();
    CHECK(play_file("odd_blocks_16", 0, 3));
    queue_file("lpc_24_96k_stereo", 4);
    pump_until_full(0);
    uint32_t rate = 0;
    CHECK(!audio_engine_wants_rate(&engine, &rate));
    audio_engine_pump(&engine, 3001);
    CHECK(audio_engine_wants_rate(&engine, &rate));
    CHECK_EQUAL(96000u, rate);
    audio_engine_restart(&engine);
    pump_until_full(0);
    CHECK(ring_holds("lpc_24_96k_stereo", 0, 0, 5000, 24));
    CHECK_EQUAL(4u, audio_engine_status(&engine).token);
    CHECK_EQUAL(96000u, audio_engine_rate(&engine));
}

static void starting_part_way_skips_to_that_sample(void)
{
    start_engine();
    CHECK(play_file("lpc_16_44k_stereo", 100, 5));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 4410, 0, 1000, 16));
    CHECK_EQUAL(100u, audio_engine_status(&engine).position_ms);
}

static void seeking_restarts_the_ring_at_the_new_place(void)
{
    start_engine();
    CHECK(play_file("lpc_16_44k_stereo", 0, 5));
    pump_until_full(0);
    audio_engine_pump(&engine, 1000);
    CHECK(audio_engine_seek(&engine, 50));
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 2205, 0, 1000, 16));
    CHECK_EQUAL(50u, audio_engine_status(&engine).position_ms);
    CHECK_EQUAL(5u, audio_engine_status(&engine).token);
}

static void volume_scales_the_samples(void)
{
    start_engine();
    audio_engine_set_volume(&engine, 0);
    CHECK(play_file("lpc_16_44k_stereo", 0, 1));
    pump_until_full(0);
    CHECK(frame_is(1000, 0, 0, 16));
    start_engine();
    audio_engine_set_volume(&engine, FULL_VOLUME - 6);
    CHECK(play_file("lpc_16_44k_stereo", 0, 1));
    pump_until_full(0);
    Fixture pcm = load("lpc_16_44k_stereo", "pcm");
    int32_t full = expected_at(&pcm, 2 * 1000);
    int32_t scaled = sixteen_bit_left(1000);
    int32_t error = scaled * 2 - full;
    int32_t allowed = (full < 0 ? -full : full) / 100 + 2;
    CHECK(error >= -allowed && error <= allowed);
    free(pcm.bytes);
}

static void mono_plays_on_both_channels(void)
{
    start_engine();
    CHECK(play_file("fixed_16_mono", 0, 1));
    pump_until_full(0);
    Fixture pcm = load("fixed_16_mono", "pcm");
    int32_t sample = expected_at(&pcm, 500);
    CHECK(frame_is(500, sample, sample, 16));
    free(pcm.bytes);
}

typedef struct ResampledReference {
    uint32_t *words;
    uint32_t frames;
} ResampledReference;

static void reference_take(AudioResample *resample, ResampledReference *reference)
{
    while (audio_resample_render(resample, &reference->words[2 * reference->frames], 1) == 1) {
        reference->frames++;
    }
}

static bool reference_push(AudioResample *resample, int32_t left, int32_t right)
{
    const int32_t frame[2] = {left, right};
    return audio_resample_push(resample, frame, 1) == 1;
}

static ResampledReference speech_reference(uint32_t first, uint32_t count)
{
    Fixture pcm = load("independent_16", "pcm");
    AudioResample resample;
    audio_resample_start(&resample, 22050);
    ResampledReference reference = {.words = malloc((size_t)count * 4 * sizeof(uint32_t))};
    CHECK(reference.words != NULL);
    for (uint32_t frame = first; frame < first + count; frame++) {
        CHECK(reference_push(&resample, expected_at(&pcm, 2 * frame) * 256,
                             expected_at(&pcm, 2 * frame + 1) * 256));
        reference_take(&resample, &reference);
    }
    audio_resample_finish(&resample);
    reference_take(&resample, &reference);
    free(pcm.bytes);
    return reference;
}

static bool reference_reaches_ring(const ResampledReference *reference)
{
    for (uint32_t frame = 0; frame < reference->frames; frame++) {
        for (int attempt = 0; attempt < 128 && engine.produced <= frame; attempt++) {
            audio_engine_pump(&engine, frame);
        }
        uint32_t slot = 2 * (frame % (engine.config.ring_words / 2));
        if (engine.produced <= frame || ring[slot] != reference->words[2 * frame] ||
            ring[slot + 1] != reference->words[2 * frame + 1]) {
            return false;
        }
        audio_engine_observe(&engine, frame + 1);
    }
    return true;
}

static void a_real_low_rate_flac_keeps_every_sample_and_its_exact_duration(void)
{
    start_engine();
    CHECK(play_file("independent_16", 0, 1));
    CHECK_EQUAL(44100, audio_engine_rate(&engine));
    AudioTrackInfo info = audio_engine_status(&engine).track;
    CHECK_EQUAL(22050, info.stream.sample_rate);
    ResampledReference reference = speech_reference(0, info.stream.total_samples);
    CHECK_EQUAL(info.stream.total_samples * 2, reference.frames);
    CHECK(reference_reaches_ring(&reference));
    for (int step = 0; step < 4; step++) {
        audio_engine_pump(&engine, reference.frames);
    }
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, audio_engine_status(&engine).state);
    free(reference.words);
}

static void low_rate_seeking_and_end_ranges_use_the_decoded_clock(void)
{
    const uint32_t first = 20 * 22050 / 1000;
    const uint32_t limit = 70 * 22050 / 1000;
    start_engine();
    CHECK(play_range("independent_16", 20, 70, 1));
    ResampledReference reference = speech_reference(first, limit - first);
    CHECK(reference_reaches_ring(&reference));
    for (int step = 0; step < 4; step++) {
        audio_engine_pump(&engine, reference.frames);
    }
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK_EQUAL(69, audio_engine_status(&engine).position_ms);
    free(reference.words);
}

static void adjacent_low_rate_chapters_share_filter_history_and_keep_their_positions(void)
{
    start_engine();
    CHECK(play_range("independent_16", 0, 50, 1));
    queue_range("independent_16", 50, 100, 2);
    ResampledReference reference = speech_reference(0, 100 * 22050 / 1000);
    pump_until_full(0);
    const uint32_t boundary = 2 * (50 * 22050 / 1000);
    audio_engine_observe(&engine, boundary - 1);
    CHECK_EQUAL(1, audio_engine_status(&engine).token);
    audio_engine_observe(&engine, boundary);
    CHECK_EQUAL(2, audio_engine_status(&engine).token);
    CHECK_EQUAL(50, audio_engine_status(&engine).position_ms);
    CHECK_EQUAL(1, opens);
    CHECK(memcmp(reference.words, ring, reference.frames * 2 * sizeof ring[0]) == 0);
    CHECK_EQUAL(1, opens);
    CHECK(!engine.rate_pending);
    free(reference.words);
}

static ResampledReference joined_speech_reference(uint32_t frames)
{
    Fixture pcm = load("independent_16", "pcm");
    AudioResample resample;
    audio_resample_start(&resample, 22050);
    ResampledReference reference = {.words = malloc((size_t)frames * 8 * sizeof(uint32_t))};
    CHECK(reference.words != NULL);
    for (uint32_t repeat = 0; repeat < 2; repeat++) {
        for (uint32_t frame = 0; frame < frames; frame++) {
            CHECK(reference_push(&resample, expected_at(&pcm, 2 * frame) * 256,
                                 expected_at(&pcm, 2 * frame + 1) * 256));
            reference_take(&resample, &reference);
        }
    }
    audio_resample_finish(&resample);
    reference_take(&resample, &reference);
    free(pcm.bytes);
    return reference;
}

static void separate_converted_files_match_a_single_continuous_filter(void)
{
    start_engine();
    CHECK(play_file("independent_16", 0, 1));
    uint32_t frames = audio_engine_status(&engine).track.stream.total_samples;
    queue_file("independent_16", 2);
    ResampledReference reference = joined_speech_reference(frames);
    CHECK_EQUAL(frames * 4, reference.frames);
    CHECK(reference_reaches_ring(&reference));
    for (int step = 0; step < 4; step++) {
        audio_engine_pump(&engine, reference.frames);
    }
    CHECK_EQUAL(2, opens);
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, audio_engine_status(&engine).state);
    CHECK_EQUAL(2, audio_engine_status(&engine).token);
    CHECK(!engine.failed && !engine.rate_pending);
    free(reference.words);
}

static void append_scripted_reference(ResampledReference *reference, uint32_t rate, uint32_t frames)
{
    AudioResample resample;
    audio_resample_start(&resample, rate);
    for (uint32_t at = 0; at < frames; at++) {
        CHECK(
            reference_push(&resample, scripted.left[at % 16] * 256, scripted.right[at % 16] * 256));
        reference_take(&resample, reference);
    }
    audio_resample_finish(&resample);
    reference_take(&resample, reference);
}

static void every_converted_rate_keeps_history_across_short_queued_files(void)
{
    static const uint32_t RATES[] = {8000, 11025, 12000, 16000, 22050, 24000};
    uint32_t words[384];
    for (uint32_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        start_scripted_engine(0, 1);
        scripted.rate = RATES[index];
        scripted.frames_per_open = 1;
        CHECK(play_file("first", 0, 1));
        queue_file("second", 2);
        ResampledReference reference = {.words = words};
        append_scripted_reference(&reference, RATES[index], 32);
        CHECK_EQUAL(32 * (audio_resample_rate(RATES[index]) / RATES[index]), reference.frames);
        CHECK(reference_reaches_ring(&reference));
        pump_until_full(reference.frames);
        CHECK_EQUAL(reference.frames, engine.end_at);
        CHECK(engine.ended && !engine.failed);
        uint32_t boundary = reference.frames / 2;
        CHECK_EQUAL(2, audio_engine_status(&engine).token);
        CHECK_EQUAL(boundary, engine.segments[0].start);
    }
}

static void different_converted_inputs_at_the_same_output_clock_drain_and_restart_exactly(void)
{
    start_scripted_engine(0, 1);
    scripted.rate = 8000;
    scripted.next_rate = 16000;
    scripted.frames_per_open = 1;
    engine.config.ring_words = 16;
    CHECK(play_file("first", 0, 1));
    queue_file("second", 2);
    uint32_t words[288];
    ResampledReference reference = {.words = words};
    append_scripted_reference(&reference, 8000, 16);
    uint32_t boundary = reference.frames;
    append_scripted_reference(&reference, 16000, 16);
    CHECK(reference_reaches_ring(&reference));
    pump_until_full(reference.frames);
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK_EQUAL(boundary, engine.segments[0].start);
    CHECK_EQUAL(2, audio_engine_status(&engine).token);
    CHECK_EQUAL(32000, audio_engine_rate(&engine));
    CHECK(engine.ended && !engine.failed && !engine.rate_pending);
}

static void wideband_speech_and_native_32_khz_files_share_one_output_clock(void)
{
    start_scripted_engine(0, 1);
    scripted.rate = 16000;
    scripted.next_rate = 32000;
    scripted.frames_per_open = 1;
    engine.config.ring_words = 16;
    CHECK(play_file("first", 0, 1));
    queue_file("second", 2);
    uint32_t words[128];
    ResampledReference reference = {.words = words};
    append_scripted_reference(&reference, 16000, 16);
    uint32_t boundary = reference.frames;
    for (uint32_t at = 0; at < 16; at++) {
        words[2 * reference.frames] = word_of(scripted.left[at], 16);
        words[2 * reference.frames + 1] = word_of(scripted.right[at], 16);
        reference.frames++;
    }
    CHECK(reference_reaches_ring(&reference));
    pump_until_full(reference.frames);
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK_EQUAL(boundary, engine.segments[0].start);
    CHECK_EQUAL(32000, audio_engine_rate(&engine));
    CHECK(engine.ended && !engine.failed && !engine.rate_pending);
}

static void clearing_the_next_file_during_a_filter_drain_rebuilds_the_current_tail(void)
{
    start_scripted_engine(0, 1);
    scripted.rate = 8000;
    scripted.next_rate = 16000;
    scripted.frames_per_open = 1;
    engine.config.ring_words = 16;
    CHECK(play_file("first", 0, 1));
    queue_file("second", 2);
    pump_until_full(0);
    CHECK(engine.resample.ended && !engine.ended);
    CHECK_EQUAL(1, engine.segment_count);
    CHECK(audio_engine_queue_needs_restart(&engine, NULL));
    audio_engine_pause(&engine);
    audio_engine_queue_next(&engine, NULL);
    CHECK_EQUAL(AUDIO_ENGINE_PAUSED, audio_engine_status(&engine).state);
    uint32_t words[192];
    ResampledReference reference = {.words = words};
    append_scripted_reference(&reference, 8000, 16);
    CHECK(reference_reaches_ring(&reference));
    pump_until_full(reference.frames);
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK_EQUAL(1, audio_engine_status(&engine).token);
    CHECK(!engine.has_next && !engine.failed);
}

static void converted_tails_finish_after_multiple_small_ring_refills(void)
{
    start_engine();
    engine.config.ring_words = 16;
    CHECK(play_file("independent_16", 0, 1));
    uint32_t frames = audio_engine_status(&engine).track.stream.total_samples;
    ResampledReference reference = speech_reference(0, frames);
    CHECK(reference_reaches_ring(&reference));
    pump_until_full(reference.frames);
    CHECK(engine.ended);
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, audio_engine_status(&engine).state);
    free(reference.words);
}

static void an_underrun_while_the_final_filter_tail_drains_is_still_counted(void)
{
    start_scripted_engine(0, 1);
    scripted.rate = 8000;
    engine.config.ring_words = 16;
    CHECK(play_file("short", 0, 1));
    pump_until_full(0);
    CHECK(engine.resample.ended && !engine.ended);
    uint32_t expected = audio_engine_stats(&engine).underruns + 1;
    audio_engine_observe(&engine, engine.produced + 1);
    CHECK_EQUAL(expected, audio_engine_stats(&engine).underruns);
}

static void an_unreadable_next_file_preserves_the_converted_current_tail(void)
{
    start_engine();
    CHECK(play_file("independent_16", 0, 1));
    uint32_t frames = audio_engine_status(&engine).track.stream.total_samples;
    queue_file("missing", 2);
    ResampledReference reference = speech_reference(0, frames);
    CHECK(reference_reaches_ring(&reference));
    pump_until_full(reference.frames);
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK(engine.ended && engine.failed);
    CHECK_EQUAL(1, audio_engine_status(&engine).token);
    free(reference.words);
}

static AudioSource eviction_source;
static bool kernel_available;
static bool kernel_restore;
static uint32_t invalid_kernel_pulls;

static bool evicting_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    bool opened_source = eviction_source.open(context, path, start_ms, track);
    if (!opened_source) {
        kernel_available = false;
    }
    return opened_source;
}

static bool restore_kernel(void)
{
    kernel_available = kernel_restore;
    return kernel_available;
}

static uint32_t checked_kernel(AudioResample *resample, uint32_t *words, uint32_t frames)
{
    if (!kernel_available) {
        invalid_kernel_pulls++;
        return 0;
    }
    return audio_resample_render(resample, words, frames);
}

static void a_failed_next_open_reloads_evicted_audio_code_before_draining(void)
{
    start_engine();
    eviction_source = engine.config.source;
    engine.config.source.open = evicting_open;
    engine.config.prepare_resample = restore_kernel;
    engine.config.resample_render = checked_kernel;
    kernel_restore = true;
    invalid_kernel_pulls = 0;
    CHECK(play_file("independent_16", 0, 1));
    uint32_t frames = audio_engine_status(&engine).track.stream.total_samples;
    queue_file("missing", 2);
    ResampledReference reference = speech_reference(0, frames);
    CHECK(reference_reaches_ring(&reference));
    pump_until_full(reference.frames);
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK_EQUAL(0, invalid_kernel_pulls);
    CHECK(engine.ended && engine.failed);
    free(reference.words);
}

static void unavailable_evicted_audio_code_finishes_with_an_error_without_calling_it(void)
{
    start_engine();
    eviction_source = engine.config.source;
    engine.config.source.open = evicting_open;
    engine.config.prepare_resample = restore_kernel;
    engine.config.resample_render = checked_kernel;
    kernel_restore = true;
    invalid_kernel_pulls = 0;
    CHECK(play_file("independent_16", 0, 1));
    kernel_restore = false;
    queue_file("missing", 2);
    pump_until_full(0);
    CHECK(engine.ended && engine.failed && !engine.decoding);
    CHECK_EQUAL(0, invalid_kernel_pulls);
    audio_engine_observe(&engine, engine.end_at);
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, audio_engine_status(&engine).state);
}

static void low_rate_filter_tail_drains_before_the_next_output_clock_is_requested(void)
{
    start_engine();
    CHECK(play_file("independent_16", 0, 1));
    uint32_t frames = audio_engine_status(&engine).track.stream.total_samples;
    queue_file("lpc_24_96k_stereo", 2);
    ResampledReference reference = speech_reference(0, frames);
    CHECK(reference_reaches_ring(&reference));
    for (int step = 0; step < 4 && !engine.rate_pending; step++) {
        audio_engine_pump(&engine, reference.frames);
    }
    uint32_t rate = 0;
    CHECK(audio_engine_wants_rate(&engine, &rate));
    CHECK_EQUAL(96000, rate);
    CHECK_EQUAL(reference.frames, engine.produced);
    CHECK_EQUAL(44100, audio_engine_rate(&engine));
    audio_engine_restart(&engine);
    CHECK_EQUAL(96000, audio_engine_rate(&engine));
    CHECK_EQUAL(2, audio_engine_status(&engine).token);
    free(reference.words);
}

static bool unavailable_resampler(void)
{
    return false;
}

static void missing_optional_audio_code_only_refuses_tracks_that_need_it(void)
{
    start_engine();
    engine.config.prepare_resample = unavailable_resampler;
    CHECK(!play_file("independent_16", 0, 1));
    CHECK_EQUAL(AUDIO_REFUSAL_UNREADABLE, audio_engine_refusal(&engine).problem);
    CHECK(play_file("odd_blocks_16", 0, 2));
    CHECK_EQUAL(44100, audio_engine_rate(&engine));
}

static void an_unsupported_rate_is_refused(void)
{
    start_scripted_engine(0, 1);
    scripted.rate = 13000;
    CHECK(!play_file("scripted", 0, 1));
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, audio_engine_status(&engine).state);
    AudioRefusal refusal = audio_engine_refusal(&engine);
    CHECK_EQUAL(AUDIO_REFUSAL_RATE, refusal.problem);
    CHECK_EQUAL(13000u, refusal.sample_rate);
}

static void a_queued_track_at_an_unsupported_rate_is_refused_with_its_rate(void)
{
    start_scripted_engine(0, 1);
    CHECK(play_file("scripted", 0, 3));
    scripted.rate = 13000;
    queue_file("scripted", 4);
    pump_until_full(0);
    audio_engine_pump(&engine, 3001);
    CHECK(audio_engine_status(&engine).failed);
    CHECK_EQUAL(AUDIO_REFUSAL_RATE, audio_engine_refusal(&engine).problem);
    CHECK_EQUAL(13000u, audio_engine_refusal(&engine).sample_rate);
}

static bool scripted_rate_plays(uint32_t rate)
{
    start_scripted_engine(0, 1);
    scripted.rate = rate;
    return play_file("scripted", 0, 1);
}

static void speech_rates_use_the_matching_supported_output_clock(void)
{
    static const uint32_t INPUTS[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000};
    static const uint32_t OUTPUTS[] = {32000, 44100, 48000, 32000, 44100, 48000, 32000};
    for (size_t index = 0; index < sizeof INPUTS / sizeof INPUTS[0]; index++) {
        CHECK(scripted_rate_plays(INPUTS[index]));
        CHECK_EQUAL(OUTPUTS[index], audio_engine_rate(&engine));
        CHECK_EQUAL(INPUTS[index], audio_engine_status(&engine).track.stream.sample_rate);
    }
}

static bool full_clock_after_playing(uint32_t rate, bool stereo)
{
    CHECK(scripted_rate_plays(rate));
    for (int index = 0; index < 16 && !stereo; index++) {
        scripted.right[index] = scripted.left[index];
    }
    pump_until_full(0);
    return audio_engine_needs_full_clock(&engine);
}

static void only_stereo_conversions_and_conversions_above_32_khz_need_the_full_clock(void)
{
    CHECK(!full_clock_after_playing(8000, false));
    CHECK(!full_clock_after_playing(16000, false));
    CHECK(full_clock_after_playing(16000, true));
    CHECK(full_clock_after_playing(11025, false));
    CHECK(full_clock_after_playing(24000, false));
    CHECK(!full_clock_after_playing(44100, true));
    CHECK(!full_clock_after_playing(32000, true));
}

static void rates_up_to_192_khz_play_and_higher_ones_are_refused(void)
{
    CHECK(scripted_rate_plays(192000));
    CHECK_EQUAL(192000u, audio_engine_rate(&engine));
    CHECK(scripted_rate_plays(176400));
    CHECK_EQUAL(176400u, audio_engine_rate(&engine));
    CHECK(scripted_rate_plays(96000));
    CHECK(!scripted_rate_plays(384000));
    CHECK_EQUAL(AUDIO_REFUSAL_RATE, audio_engine_refusal(&engine).problem);
    CHECK_EQUAL(384000u, audio_engine_refusal(&engine).sample_rate);
}

static void a_missing_file_is_refused(void)
{
    start_engine();
    CHECK(!play_file("no_such_file", 0, 1));
    CHECK_EQUAL(AUDIO_REFUSAL_UNREADABLE, audio_engine_refusal(&engine).problem);
}

static void the_ring_never_runs_ahead_of_the_dma_by_more_than_its_size(void)
{
    start_engine();
    CHECK(play_file("constant_and_noise", 0, 1));
    pump_until_full(0);
    CHECK(audio_engine_buffered(&engine, 0) <= RING_FRAMES);
    CHECK(audio_engine_buffered(&engine, 0) >= RING_FRAMES - 4608);
}

static void observing_the_dma_position_never_decodes(void)
{
    start_engine();
    CHECK(play_file("lpc_16_44k_stereo", 0, 7));
    pump_until_full(0);
    uint32_t produced = audio_engine_buffered(&engine, 0);
    audio_engine_observe(&engine, 4410);
    CHECK_EQUAL(100u, audio_engine_status(&engine).position_ms);
    CHECK_EQUAL(produced - 4410, audio_engine_buffered(&engine, 4410));
    audio_engine_pump(&engine, 4410);
    CHECK(audio_engine_buffered(&engine, 4410) > produced - 4410);
}

static void the_dma_overtaking_the_decoder_counts_an_underrun(void)
{
    start_engine();
    CHECK(play_file("constant_and_noise", 0, 7));
    pump_until_full(0);
    CHECK_EQUAL(0u, audio_engine_stats(&engine).underruns);
    audio_engine_pump(&engine, RING_FRAMES + 100);
    CHECK_EQUAL(1u, audio_engine_stats(&engine).underruns);
    CHECK(audio_engine_buffered(&engine, RING_FRAMES + 100) <= RING_FRAMES);
    audio_engine_pump(&engine, RING_FRAMES + 200);
    CHECK_EQUAL(1u, audio_engine_stats(&engine).underruns);
}

static void every_decoded_frame_is_counted(void)
{
    start_engine();
    CHECK(play_file("lpc_16_44k_stereo", 0, 7));
    CHECK_EQUAL(0u, audio_engine_stats(&engine).frames);
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(1u, audio_engine_stats(&engine).frames);
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(2u, audio_engine_stats(&engine).frames);
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(2u, audio_engine_stats(&engine).frames);
}

static void the_current_path_is_the_track_being_played(void)
{
    start_engine();
    CHECK(audio_engine_current_path(&engine) == NULL);
    CHECK(play_file("lpc_16_44k_stereo", 0, 1));
    CHECK(strcmp("lpc_16_44k_stereo", audio_engine_current_path(&engine)) == 0);
}

static void a_source_that_is_not_ready_leaves_the_ring_alone(void)
{
    start_scripted_engine(2, 1);
    CHECK(play_file("scripted", 0, 1));
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(0u, audio_engine_buffered(&engine, 0));
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(0u, audio_engine_buffered(&engine, 0));
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(16u, audio_engine_buffered(&engine, 0));
    CHECK(frame_is(0, 1, -1, 16));
    CHECK(frame_is(15, 16, -16, 16));
}

static void a_frame_is_released_once_its_samples_are_in_the_ring(void)
{
    start_scripted_engine(0, 2);
    CHECK(play_file("scripted", 0, 1));
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(1, scripted.releases);
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(2, scripted.releases);
    audio_engine_stop(&engine);
    CHECK_EQUAL(1, scripted.closes);
}

static Equalizer equalizer;

static const HalAudioEqualizer POP = {
    .band_hz = {100, 3150, 10000}, .gain_half_db = {8, -6, 6}, .band_count = 3, .pre_gain = 20480};
static const HalAudioEqualizer FLAT = {.pre_gain = HAL_AUDIO_UNITY_PRE_GAIN};

static void start_equalized_engine(const HalAudioEqualizer *curve)
{
    start_engine();
    engine.config.equalizer = &equalizer;
    audio_engine_set_equalizer(&engine, curve);
}

static bool equalized_ring_holds(const char *name, const HalAudioEqualizer *curve, int bits)
{
    start_equalized_engine(&POP);
    audio_engine_set_equalizer(&engine, curve);
    CHECK(play_file(name, 0, 1));
    pump_until_full(0);
    return ring_holds(name, 0, 0, 5000, bits);
}

static void equalizer_off_leaves_sixteen_bit_samples_bit_identical(void)
{
    CHECK(equalized_ring_holds("lpc_16_44k_stereo", &FLAT, 16));
}

static void equalizer_off_leaves_twenty_four_bit_samples_bit_identical(void)
{
    CHECK(equalized_ring_holds("lpc_24_96k_stereo", &FLAT, 24));
}

static void an_equalizer_shapes_sixteen_and_twenty_four_bit_audio(void)
{
    CHECK(!equalized_ring_holds("lpc_16_44k_stereo", &POP, 16));
    CHECK(engine.equalizing);
    CHECK(!equalized_ring_holds("lpc_24_96k_stereo", &POP, 24));
    CHECK(engine.equalizing);
}

static void the_equalizer_stands_aside_above_96_khz(void)
{
    start_scripted_engine(0, 1);
    scripted.rate = 192000;
    engine.config.equalizer = &equalizer;
    audio_engine_set_equalizer(&engine, &POP);
    CHECK(play_file("scripted", 0, 1));
    pump_until_full(0);
    CHECK(!engine.equalizing);
    CHECK(frame_is(0, 1, -1, 16));
    CHECK(frame_is(15, 16, -16, 16));
}

static void changing_only_the_gains_keeps_the_filter_history(void)
{
    static const HalAudioEqualizer LOUDER = {.band_hz = {100, 3150, 10000},
                                             .gain_half_db = {10, -6, 6},
                                             .band_count = 3,
                                             .pre_gain = 18000};
    start_equalized_engine(&POP);
    CHECK(play_file("lpc_16_44k_stereo", 0, 1));
    pump_until_full(0);
    EqualizerHistory before = equalizer.history[0][1];
    CHECK(before.x1 != 0 || before.y1 != 0);
    audio_engine_set_equalizer(&engine, &LOUDER);
    CHECK(engine.equalizing);
    CHECK_EQUAL(before.x1, equalizer.history[0][1].x1);
    CHECK_EQUAL(before.y2, equalizer.history[0][1].y2);
}

static void changing_the_bands_starts_the_filter_from_silence(void)
{
    static const HalAudioEqualizer BASS = {
        .band_hz = {63}, .gain_half_db = {8}, .band_count = 1, .pre_gain = 20480};
    start_equalized_engine(&POP);
    CHECK(play_file("lpc_16_44k_stereo", 0, 1));
    pump_until_full(0);
    audio_engine_set_equalizer(&engine, &BASS);
    CHECK_EQUAL(0, equalizer.history[0][0].x1);
    CHECK_EQUAL(0, equalizer.history[0][0].y1);
}

static void replacing_a_buffered_next_song_keeps_the_current_position(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 0, 50, 3));
    queue_file("odd_blocks_16", 4);
    pump_until_full(0);
    audio_engine_observe(&engine, 441);
    audio_engine_pause(&engine);
    AudioEngineTrack replacement = {.path = "lpc_16_44k_stereo", .token = 5};
    audio_engine_queue_next(&engine, &replacement);
    pump_until_full(0);
    CHECK_EQUAL(3u, audio_engine_status(&engine).token);
    CHECK_EQUAL(10u, audio_engine_status(&engine).position_ms);
    CHECK_EQUAL(AUDIO_ENGINE_PAUSED, audio_engine_status(&engine).state);
    CHECK(ring_holds("lpc_16_44k_stereo", 441, 0, 1764, 16));
    CHECK(ring_holds("lpc_16_44k_stereo", 0, 1764, 3000, 16));
    audio_engine_observe(&engine, 1764);
    CHECK_EQUAL(5u, audio_engine_status(&engine).token);
}

static void clearing_a_buffered_next_song_leaves_only_the_current_tail(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 0, 50, 3));
    queue_file("odd_blocks_16", 4);
    pump_until_full(0);
    audio_engine_observe(&engine, 441);
    audio_engine_pause(&engine);
    audio_engine_queue_next(&engine, NULL);
    pump_until_full(0);
    CHECK(ring_holds("lpc_16_44k_stereo", 441, 0, 1764, 16));
    CHECK(frame_is(1764, 0, 0, 16));
    audio_engine_observe(&engine, 1764);
    CHECK_EQUAL(3u, audio_engine_status(&engine).token);
    CHECK_EQUAL(AUDIO_ENGINE_STOPPED, audio_engine_status(&engine).state);
}

static void replacing_a_pending_rate_change_restarts_at_the_current_rate(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 0, 50, 3));
    queue_file("lpc_24_96k_stereo", 4);
    pump_until_full(0);
    CHECK(engine.rate_pending);
    audio_engine_observe(&engine, 441);
    queue_file("odd_blocks_16", 5);
    pump_until_full(0);
    CHECK(!engine.rate_pending);
    CHECK_EQUAL(44100u, audio_engine_rate(&engine));
    CHECK(ring_holds("odd_blocks_16", 0, 1764, 3001, 16));
    audio_engine_observe(&engine, 1764);
    CHECK_EQUAL(5u, audio_engine_status(&engine).token);
}

static void clearing_an_already_empty_queue_preserves_prefetched_audio_and_position(void)
{
    start_engine();
    CHECK(play_file("independent_16", 0, 1));
    pump_until_full(0);
    CHECK(engine.ended);
    audio_engine_observe(&engine, 441);
    uint32_t produced = engine.produced;
    uint32_t position = audio_engine_status(&engine).position_ms;
    int opened_count = opens;
    audio_engine_queue_next(&engine, NULL);
    CHECK_EQUAL(opened_count, opens);
    CHECK_EQUAL(produced, engine.produced);
    CHECK_EQUAL(position, audio_engine_status(&engine).position_ms);
    CHECK_EQUAL(441, engine.consumed);
}

static void adding_after_the_decoder_ended_replaces_buffered_silence(void)
{
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 0, 50, 3));
    pump_until_full(0);
    CHECK(engine.ended);
    audio_engine_observe(&engine, 441);
    queue_file("odd_blocks_16", 5);
    pump_until_full(0);
    CHECK(ring_holds("odd_blocks_16", 0, 1764, 3001, 16));
    audio_engine_observe(&engine, 1764);
    CHECK_EQUAL(5u, audio_engine_status(&engine).token);
}

static uint32_t gained_word(uint32_t word, uint32_t gain)
{
    int64_t value = (int64_t)((int32_t)word / 256) * gain + 32768;
    value = value >= 0 ? value / 65536 : -((-value + 65535) / 65536);
    value = value > 8388607 ? 8388607 : value < -8388608 ? -8388608 : value;
    return (uint32_t)value << 8;
}

static void replay_gain_scales_pcm_and_unity_matches_the_existing_output_exactly(void)
{
    static uint32_t reference[RING_WORDS];
    start_engine();
    CHECK(play_file("lpc_16_44k_stereo", 0, 1));
    pump_until_full(0);
    memcpy(reference, ring, sizeof reference);
    static const uint32_t GAINS[] = {0, 65536, 32768, 131072};
    for (uint32_t at = 0; at < sizeof GAINS / sizeof GAINS[0]; at++) {
        start_engine();
        AudioEngineTrack track = {
            .path = "lpc_16_44k_stereo", .token = 1, .replay_gain = GAINS[at]};
        CHECK(audio_engine_play(&engine, &track, 0));
        pump_until_full(0);
        uint32_t gain = GAINS[at] == 0 ? 65536 : GAINS[at];
        for (uint32_t sample = 0; sample < RING_WORDS; sample++) {
            uint32_t expected = gained_word(reference[sample], gain);
            CHECK_EQUAL(expected, ring[sample]);
            if (expected != ring[sample]) {
                break;
            }
        }
    }
}

static void contiguous_chapters_change_gain_without_reopening_the_file(void)
{
    static uint32_t reference[176];
    start_engine();
    CHECK(play_range("lpc_16_44k_stereo", 0, 2, 1));
    pump_until_full(0);
    memcpy(reference, ring, sizeof reference);
    start_engine();
    AudioEngineTrack first = {
        .path = "lpc_16_44k_stereo", .end_ms = 1, .token = 1, .replay_gain = 65536};
    AudioEngineTrack next = {
        .path = "lpc_16_44k_stereo", .start_ms = 1, .end_ms = 2, .token = 2, .replay_gain = 32768};
    CHECK(audio_engine_play(&engine, &first, 0));
    audio_engine_queue_next(&engine, &next);
    pump_until_full(0);
    CHECK_EQUAL(1, opens);
    CHECK_EQUAL(88, engine.end_at);
    for (uint32_t sample = 0; sample < 176; sample++) {
        CHECK_EQUAL(gained_word(reference[sample], sample < 88 ? 65536 : 32768), ring[sample]);
    }
}

static void live_gain_updates_future_samples_without_restarting_and_survives_seek(void)
{
    start_engine();
    engine.config.ring_words = 2048;
    CHECK(play_file("lpc_16_44k_stereo", 0, 5));
    pump_until_full(0);
    audio_engine_set_replay_gain(&engine, 999, 131072);
    audio_engine_set_replay_gain(&engine, 5, 32768);
    pump_until_full(128);
    CHECK_EQUAL(1, opens);
    Fixture pcm = load("lpc_16_44k_stereo", "pcm");
    CHECK(pcm.size >= 2 * (1024 + 128) * sizeof(int32_t));
    for (uint32_t frame = 0; frame < 128; frame++) {
        uint32_t pcm_index = 2 * (1024 + frame);
        CHECK_EQUAL(gained_word(word_of(expected_at(&pcm, pcm_index), 16), 32768), ring[2 * frame]);
    }
    CHECK(audio_engine_seek(&engine, 0));
    pump_until_full(0);
    CHECK_EQUAL(gained_word(word_of(expected_at(&pcm, 1000), 16), 32768), ring[1000]);
    free(pcm.bytes);
}

static void boosted_extreme_samples_saturate_safely_at_every_admitted_bit_depth(void)
{
    static const uint8_t BITS[] = {4, 8, 16, 24, 32};
    for (uint32_t at = 0; at < sizeof BITS / sizeof BITS[0]; at++) {
        start_scripted_engine(0, 1);
        scripted.bits = BITS[at];
        for (uint32_t frame = 0; frame < 16; frame++) {
            scripted.left[frame] = INT32_MAX;
            scripted.right[frame] = INT32_MIN;
        }
        AudioEngineTrack track = {.path = "samples", .token = 1, .replay_gain = UINT32_MAX};
        CHECK(audio_engine_play(&engine, &track, 0));
        pump_until_full(0);
        CHECK_EQUAL(UINT32_C(0x7FFFFF00), ring[0]);
        CHECK_EQUAL(UINT32_C(0x80000000), ring[1]);
        audio_engine_set_volume(&engine, 0);
        CHECK(audio_engine_seek(&engine, 0));
        scripted.frames_left = 1;
        pump_until_full(0);
        CHECK_EQUAL(0, ring[0]);
        CHECK_EQUAL(0, ring[1]);
    }
}

static void normalization_composes_with_volume_before_saturation(void)
{
    start_scripted_engine(0, 1);
    for (uint32_t frame = 0; frame < 16; frame++) {
        scripted.left[frame] = 20000;
        scripted.right[frame] = -20000;
    }
    audio_engine_set_volume(&engine, 48);
    CHECK(engine.gain > 16000 && engine.gain < 17000);
    uint32_t combined = engine.gain * 4;
    AudioEngineTrack track = {.path = "samples", .token = 1, .replay_gain = 4 * 65536};
    CHECK(audio_engine_play(&engine, &track, 0));
    pump_until_full(0);
    CHECK_EQUAL(gained_word(word_of(20000, 16), combined), ring[0]);
    CHECK_EQUAL(gained_word(word_of(-20000, 16), combined), ring[1]);
}

static void converted_files_apply_their_gain_before_sharing_filter_history(void)
{
    start_scripted_engine(0, 1);
    scripted.rate = 8000;
    scripted.frames_per_open = 1;
    static uint32_t words[512];
    ResampledReference reference = {.words = words};
    AudioResample resample;
    audio_resample_start(&resample, 8000);
    for (uint32_t file = 0; file < 2; file++) {
        for (uint32_t frame = 0; frame < 16; frame++) {
            int32_t scale = file == 0 ? 256 : 128;
            CHECK(reference_push(&resample, scripted.left[frame] * scale,
                                 scripted.right[frame] * scale));
            reference_take(&resample, &reference);
        }
    }
    audio_resample_finish(&resample);
    reference_take(&resample, &reference);
    AudioEngineTrack first = {.path = "first", .token = 1, .replay_gain = 65536};
    AudioEngineTrack next = {.path = "second", .token = 2, .replay_gain = 32768};
    CHECK(audio_engine_play(&engine, &first, 0));
    audio_engine_queue_next(&engine, &next);
    pump_until_full(0);
    CHECK_EQUAL(32 * audio_resample_rate(8000) / 8000, reference.frames);
    CHECK_EQUAL(reference.frames, engine.end_at);
    CHECK(memcmp(words, ring, reference.frames * 2 * sizeof(uint32_t)) == 0);
}

static void a_live_gain_for_a_prefetched_track_is_preserved_when_seeking_into_it(void)
{
    start_engine();
    AudioEngineTrack first = {.path = "lpc_16_44k_stereo", .end_ms = 1, .token = 1};
    AudioEngineTrack next = {.path = "lpc_16_44k_stereo", .token = 2};
    CHECK(audio_engine_play(&engine, &first, 0));
    audio_engine_queue_next(&engine, &next);
    pump_until_full(0);
    CHECK_EQUAL(2, opens);
    audio_engine_set_replay_gain(&engine, 1, 131072);
    audio_engine_set_replay_gain(&engine, 2, 32768);
    CHECK_EQUAL(2, opens);
    audio_engine_observe(&engine, 44);
    CHECK_EQUAL(2, audio_engine_status(&engine).token);
    CHECK(audio_engine_seek(&engine, 0));
    pump_until_full(0);
    Fixture pcm = load("lpc_16_44k_stereo", "pcm");
    CHECK_EQUAL(gained_word(word_of(expected_at(&pcm, 1000), 16), 32768), ring[1000]);
    free(pcm.bytes);
}

static void live_normalization_targets_the_decoder_while_a_new_output_clock_waits(void)
{
    start_engine();
    AudioEngineTrack first = {.path = "odd_blocks_16", .token = 1, .replay_gain = 65536};
    AudioEngineTrack next = {.path = "lpc_24_96k_stereo", .token = 2, .replay_gain = 32768};
    CHECK(audio_engine_play(&engine, &first, 0));
    audio_engine_queue_next(&engine, &next);
    pump_until_full(0);
    CHECK(engine.rate_pending);
    CHECK_EQUAL(1, audio_engine_status(&engine).token);
    audio_engine_set_replay_gain(&engine, 1, 131072);
    audio_engine_set_replay_gain(&engine, 2, 16384);
    audio_engine_pump(&engine, 3001);
    uint32_t rate = 0;
    CHECK(audio_engine_wants_rate(&engine, &rate));
    CHECK_EQUAL(96000, rate);
    audio_engine_restart(&engine);
    pump_until_full(0);
    Fixture pcm = load("lpc_24_96k_stereo", "pcm");
    for (uint32_t sample = 0; sample < 200; sample++) {
        uint32_t expected = gained_word(word_of(expected_at(&pcm, sample), 24), 16384);
        CHECK_EQUAL(expected, ring[sample]);
        if (expected != ring[sample]) {
            break;
        }
    }
    free(pcm.bytes);
}

static void headphones_compose_before_clipping_at_native_and_converted_rates(void)
{
    const uint32_t rates[] = {8000, 16000, 44100, 96000, 192000};
    uint32_t reference[4096];
    for (unsigned rate = 0; rate < sizeof rates / sizeof rates[0]; rate++) {
        for (int pass = 0; pass < 2; pass++) {
            start_scripted_engine(0, 1);
            scripted.rate = rates[rate];
            scripted.bits = 24;
            for (int frame = 0; frame < 16; frame++) {
                scripted.left[frame] = 7000000;
                scripted.right[frame] = -7000000;
            }
            if (pass == 1) {
                audio_engine_set_headphones(&engine, (HeadphoneAudio){16384, 8192, 60});
            }
            AudioEngineTrack track = {
                .path = "first", .token = 1, .replay_gain = pass == 0 ? 32768 : 131072};
            CHECK(audio_engine_play(&engine, &track, 0));
            pump_until_full(0);
            CHECK(engine.end_at > 0 && engine.end_at * 2 <= 4096);
            for (uint32_t frame = 0; frame < engine.end_at; frame++) {
                if (pass == 0) {
                    reference[2 * frame] = ring[2 * frame];
                    reference[2 * frame + 1] = ring[2 * frame + 1];
                } else {
                    CHECK_EQUAL(reference[2 * frame], ring[2 * frame]);
                    int32_t right = (int32_t)ring[2 * frame + 1] / 256;
                    int32_t expected = (int32_t)reference[2 * frame + 1] / 512;
                    CHECK(right >= expected - 1 && right <= expected + 1);
                }
            }
        }
    }
}

static void sleep_fades_in_the_pcm_stage_and_cancel_restores_unity(void)
{
    start_scripted_engine(0, 1);
    CHECK(play_file("first", 0, 1));
    audio_engine_set_sleep_remaining(&engine, 30000);
    audio_engine_pump(&engine, 0);
    int32_t half = (int32_t)ring[30] >> 8;
    CHECK(half >= 2032 && half <= 2048);
    CHECK(play_file("first", 0, 1));
    scripted.frames_left = 1;
    audio_engine_set_sleep_remaining(&engine, UINT32_MAX);
    audio_engine_pump(&engine, 0);
    CHECK_EQUAL(16, sixteen_bit_left(15));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(sleep_fades_in_the_pcm_stage_and_cancel_restores_unity),
        TEST_CASE(headphones_compose_before_clipping_at_native_and_converted_rates),
        TEST_CASE(replacing_a_buffered_next_song_keeps_the_current_position),
        TEST_CASE(clearing_a_buffered_next_song_leaves_only_the_current_tail),
        TEST_CASE(replacing_a_pending_rate_change_restarts_at_the_current_rate),
        TEST_CASE(clearing_an_already_empty_queue_preserves_prefetched_audio_and_position),
        TEST_CASE(adding_after_the_decoder_ended_replaces_buffered_silence),

        TEST_CASE(changing_only_the_gains_keeps_the_filter_history),
        TEST_CASE(changing_the_bands_starts_the_filter_from_silence),
        TEST_CASE(equalizer_off_leaves_sixteen_bit_samples_bit_identical),
        TEST_CASE(equalizer_off_leaves_twenty_four_bit_samples_bit_identical),
        TEST_CASE(an_equalizer_shapes_sixteen_and_twenty_four_bit_audio),
        TEST_CASE(the_equalizer_stands_aside_above_96_khz),
        TEST_CASE(the_current_path_is_the_track_being_played),
        TEST_CASE(a_source_that_is_not_ready_leaves_the_ring_alone),
        TEST_CASE(a_frame_is_released_once_its_samples_are_in_the_ring),
        TEST_CASE(observing_the_dma_position_never_decodes),
        TEST_CASE(the_dma_overtaking_the_decoder_counts_an_underrun),
        TEST_CASE(every_decoded_frame_is_counted),
        TEST_CASE(playing_fills_the_ring_with_the_track),
        TEST_CASE(the_position_follows_what_the_dma_consumed),
        TEST_CASE(twenty_four_bit_audio_reaches_the_ring_exactly),
        TEST_CASE(the_end_of_a_track_without_a_next_one_stops_and_goes_silent),
        TEST_CASE(a_queued_track_follows_without_a_gap),
        TEST_CASE(a_range_ends_at_its_end_and_goes_silent),
        TEST_CASE(a_range_starts_at_its_start_and_reports_file_positions),
        TEST_CASE(a_range_starting_in_a_later_frame_starts_at_its_exact_sample),
        TEST_CASE(consecutive_ranges_of_one_file_continue_in_the_same_stream),
        TEST_CASE(the_last_range_of_a_file_runs_to_its_end_in_the_same_stream),
        TEST_CASE(a_range_of_the_same_file_that_does_not_follow_reopens_it),
        TEST_CASE(seeking_inside_a_range_keeps_its_end),
        TEST_CASE(a_range_ending_at_the_end_of_the_file_hands_over_to_the_next_track),
        TEST_CASE(the_status_describes_the_playing_tracks_stream),
        TEST_CASE(the_stream_description_follows_the_audio_into_the_next_track),
        TEST_CASE(a_next_track_at_another_rate_waits_for_the_ring_to_drain),
        TEST_CASE(starting_part_way_skips_to_that_sample),
        TEST_CASE(seeking_restarts_the_ring_at_the_new_place),
        TEST_CASE(volume_scales_the_samples),
        TEST_CASE(replay_gain_scales_pcm_and_unity_matches_the_existing_output_exactly),
        TEST_CASE(contiguous_chapters_change_gain_without_reopening_the_file),
        TEST_CASE(live_gain_updates_future_samples_without_restarting_and_survives_seek),
        TEST_CASE(boosted_extreme_samples_saturate_safely_at_every_admitted_bit_depth),
        TEST_CASE(normalization_composes_with_volume_before_saturation),
        TEST_CASE(converted_files_apply_their_gain_before_sharing_filter_history),
        TEST_CASE(a_live_gain_for_a_prefetched_track_is_preserved_when_seeking_into_it),
        TEST_CASE(live_normalization_targets_the_decoder_while_a_new_output_clock_waits),
        TEST_CASE(mono_plays_on_both_channels),
        TEST_CASE(a_real_low_rate_flac_keeps_every_sample_and_its_exact_duration),
        TEST_CASE(low_rate_seeking_and_end_ranges_use_the_decoded_clock),
        TEST_CASE(adjacent_low_rate_chapters_share_filter_history_and_keep_their_positions),
        TEST_CASE(separate_converted_files_match_a_single_continuous_filter),
        TEST_CASE(every_converted_rate_keeps_history_across_short_queued_files),
        TEST_CASE(different_converted_inputs_at_the_same_output_clock_drain_and_restart_exactly),
        TEST_CASE(wideband_speech_and_native_32_khz_files_share_one_output_clock),
        TEST_CASE(clearing_the_next_file_during_a_filter_drain_rebuilds_the_current_tail),
        TEST_CASE(a_failed_next_open_reloads_evicted_audio_code_before_draining),
        TEST_CASE(unavailable_evicted_audio_code_finishes_with_an_error_without_calling_it),
        TEST_CASE(converted_tails_finish_after_multiple_small_ring_refills),
        TEST_CASE(an_underrun_while_the_final_filter_tail_drains_is_still_counted),
        TEST_CASE(an_unreadable_next_file_preserves_the_converted_current_tail),
        TEST_CASE(low_rate_filter_tail_drains_before_the_next_output_clock_is_requested),
        TEST_CASE(missing_optional_audio_code_only_refuses_tracks_that_need_it),
        TEST_CASE(an_unsupported_rate_is_refused),
        TEST_CASE(only_stereo_conversions_and_conversions_above_32_khz_need_the_full_clock),
        TEST_CASE(rates_up_to_192_khz_play_and_higher_ones_are_refused),
        TEST_CASE(speech_rates_use_the_matching_supported_output_clock),
        TEST_CASE(a_queued_track_at_an_unsupported_rate_is_refused_with_its_rate),
        TEST_CASE(a_missing_file_is_refused),
        TEST_CASE(the_ring_never_runs_ahead_of_the_dma_by_more_than_its_size),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
