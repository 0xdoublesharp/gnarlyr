#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/audio_engine.h"
#include "device/core1_heap.h"
#include "device/core1_m4a.h"
#include "device/core1_stream.h"
#include "m4a_fixture_files.h"

#define AAC_ALBUM "m4a_music/Aac Artist/Aac Album/"
#define ALAC_ALBUM "m4a_music/Alac Artist/Alac Album/"

enum {
    FULL_RING_BYTES = 128 * 1024,
    FILL_BUDGET = 4096,
    MAX_ROUNDS = 200000,
    ENGINE_FRAMES = 8192,
    AAC_TOLERANCE = 3 << 8,
    MAX_PCM_FRAMES = 60000,
    CAPTURE_FRAMES = 100000
};

typedef struct Expect {
    const char *path;
    const char *pcm;
    uint8_t channels;
    uint8_t pcm_bits;
    bool lossy;
    uint32_t skip;
    uint32_t frames;
} Expect;

static const Expect LEAD = {AAC_ALBUM "01 Lead.m4a", "m4a_pcm/lead.pcm", 2, 16, true, 0, 0};
static const Expect TAIL = {AAC_ALBUM "02 Tail.m4a", "m4a_pcm/tail.pcm", 2, 16, true, 1024, 44100};
static const Expect MONO = {AAC_ALBUM "03 Mono.m4a", "m4a_pcm/mono.pcm", 1, 16, true, 0, 0};
static const Expect LOSSLESS = {
    ALAC_ALBUM "01 Lossless.m4a", "m4a_pcm/lossless.pcm", 2, 16, false, 0, 0};
static const Expect DEEP = {ALAC_ALBUM "02 Deep.m4a", "m4a_pcm/deep.pcm", 2, 32, false, 0, 0};
static const Expect SINGLE = {ALAC_ALBUM "03 Single.m4a", "m4a_pcm/single.pcm", 1, 16, false, 0, 0};

static StreamControl control;
static int32_t samples[STREAM_SLOTS][FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK];
static uint8_t ring[FULL_RING_BYTES];
_Alignas(AacDecoder) static uint8_t heap[M4A_HEAP_BYTES];
static FlacDecoder unused_decoder;
static Core1M4a core1_m4a;
static Core1Stream stream;
static Core1Source source;
static M4aStream m4a;
static AudioSource interface;
static M4aFile opened;
static int close_count;
static uint32_t engine_ring[2 * ENGINE_FRAMES];
static AudioEngine engine;
static int32_t reference[MAX_PCM_FRAMES][2];
static int32_t captured[CAPTURE_FRAMES][2];

static bool open_file(void *context, const char *path, FlacInput *input)
{
    (void)context;
    m4a_file_free(&opened);
    opened = m4a_file_load(path);
    *input = m4a_file_input(&opened);
    return opened.size > 0;
}

static void close_file(void *context)
{
    (void)context;
    close_count++;
}

static void start_with_budget(uint32_t budget)
{
    memset(&control, 0, sizeof control);
    memset(ring, 0, sizeof ring);
    close_count = 0;
    core1_heap_init(heap, sizeof heap);
    StreamRegion region = {
        .control = &control, .samples = samples, .ring = ring, .ring_bytes = FULL_RING_BYTES};
    core1_source_init(&source, region,
                      (FlacFileOpener){.open = open_file, .close = close_file, .context = NULL},
                      &unused_decoder, budget);
    region.ring_bytes = M4A_RING_BYTES;
    core1_stream_init(&stream, region, core1_m4a_decoder(&core1_m4a));
    source.m4a = &m4a;
    interface = core1_source_interface(&source, CORE1_FORMAT_M4A);
}

