#include <stdio.h>
#include <string.h>

#include "check.h"
#include "device/core1_opus.h"
#include "device/opus_track.h"

enum {
    MAX_ERROR = 16,
    SETTLE_FRAMES = 9600
};

static StreamControl control;
static int32_t samples[STREAM_SLOTS][FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK];
static _Alignas(8) uint8_t ring[OPUS_RING_BYTES + sizeof(OpusWorkspace)];
static FlacDecoder unused_decoder;
static Core1Opus decoder;
static Core1Stream consumer;
static Core1Source source;
static OggStream producer;
static AudioSource interface;
static uint8_t bytes[65536];
static int16_t expected[2 * 96000];
static uint32_t file_size;
static uint32_t close_count;
static uint32_t open_count;
static bool file_open;
static bool fail_read;
static bool damage_head;

static bool read_file(void *context, uint32_t offset, void *out, uint32_t count)
{
    (void)context;
    if (!file_open || fail_read || offset > file_size || count > file_size - offset) {
        return false;
    }
    memcpy(out, bytes + offset, count);
    return true;
}

static bool open_file(void *context, const char *path, FlacInput *input)
{
    (void)context;
    char full[512];
    (void)snprintf(full, sizeof full, "%s/opus/%s.opus", ECHO_FIXTURE_DIR, path);
    FILE *file = fopen(full, "rb");
    if (file == NULL) {
        return false;
    }
    file_size = (uint32_t)fread(bytes, 1, sizeof bytes, file);
    CHECK(feof(file));
    CHECK_EQUAL(0, fclose(file));
    if (damage_head) {
        bytes[28 + 18] = 1;
    }
    open_count++;
    file_open = true;
    *input = (FlacInput){read_file, NULL, file_size};
    return true;
}

static void close_file(void *context)
{
    (void)context;
    CHECK(file_open);
    close_count++;
    file_open = false;
}

static void reset(uint32_t budget)
{
    control = (StreamControl){0};
    memset(ring, 0, sizeof ring);
    close_count = 0;
    open_count = 0;
    file_open = false;
    fail_read = false;
    damage_head = false;
    StreamRegion region = {
        .control = &control, .samples = samples, .ring = ring, .ring_bytes = sizeof ring};
    core1_source_init(&source, region, (FlacFileOpener){open_file, close_file, NULL},
                      &unused_decoder, budget);
    region.ring_bytes = OPUS_RING_BYTES;
    core1_stream_init(&consumer, region, core1_opus_decoder(&decoder, region));
    source.ogg = &producer;
    interface = core1_source_interface(&source, CORE1_FORMAT_OPUS);
}

static AudioFrameResult next_frame(AudioFrame *frame)
{
    for (uint32_t step = 0; step < 100000; step++) {
        AudioFrameResult result = interface.next(interface.context, frame);
        if (result != AUDIO_FRAME_WAIT) {
            return result;
        }
        core1_stream_step(&consumer);
    }
    return AUDIO_FRAME_WAIT;
}

static uint32_t load_reference(const char *name)
{
    char path[512];
    (void)snprintf(path, sizeof path, "%s/opus/%s.pcm", ECHO_FIXTURE_DIR, name);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file == NULL) {
        return 0;
    }
    uint32_t count = (uint32_t)fread(expected, sizeof expected[0], 2 * 96000, file);
    CHECK_EQUAL(0, fclose(file));
    return count;
}

typedef struct Playback {
    const char *name;
    const char *reference;
    uint32_t start_ms;
    uint32_t budget;
} Playback;

static void check_frame(const AudioFrame *frame, uint32_t channels, uint32_t position,
                        uint32_t target)
{
    for (uint32_t at = 0; at < frame->samples; at++) {
        for (uint32_t channel = 0; channel < channels; channel++) {
            int32_t actual = channel == 0 ? frame->left[at] : frame->right[at];
            int32_t delta = (actual >> 8) - expected[(position + at) * channels + channel];
            CHECK_EQUAL(0, actual & 255);
            if (position + at >= target + SETTLE_FRAMES || target == 0) {
                CHECK(delta >= -MAX_ERROR && delta <= MAX_ERROR);
            } else {
                CHECK(delta >= -1024 && delta <= 1024);
            }
        }
        if (channels == 1) {
            CHECK_EQUAL(frame->left[at], frame->right[at]);
        }
    }
}

static void play(Playback playback)
{
    reset(playback.budget);
    uint32_t values = load_reference(playback.reference);
    AudioTrackInfo info = {0};
    bool opened = interface.open(interface.context, playback.name, playback.start_ms, &info);
    CHECK(opened);
    if (!opened) {
        return;
    }
    CHECK_EQUAL(AUDIO_CODEC_OPUS, info.codec);
    CHECK_EQUAL(48000, info.stream.sample_rate);
    CHECK_EQUAL(values / info.stream.channels, info.stream.total_samples);
    CHECK(info.audio_bytes > 0 && info.audio_bytes < file_size);
    uint64_t desired = (uint64_t)playback.start_ms * 48;
    uint32_t target = desired < info.stream.total_samples ? (uint32_t)desired
                                                          : (uint32_t)info.stream.total_samples;
    uint32_t position = target;
    AudioFrame frame;
    AudioFrameResult result;
    while ((result = next_frame(&frame)) == AUDIO_FRAME_READY) {
        CHECK_EQUAL(position, frame.first_sample);
        if (position != frame.first_sample) {
            break;
        }
        CHECK(frame.samples > 0 && frame.samples <= FLAC_MAX_BLOCK);
        CHECK_EQUAL(24, frame.bits);
        check_frame(&frame, info.stream.channels, position, target);
        position += frame.samples;
        interface.release(interface.context);
    }
    CHECK_EQUAL(AUDIO_FRAME_END, result);
    CHECK_EQUAL(info.stream.total_samples, position);
    interface.close(interface.context);
    CHECK_EQUAL(1, close_count);
    CHECK_EQUAL(0, control.generation);
    CHECK(!file_open && !source.streaming);
}

