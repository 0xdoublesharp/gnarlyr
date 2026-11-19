#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/bytes.h"
#include "device/core1_vorbis.h"

static StreamControl control;
static int32_t samples[STREAM_SLOTS][FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK];
static _Alignas(8) uint8_t ring[VORBIS_RING_BYTES + VORBIS_WORKSPACE_BYTES];
static FlacDecoder unused_decoder;
static Core1Vorbis decoder;
static Core1Stream consumer;
static Core1Source source;
static OggStream producer;
static AudioSource interface;
static uint8_t bytes[65536];
static uint32_t file_size;
static uint32_t close_count;
static uint32_t open_count;
static bool file_open;
static bool fail_read;
static bool timeout_wait;
static bool stale_generation;
static bool fail_after_config;
static uint32_t origin_shift;
static uint64_t shift_above;
static uint32_t wait_calls;

static bool read_file(void *context, uint32_t offset, void *out, uint32_t count)
{
    (void)context;
    if (!file_open || fail_read || offset > file_size || count > file_size - offset) {
        return false;
    }
    memcpy(out, bytes + offset, count);
    return true;
}

static void shift_granules(void)
{
    OggPage page;
    uint32_t offset = 0;
    while (offset < file_size) {
        CHECK_EQUAL(OGG_PACKET_READY,
                    ogg_page_read((FlacInput){read_file, NULL, file_size}, offset, &page));
        if (page.granule > shift_above && page.granule != UINT64_MAX) {
            uint64_t granule = page.granule + origin_shift;
            bytes_put_le32(bytes + offset + 6, (uint32_t)granule);
            bytes_put_le32(bytes + offset + 10, (uint32_t)(granule >> 32));
            bytes_put_le32(bytes + offset + 22, 0);
            uint32_t crc = 0;
            for (uint32_t index = offset; index < page.next; index++) {
                crc ^= (uint32_t)bytes[index] << 24;
                for (uint32_t bit = 0; bit < 8; bit++) {
                    crc = (crc << 1) ^ ((crc & 0x80000000u) != 0 ? 0x04C11DB7u : 0);
                }
            }
            bytes_put_le32(bytes + offset + 22, crc);
        }
        offset = page.next;
    }
}