static uint32_t load_reference(const Expect *expect, int32_t (*out)[2])
{
    M4aFile pcm = m4a_file_load(expect->pcm);
    uint32_t width = expect->pcm_bits / 8;
    uint32_t count = pcm.size / (width * expect->channels) - expect->skip;
    count = expect->frames != 0 && expect->frames < count ? expect->frames : count;
    for (uint32_t index = 0; index < count && index < MAX_PCM_FRAMES; index++) {
        for (uint32_t channel = 0; channel < 2; channel++) {
            uint32_t at = ((index + expect->skip) * expect->channels +
                           (channel < expect->channels ? channel : 0)) *
                          width;
            int32_t value = 0;
            if (width == 2) {
                value = (int16_t)(pcm.bytes[at] | pcm.bytes[at + 1] << 8);
            } else {
                value = (int32_t)((uint32_t)pcm.bytes[at] | (uint32_t)pcm.bytes[at + 1] << 8 |
                                  (uint32_t)pcm.bytes[at + 2] << 16 |
                                  (uint32_t)pcm.bytes[at + 3] << 24) >>
                        8;
            }
            out[index][channel] = value;
        }
    }
    m4a_file_free(&pcm);
    return count;
}

static AudioFrameResult next_frame(AudioFrame *frame)
{
    for (int round = 0; round < MAX_ROUNDS; round++) {
        AudioFrameResult result = interface.next(interface.context, frame);
        if (result != AUDIO_FRAME_WAIT) {
            return result;
        }
        core1_stream_step(&stream);
    }
    return AUDIO_FRAME_WAIT;
}

static bool close_enough(const Expect *expect, int32_t decoded, int32_t wanted, uint8_t bits)
{
    if (!expect->lossy) {
        return decoded == wanted;
    }
    int32_t scaled = wanted * (1 << (bits - expect->pcm_bits));
    int32_t difference = decoded - scaled;
    return difference >= -AAC_TOLERANCE && difference <= AAC_TOLERANCE;
}

static bool frame_matches(const Expect *expect, const AudioFrame *frame, uint32_t from)
{
    for (uint32_t index = 0; index < frame->samples; index++) {
        uint32_t at = frame->first_sample + index;
        if (at < from) {
            continue;
        }
        if (!close_enough(expect, frame->left[index], reference[at][0], frame->bits) ||
            !close_enough(expect, frame->right[index], reference[at][1], frame->bits)) {
            return false;
        }
    }
    return true;
}

static bool streams_like_reference(const Expect *expect, uint32_t start_ms, uint32_t budget)
{
    start_with_budget(budget);
    uint32_t count = load_reference(expect, reference);
    AudioTrackInfo info;
    if (!interface.open(interface.context, expect->path, start_ms, &info)) {
        return false;
    }
    uint32_t target = (uint32_t)((uint64_t)start_ms * info.stream.sample_rate / 1000);
    bool ok = info.stream.total_samples == count && info.stream.channels == expect->channels;
    uint32_t next = UINT32_MAX;
    uint32_t end = 0;
    AudioFrame frame;
    AudioFrameResult result;
    while (ok && (result = next_frame(&frame)) == AUDIO_FRAME_READY) {
        ok = (next == UINT32_MAX || frame.first_sample == next) && frame.samples > 0 &&
             frame.first_sample + frame.samples <= count && frame_matches(expect, &frame, target);
        next = frame.first_sample + frame.samples;
        end = next;
        interface.release(interface.context);
    }
    ok = ok && result == AUDIO_FRAME_END && end == count;
    interface.close(interface.context);
    return ok;
}

static void an_aac_file_streams_through_core_1_like_the_reference_decoder(void)
{
    CHECK(streams_like_reference(&LEAD, 0, FILL_BUDGET));
    CHECK(streams_like_reference(&TAIL, 0, FILL_BUDGET));
    CHECK(streams_like_reference(&MONO, 0, FILL_BUDGET));
}

static void an_alac_file_streams_through_core_1_bit_exactly(void)
{
    CHECK(streams_like_reference(&LOSSLESS, 0, FILL_BUDGET));
    CHECK(streams_like_reference(&DEEP, 0, FILL_BUDGET));
    CHECK(streams_like_reference(&SINGLE, 0, FILL_BUDGET));
}

static void a_slow_feed_starves_core_1_without_losing_samples(void)
{
    CHECK(streams_like_reference(&LEAD, 0, 200));
    CHECK(streams_like_reference(&LOSSLESS, 0, 333));
}