static void real_files_play_and_seek_with_bounded_ring_refills(void)
{
    play((Playback){"stereo48", "stereo48", 0, 4096});
    play((Playback){"stereo48", "stereo48", 700, 127});
    play((Playback){"stereo48", "stereo48", 1500, 4096});
    play((Playback){"stereo48", "stereo48", 70, 4096});
    play((Playback){"stereo48", "stereo48", UINT32_MAX, 4096});
    play((Playback){"speech16", "speech16", 0, 7});
    play((Playback){"speech16", "speech16", 500, 31});
    play((Playback){"long120", "long120", 0, 4096});
    play((Playback){"long120", "long120", 300, 4096});
    play((Playback){"gain120", "gain120", 0, 4096});
    play((Playback){"origin48", "stereo48", 0, 4096});
    play((Playback){"origin48", "stereo48", 1000, 511});
}

static void a_seek_starts_decoding_at_least_the_pre_roll_before_the_target(void)
{
    reset(4096);
    AudioTrackInfo info;
    CHECK(interface.open(interface.context, "stereo48", 1000, &info));
    CHECK(producer.seek_offset != 0);
    CHECK_EQUAL(48000, control.first_sample);
    CHECK_EQUAL(48000, source.skip_before);
    OggPage page;
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read((FlacInput){read_file, NULL, file_size},
                                                producer.seek_offset, &page));
    CHECK(page.granule + OPUS_PRE_ROLL <= 48000 + 312);
    interface.close(interface.context);
    reset(4096);
    CHECK(interface.open(interface.context, "stereo48", 60, &info));
    CHECK_EQUAL(0, producer.seek_offset);
    CHECK_EQUAL(0, control.first_sample);
    CHECK_EQUAL(2880, source.skip_before);
    interface.close(interface.context);
}

static void failed_opens_release_the_file_and_preserve_track_info(void)
{
    reset(4096);
    AudioTrackInfo info = {.codec = AUDIO_CODEC_WAV, .stream = {.sample_rate = 77}};
    AudioTrackInfo before = info;
    CHECK(!interface.open(interface.context, "missing", 0, &info));
    CHECK_EQUAL(0, open_count);
    fail_read = true;
    CHECK(!interface.open(interface.context, "stereo48", 0, &info));
    CHECK_EQUAL(1, close_count);
    fail_read = false;
    damage_head = true;
    CHECK(!interface.open(interface.context, "stereo48", 0, &info));
    CHECK_EQUAL(2, close_count);
    CHECK_EQUAL(0, control.generation);
    CHECK(!file_open && !source.streaming);
    CHECK(memcmp(&before, &info, sizeof info) == 0);
    damage_head = false;
    source.ring_capacity = OPUS_RING_BYTES - 1;
    CHECK(!interface.open(interface.context, "stereo48", 0, &info));
    CHECK_EQUAL(3, close_count);
    CHECK(memcmp(&before, &info, sizeof info) == 0);
}

static void failed_reads_during_playback_are_errors(void)
{
    reset(127);
    AudioTrackInfo info;
    CHECK(interface.open(interface.context, "stereo48", 0, &info));
    AudioFrame frame;
    CHECK_EQUAL(AUDIO_FRAME_READY, next_frame(&frame));
    interface.release(interface.context);
    fail_read = true;
    AudioFrameResult result = AUDIO_FRAME_READY;
    for (uint32_t step = 0; step < 100 && result == AUDIO_FRAME_READY; step++) {
        result = next_frame(&frame);
        if (result == AUDIO_FRAME_READY) {
            interface.release(interface.context);
        }
    }
    CHECK_EQUAL(AUDIO_FRAME_ERROR, result);
    CHECK(source.read_failed);
    interface.close(interface.context);
    CHECK(!file_open && !source.streaming);
}

static void reopening_discards_full_slots_from_the_previous_generation(void)
{
    reset(4096);
    AudioTrackInfo info;
    CHECK(interface.open(interface.context, "stereo48", 0, &info));
    AudioFrame frame;
    CHECK_EQUAL(AUDIO_FRAME_READY, next_frame(&frame));
    core1_stream_step(&consumer);
    CHECK_EQUAL(STREAM_SLOT_FULL, control.slots[0].state);
    CHECK_EQUAL(STREAM_SLOT_FULL, control.slots[1].state);
    uint32_t previous = source.generation;
    interface.close(interface.context);
    CHECK(interface.open(interface.context, "speech16", 50, &info));
    CHECK(source.generation != previous);
    CHECK_EQUAL(1, info.stream.channels);
    CHECK_EQUAL(AUDIO_FRAME_READY, next_frame(&frame));
    CHECK_EQUAL(2400, frame.first_sample);
    CHECK_EQUAL(1, decoder.decoder.channels);
    interface.close(interface.context);
    CHECK_EQUAL(2, close_count);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(real_files_play_and_seek_with_bounded_ring_refills),
        TEST_CASE(a_seek_starts_decoding_at_least_the_pre_roll_before_the_target),
        TEST_CASE(failed_opens_release_the_file_and_preserve_track_info),
        TEST_CASE(failed_reads_during_playback_are_errors),
        TEST_CASE(reopening_discards_full_slots_from_the_previous_generation),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