static bool open_file(void *context, const char *path, FlacInput *input)
{
    (void)context;
    char full[512];
    (void)snprintf(full, sizeof full, "%s/vorbis/%s.ogg", ECHO_FIXTURE_DIR, path);
    FILE *file = fopen(full, "rb");
    if (file == NULL) {
        return false;
    }
    file_size = (uint32_t)fread(bytes, 1, sizeof bytes, file);
    CHECK(feof(file));
    CHECK_EQUAL(0, fclose(file));
    open_count++;
    file_open = true;
    if (origin_shift != 0) {
        shift_granules();
    }
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

static bool wait_config(Core1Source *waiting)
{
    wait_calls++;
    if (timeout_wait) {
        return false;
    }
    for (uint32_t step = 0; step < 100000; step++) {
        core1_source_fill(waiting);
        core1_stream_step(&consumer);
        if (waiting->read_failed) {
            return false;
        }
        if (control.vorbis_config_generation == waiting->generation) {
            CHECK_EQUAL(producer.position, waiting->written);
            CHECK_EQUAL(3, producer.headers);
            CHECK_EQUAL(0, control.slots[0].state);
            CHECK(!decoder.audio_started);
            if (stale_generation) {
                control.vorbis_config_generation++;
            }
            fail_read = fail_after_config;
            return true;
        }
        if (consumer.finished) {
            return false;
        }
    }
    return false;
}

static void reset(uint32_t budget)
{
    control = (StreamControl){0};
    memset(ring, 0, sizeof ring);
    close_count = 0;
    open_count = 0;
    file_open = false;
    fail_read = false;
    timeout_wait = false;
    stale_generation = false;
    fail_after_config = false;
    origin_shift = 0;
    shift_above = 0;
    wait_calls = 0;
    StreamRegion region = {
        .control = &control, .samples = samples, .ring = ring, .ring_bytes = sizeof ring};
    core1_source_init(&source, region, (FlacFileOpener){open_file, close_file, NULL},
                      &unused_decoder, budget);
    region.ring_bytes = VORBIS_RING_BYTES;
    core1_stream_init(&consumer, region, core1_vorbis_decoder(&decoder, region));
    source.ogg = &producer;
    source.wait_vorbis = wait_config;
    interface = core1_source_interface(&source, CORE1_FORMAT_VORBIS);
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

static void compare_file(const char *name, uint32_t start_ms, uint32_t budget, uint32_t shift)
{
    reset(budget);
    origin_shift = shift;
    AudioTrackInfo info = {0};
    bool opened = interface.open(interface.context, name, start_ms, &info);
    CHECK(opened);
    if (!opened) {
        return;
    }
    CHECK_EQUAL(AUDIO_CODEC_VORBIS, info.codec);
    CHECK_EQUAL(1, wait_calls);
    CHECK_EQUAL(source.generation, control.vorbis_start_generation);
    CHECK_EQUAL(0, info.stream.bits_per_sample);
    char path[512];
    (void)snprintf(path, sizeof path, "%s/vorbis/%s.pcm", ECHO_FIXTURE_DIR, name);
    FILE *reference = fopen(path, "rb");
    CHECK(reference != NULL);
    if (reference == NULL) {
        interface.close(interface.context);
        return;
    }
    uint64_t desired = (uint64_t)start_ms * info.stream.sample_rate / 1000;
    uint32_t target = desired < info.stream.total_samples ? (uint32_t)desired
                                                          : (uint32_t)info.stream.total_samples;
    CHECK_EQUAL(0, fseek(reference, (long)(target * info.stream.channels * 2), SEEK_SET));
    uint32_t position = target;
    AudioFrame frame;
    AudioFrameResult result;
    while ((result = next_frame(&frame)) == AUDIO_FRAME_READY) {
        CHECK_EQUAL(position, frame.first_sample);
        if (position != frame.first_sample) {
            break;
        }
        CHECK(frame.samples > 0);
        CHECK_EQUAL(24, frame.bits);
        for (uint32_t at = 0; at < frame.samples; at++) {
            for (uint32_t channel = 0; channel < info.stream.channels; channel++) {
                int low = fgetc(reference);
                int high = fgetc(reference);
                CHECK(low != EOF && high != EOF);
                int16_t expected = (int16_t)((uint32_t)low | (uint32_t)high << 8);
                int32_t actual = channel == 0 ? frame.left[at] : frame.right[at];
                int32_t delta = (actual >> 8) - expected;
                CHECK(delta >= -2 && delta <= 2);
            }
            if (info.stream.channels == 1) {
                CHECK_EQUAL(frame.left[at], frame.right[at]);
            }
        }
        position += frame.samples;
        interface.release(interface.context);
    }
    CHECK_EQUAL(AUDIO_FRAME_END, result);
    CHECK_EQUAL(info.stream.total_samples, position);
    CHECK_EQUAL(EOF, fgetc(reference));
    CHECK_EQUAL(0, fclose(reference));
    interface.close(interface.context);
    CHECK_EQUAL(1, close_count);
    CHECK_EQUAL(0, control.generation);
    CHECK(!file_open && !source.streaming);
}

static void real_files_stream_and_resume_with_bounded_ring_refills(void)
{
    compare_file("stereo44", 0, 4096, 0);
    compare_file("seek44", 1400, 127, 0);
    compare_file("seek44", 3000, 4096, 0);
    compare_file("mono8", 100, 7, 0);
    compare_file("seek8", 2400, 31, 0);
    compare_file("cropped44", 2000, 4096, 0);
    compare_file("seek44", UINT32_MAX, 4096, 0);
    compare_file("seek44", 0, 127, 500);
    compare_file("seek44", 1400, 4096, 500);
    compare_file("seek44", UINT32_MAX, 4096, 500);
}

enum {
    CORRECTION = 1000,
    CORRECTED_AFTER = 60000,
    COLLECTED = 160000,
    CUTOFF_BEFORE_CORRECTION = 1300,
    PROMPT_FRAMES = 4
};

typedef struct Collected {
    uint32_t positions[COLLECTED];
    int32_t left[COLLECTED];
    uint32_t count;
    uint32_t largest;
} Collected;

static const uint32_t LAGS[] = {5, 7, 9, 11, 13, 17, 24};

static Collected released_at_once;
static Collected lagging;

static void lag(uint32_t steps)
{
    for (uint32_t step = 0; step < steps; step++) {
        core1_source_fill(&source);
        core1_stream_step(&consumer);
    }
}

static AudioFrameResult lagging_frame(uint32_t steps, AudioFrame *frame)
{
    if (steps == 0) {
        return next_frame(frame);
    }
    for (uint32_t round = 0; round < 100000; round++) {
        lag(steps);
        AudioFrameResult result = interface.next(interface.context, frame);
        if (result != AUDIO_FRAME_WAIT) {
            return result;
        }
    }
    return AUDIO_FRAME_WAIT;
}

static void collect_corrected(uint32_t cutoff, uint32_t steps, uint32_t prompt, Collected *out)
{
    reset(4096);
    origin_shift = CORRECTION;
    shift_above = CORRECTED_AFTER;
    *out = (Collected){0};
    AudioTrackInfo info = {0};
    CHECK(interface.open(interface.context, "seek44", 0, &info));
    source.skip_before = cutoff;
    AudioFrame frame;
    while (lagging_frame(prompt == 0 ? steps : 0, &frame) == AUDIO_FRAME_READY &&
           out->count + frame.samples <= COLLECTED) {
        prompt -= prompt != 0;
        for (uint32_t at = 0; at < frame.samples; at++) {
            out->positions[out->count] = frame.first_sample + at;
            out->left[out->count] = frame.left[at];
            out->count++;
        }
        out->largest = frame.samples > out->largest ? frame.samples : out->largest;
        interface.release(interface.context);
    }
    interface.close(interface.context);
}

static uint32_t correction_index(const Collected *run)
{
    for (uint32_t index = 1; index < run->count; index++) {
        if (run->positions[index] != run->positions[index - 1] + 1) {
            return index;
        }
    }
    return 0;
}

static void check_same_positions(void)
{
    CHECK_EQUAL(released_at_once.count, lagging.count);
    CHECK(lagging.largest > 1024);
    uint32_t count =
        lagging.count < released_at_once.count ? lagging.count : released_at_once.count;
    uint32_t mismatches = 0;
    for (uint32_t index = 0; index < count; index++) {
        mismatches += lagging.positions[index] != released_at_once.positions[index] ||
                      lagging.left[index] != released_at_once.left[index];
    }
    CHECK_EQUAL(0u, mismatches);
}

static void a_granule_correction_keeps_every_gathered_sample_at_its_position(void)
{
    collect_corrected(0, 0, 0, &released_at_once);
    uint32_t index = correction_index(&released_at_once);
    CHECK(index > 0);
    CHECK_EQUAL(released_at_once.positions[index - 1] + 1 + CORRECTION,
                released_at_once.positions[index]);
    for (uint32_t prompt = 0; prompt < PROMPT_FRAMES; prompt++) {
        collect_corrected(0, LAGS[sizeof LAGS / sizeof LAGS[0] - 1], prompt, &lagging);
        check_same_positions();
    }
}

static void a_cutoff_just_before_a_granule_correction_admits_no_earlier_sound(void)
{
    collect_corrected(0, 0, 0, &released_at_once);
    uint32_t index = correction_index(&released_at_once);
    CHECK(index > CUTOFF_BEFORE_CORRECTION);
    uint32_t cutoff = released_at_once.positions[index - CUTOFF_BEFORE_CORRECTION];
    collect_corrected(cutoff, 0, 0, &released_at_once);
    CHECK_EQUAL(cutoff, released_at_once.positions[0]);
    for (uint32_t lags = 0; lags < sizeof LAGS / sizeof LAGS[0]; lags++) {
        collect_corrected(cutoff, LAGS[lags], 0, &lagging);
        CHECK_EQUAL(cutoff, lagging.positions[0]);
        check_same_positions();
    }
}

static void failed_open_setup_and_timeout_release_the_file_and_preserve_track_info(void)
{
    reset(4096);
    AudioTrackInfo info = {.codec = AUDIO_CODEC_WAV, .stream = {.sample_rate = 77}};
    AudioTrackInfo before = info;
    CHECK(!interface.open(interface.context, "missing", 0, &info));
    CHECK_EQUAL(0, close_count);
    CHECK_EQUAL(0, open_count);
    CHECK(memcmp(&before, &info, sizeof info) == 0);
    fail_read = true;
    CHECK(!interface.open(interface.context, "seek44", 0, &info));
    CHECK_EQUAL(1, close_count);
    CHECK(!file_open && !source.streaming);
    CHECK(memcmp(&before, &info, sizeof info) == 0);
    fail_read = false;
    timeout_wait = true;
    CHECK(!interface.open(interface.context, "seek44", 0, &info));
    CHECK_EQUAL(2, close_count);
    CHECK_EQUAL(0, control.generation);
    CHECK(!file_open && !source.streaming);
    CHECK(memcmp(&before, &info, sizeof info) == 0);
    timeout_wait = false;
    stale_generation = true;
    CHECK(!interface.open(interface.context, "seek44", 0, &info));
    CHECK_EQUAL(3, close_count);
    CHECK_EQUAL(0, control.generation);
    CHECK(memcmp(&before, &info, sizeof info) == 0);
}

static void failed_reads_after_configuration_and_during_playback_are_errors(void)
{
    reset(4096);
    AudioTrackInfo info = {.codec = AUDIO_CODEC_WAV, .stream = {.sample_rate = 77}};
    AudioTrackInfo before = info;
    fail_after_config = true;
    CHECK(!interface.open(interface.context, "seek44", 1000, &info));
    CHECK_EQUAL(1, close_count);
    CHECK_EQUAL(1, wait_calls);
    CHECK_EQUAL(0, control.generation);
    CHECK(memcmp(&before, &info, sizeof info) == 0);
    reset(127);
    CHECK(interface.open(interface.context, "seek44", 0, &info));
    fail_read = true;
    AudioFrame frame;
    CHECK_EQUAL(AUDIO_FRAME_ERROR, next_frame(&frame));
    CHECK(source.read_failed);
    CHECK_EQUAL(AUDIO_FRAME_ERROR, next_frame(&frame));
    interface.close(interface.context);
    CHECK_EQUAL(1, close_count);
    CHECK(!file_open && !source.streaming);
}

static void reopening_discards_full_pcm_slots_from_the_previous_generation(void)
{
    reset(4096);
    AudioTrackInfo info = {0};
    CHECK(interface.open(interface.context, "seek44", 0, &info));
    AudioFrame frame;
    CHECK_EQUAL(AUDIO_FRAME_READY, next_frame(&frame));
    for (int step = 0; step < 64 && control.slots[1].state != STREAM_SLOT_FULL; step++) {
        core1_stream_step(&consumer);
    }
    CHECK_EQUAL(STREAM_SLOT_FULL, control.slots[0].state);
    CHECK_EQUAL(STREAM_SLOT_FULL, control.slots[1].state);
    uint32_t previous = source.generation;
    interface.close(interface.context);
    bool opened = interface.open(interface.context, "mono8", 50, &info);
    CHECK(opened);
    if (!opened) {
        return;
    }
    CHECK(source.generation != previous);
    CHECK_EQUAL(source.generation, control.vorbis_config_generation);
    CHECK_EQUAL(AUDIO_FRAME_READY, next_frame(&frame));
    CHECK_EQUAL(400, frame.first_sample);
    CHECK_EQUAL(8000, info.stream.sample_rate);
    interface.close(interface.context);
    CHECK_EQUAL(2, close_count);
}

static void a_closed_source_can_reopen_without_reusing_its_old_configuration(void)
{
    reset(4096);
    AudioTrackInfo info = {0};
    CHECK(interface.open(interface.context, "stereo44", 0, &info));
    uint32_t first = source.generation;
    interface.close(interface.context);
    CHECK(interface.open(interface.context, "mono8", 50, &info));
    CHECK(source.generation != first);
    CHECK_EQUAL(source.generation, control.vorbis_config_generation);
    CHECK_EQUAL(8000, info.stream.sample_rate);
    CHECK_EQUAL(1, info.stream.channels);
    interface.close(interface.context);
    CHECK_EQUAL(2, close_count);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(real_files_stream_and_resume_with_bounded_ring_refills),
        TEST_CASE(a_granule_correction_keeps_every_gathered_sample_at_its_position),
        TEST_CASE(a_cutoff_just_before_a_granule_correction_admits_no_earlier_sound),
        TEST_CASE(failed_open_setup_and_timeout_release_the_file_and_preserve_track_info),
        TEST_CASE(failed_reads_after_configuration_and_during_playback_are_errors),
        TEST_CASE(reopening_discards_full_pcm_slots_from_the_previous_generation),
        TEST_CASE(a_closed_source_can_reopen_without_reusing_its_old_configuration),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