static void seeking_starts_decoding_before_the_target_so_the_target_is_exact(void)
{
    CHECK(streams_like_reference(&LEAD, 500, FILL_BUDGET));
    CHECK(streams_like_reference(&TAIL, 123, FILL_BUDGET));
    CHECK(streams_like_reference(&MONO, 900, FILL_BUDGET));
    CHECK(streams_like_reference(&LOSSLESS, 500, FILL_BUDGET));
    CHECK(streams_like_reference(&DEEP, 777, FILL_BUDGET));
}

static void seeking_past_the_end_plays_the_last_samples_only(void)
{
    CHECK(streams_like_reference(&LEAD, 60000, FILL_BUDGET));
    CHECK(streams_like_reference(&LOSSLESS, 60000, FILL_BUDGET));
}

static void opening_reports_the_codec_and_length(void)
{
    start_with_budget(FILL_BUDGET);
    AudioTrackInfo info;
    CHECK(interface.open(interface.context, LEAD.path, 0, &info));
    CHECK_EQUAL(AUDIO_CODEC_AAC, info.codec);
    CHECK_EQUAL(44100u, info.stream.sample_rate);
    CHECK_EQUAL(0, info.stream.bits_per_sample);
    CHECK_EQUAL(42312u, info.stream.total_samples);
    CHECK(info.audio_bytes > 10000);
    interface.close(interface.context);
    CHECK_EQUAL(0u, control.generation);
    CHECK_EQUAL(1, close_count);
    CHECK(interface.open(interface.context, DEEP.path, 0, &info));
    CHECK_EQUAL(AUDIO_CODEC_ALAC, info.codec);
    CHECK_EQUAL(24, info.stream.bits_per_sample);
    interface.close(interface.context);
}

static void files_that_are_not_mp4_do_not_start_core_1(void)
{
    start_with_budget(FILL_BUDGET);
    AudioTrackInfo info;
    CHECK(!interface.open(interface.context, "m4a_music.db", 0, &info));
    CHECK_EQUAL(0u, control.generation);
    CHECK(!interface.open(interface.context, "missing.m4a", 0, &info));
    CHECK_EQUAL(0u, control.generation);
}

static uint32_t drain_into(int32_t (*out)[2], uint32_t at)
{
    uint32_t produced = engine.ended ? engine.end_at : engine.produced;
    for (uint32_t frame = at; frame < produced; frame++) {
        uint32_t slot = 2 * (frame % ENGINE_FRAMES);
        out[frame][0] = (int32_t)engine_ring[slot] >> 8;
        out[frame][1] = (int32_t)engine_ring[slot + 1] >> 8;
    }
    return produced;
}

static uint32_t play_to_end(const char *path, const char *next_path, uint32_t start_ms)
{
    start_with_budget(FILL_BUDGET);
    audio_engine_init(&engine,
                      (AudioEngineConfig){.source = interface,
                                          .ring = engine_ring,
                                          .ring_words = (uint32_t)(sizeof engine_ring / 4)});
    AudioEngineTrack track = {.path = path, .start_ms = 0, .end_ms = 0, .token = 1};
    if (!audio_engine_play(&engine, &track, start_ms)) {
        return 0;
    }
    if (next_path != NULL) {
        AudioEngineTrack next = {.path = next_path, .start_ms = 0, .end_ms = 0, .token = 2};
        audio_engine_queue_next(&engine, &next);
    }
    uint32_t taken = 0;
    for (int round = 0; round < MAX_ROUNDS && engine.state != AUDIO_ENGINE_STOPPED; round++) {
        core1_stream_step(&stream);
        audio_engine_pump(&engine, taken);
        taken = drain_into(captured, taken);
    }
    return taken;
}

static void the_engine_plays_an_aac_file_to_its_exact_gapless_length(void)
{
    uint32_t count = load_reference(&LEAD, reference);
    uint32_t played = play_to_end(LEAD.path, NULL, 0);
    CHECK_EQUAL(count, played);
    bool ok = true;
    for (uint32_t index = 0; ok && index < count && index < played; index++) {
        ok = close_enough(&LEAD, captured[index][0], reference[index][0], 24) &&
             close_enough(&LEAD, captured[index][1], reference[index][1], 24);
    }
    CHECK(ok);
}

static void the_engine_resumes_an_alac_file_from_a_position(void)
{
    uint32_t count = load_reference(&LOSSLESS, reference);
    uint32_t played = play_to_end(LOSSLESS.path, NULL, 500);
    uint32_t skipped = 500 * 44100 / 1000;
    CHECK_EQUAL(count - skipped, played);
    bool ok = true;
    for (uint32_t index = 0; ok && index < played; index++) {
        ok = captured[index][0] >> 8 == reference[skipped + index][0];
    }
    CHECK(ok);
}

static void consecutive_m4a_tracks_play_without_a_gap(void)
{
    uint32_t first = load_reference(&LEAD, reference);
    static int32_t other[MAX_PCM_FRAMES][2];
    uint32_t second = load_reference(&TAIL, other);
    uint32_t played = play_to_end(LEAD.path, TAIL.path, 0);
    CHECK_EQUAL(first + second, played);
    bool ok = true;
    for (uint32_t index = 0; ok && index < second && first + index < played; index++) {
        ok = close_enough(&TAIL, captured[first + index][0], other[index][0], 24);
    }
    CHECK(ok);
}

static void truncated_virtual_records_fail_instead_of_waiting_forever(void)
{
    uint8_t bytes[] = {4, 0, 0, 0, 1, 2};
    for (uint32_t length = 0; length <= sizeof bytes; length++) {
        M4aFile file = {.bytes = bytes, .size = length};
        Core1M4a context;
        Core1Decoder decoder = core1_m4a_decoder(&context);
        core1_heap_init(heap, sizeof heap);
        decoder.start(decoder.context, m4a_file_input(&file), (FlacInfo){0}, 0, NULL);
        Core1Block block = {0};
        CHECK_EQUAL(CORE1_FAILED, decoder.decode(decoder.context, &block));
    }
}

static void the_aac_decoder_rejects_a_truncated_core_coder_dependency(void)
{
    static AacDecoder decoder;
    static const uint8_t incomplete[] = {0x12, 0x12};
    core1_heap_init(heap, sizeof heap);
    CHECK(!aac_decoder_start(&decoder, incomplete, sizeof incomplete));
}

static void the_aac_decoder_rejects_short_frames(void)
{
    static AacDecoder decoder;
    static const uint8_t standard[] = {0x12, 0x10};
    static const uint8_t short_frame[] = {0x12, 0x14};
    core1_heap_init(heap, sizeof heap);
    CHECK(aac_decoder_start(&decoder, standard, sizeof standard));
    core1_heap_init(heap, sizeof heap);
    CHECK(!aac_decoder_start(&decoder, short_frame, sizeof short_frame));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(truncated_virtual_records_fail_instead_of_waiting_forever),
        TEST_CASE(the_aac_decoder_rejects_a_truncated_core_coder_dependency),
        TEST_CASE(the_aac_decoder_rejects_short_frames),
        TEST_CASE(an_aac_file_streams_through_core_1_like_the_reference_decoder),
        TEST_CASE(an_alac_file_streams_through_core_1_bit_exactly),
        TEST_CASE(a_slow_feed_starves_core_1_without_losing_samples),
        TEST_CASE(seeking_starts_decoding_before_the_target_so_the_target_is_exact),
        TEST_CASE(seeking_past_the_end_plays_the_last_samples_only),
        TEST_CASE(opening_reports_the_codec_and_length),
        TEST_CASE(files_that_are_not_mp4_do_not_start_core_1),
        TEST_CASE(the_engine_plays_an_aac_file_to_its_exact_gapless_length),
        TEST_CASE(the_engine_resumes_an_alac_file_from_a_position),
        TEST_CASE(consecutive_m4a_tracks_play_without_a_gap),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
