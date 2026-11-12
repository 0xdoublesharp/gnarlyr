#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/dsd_stream.h"

static uint8_t bytes[524288];
static uint32_t length;
static bool fail_reads;
static uint32_t closes;

static void number(uint32_t offset, uint64_t value, uint32_t count, bool little)
{
    for (uint32_t index = 0; index < count; index++) {
        bytes[offset + index] = (uint8_t)(value >> (8 * (little ? index : count - 1 - index)));
    }
}

static bool read_file(void *context, uint32_t offset, void *out, uint32_t count)
{
    (void)context;
    if (fail_reads || offset > length || count > length - offset) {
        return false;
    }
    memcpy(out, bytes + offset, count);
    return true;
}

static FlacInput input(void)
{
    return (FlacInput){read_file, NULL, length};
}

static void dsf(uint32_t rate, uint32_t channels, uint64_t samples)
{
    memset(bytes, 0, sizeof bytes);
    uint32_t blocks = (uint32_t)((samples + 32767) / 32768);
    length = 92 + blocks * 4096 * channels;
    memcpy(bytes, "DSD ", 4);
    number(4, 28, 8, true);
    number(12, length, 8, true);
    memcpy(bytes + 28, "fmt ", 4);
    number(32, 52, 8, true);
    number(40, 1, 4, true);
    number(48, channels, 4, true);
    number(52, channels, 4, true);
    number(56, rate, 4, true);
    number(60, 1, 4, true);
    number(64, samples, 8, true);
    number(72, 4096, 4, true);
    memcpy(bytes + 80, "data", 4);
    number(84, length - 80, 8, true);
    memset(bytes + 92, 0x69, length - 92);
}

static void dsf_formats_and_padded_endpoints_are_probed(void)
{
    for (uint32_t rate = 2822400; rate <= 11289600; rate *= 2) {
        for (uint32_t channels = 1; channels <= 2; channels++) {
            dsf(rate, channels, 32771);
            DsdTrack track;
            CHECK(dsd_probe(input(), &track));
            CHECK_EQUAL(rate, track.rate);
            CHECK_EQUAL(channels, track.channels);
            CHECK_EQUAL(32771, track.samples);
            CHECK_EQUAL(92, track.data_offset);
            CHECK_EQUAL(length - 92, track.data_bytes);
        }
    }
}

static void damaged_dsf_is_refused(void)
{
    DsdTrack track;
    dsf(2822400, 2, 32771);
    length--;
    CHECK(!dsd_probe(input(), &track));
    dsf(2822400, 2, 32771);
    number(44, 1, 4, true);
    CHECK(!dsd_probe(input(), &track));
    dsf(2822400, 2, 32771);
    number(72, 4095, 4, true);
    CHECK(!dsd_probe(input(), &track));
    dsf(2822400, 2, 32771);
    number(64, UINT64_MAX, 8, true);
    CHECK(!dsd_probe(input(), &track));
    dsf(2822400, 2, 32771);
    number(56, 22579200, 4, true);
    CHECK(!dsd_probe(input(), &track));
}

static StreamControl control;
static int32_t output[2][2][FLAC_MAX_BLOCK];
static _Alignas(8) uint8_t ring[DSD_RING_BYTES + sizeof(DsdWorkspace)];
static Core1Dsd decoder;
static uint8_t temporal[2][8192];
static double first_coefficients[512];
static double second_coefficients[DSD_SECOND_TAPS];
static double stage[2][10000];

static double bessel(double value)
{
    double result = 1;
    double term = 1;
    for (uint32_t index = 1; index <= 50; index++) {
        term *= value * value / (4 * index * index);
        result += term;
    }
    return result;
}

static void make_signal(uint32_t rate, uint32_t kind, uint32_t samples)
{
    memset(temporal, kind == 3 ? 0x69 : kind == 4 ? 0xff : 0, sizeof temporal);
    if (kind >= 3) {
        return;
    }
    for (uint32_t channel = 0; channel < 2; channel++) {
        double error = 0;
        for (uint32_t index = 0; index < samples; index++) {
            double time = (double)index / rate;
            double phase = kind == 1 ? 2 * 3.141592653589793 * (500 * time + 400000 * time * time)
                                     : 2 * 3.141592653589793 * (1000 + 321 * channel) * time;
            double wanted = kind == 2 ? 0 : 0.35 * sin(phase);
            error += wanted;
            bool bit = error >= 0;
            error -= bit ? 1 : -1;
            temporal[channel][index / 8] |= (uint8_t)((bit ? 1u : 0u) << (index % 8));
        }
    }
}

static void reference_compensation(uint32_t ratio)
{
    double total = 0;
    uint32_t step = 16 * ratio;
    for (uint32_t tap = 0; tap < DSD_SECOND_TAPS; tap++) {
        double distance = (double)tap - 31.5;
        double integral = 0;
        for (uint32_t index = 0; index <= 2048; index++) {
            double f = (double)index * 0.17 / 2048;
            double cic =
                index == 0
                    ? 1
                    : pow(sin(3.141592653589793 * f) / (step * sin(3.141592653589793 * f / step)),
                          5);
            integral += (index == 0 || index == 2048 ? 0.5 : 1) *
                        cos(2 * 3.141592653589793 * f * distance) / cic;
        }
        double window =
            bessel(8.6 * sqrt(fmax(0, 1 - distance * distance / (31.5 * 31.5)))) / bessel(8.6);
        second_coefficients[tap] = integral * 0.34 / 2048 * window;
        total += second_coefficients[tap];
    }
    for (uint32_t tap = 0; tap < DSD_SECOND_TAPS; tap++) {
        second_coefficients[tap] /= total;
    }
}

static void reference_stages(uint32_t ratio, uint32_t samples, uint32_t frames)
{
    uint32_t step = 16 * ratio;
    uint32_t taps = 1;
    first_coefficients[0] = 1;
    for (uint32_t order = 0; order < 5; order++) {
        double row[512] = {0};
        for (uint32_t tap = 0; tap < taps; tap++) {
            for (uint32_t index = 0; index < step; index++) {
                row[tap + index] += first_coefficients[tap] / step;
            }
        }
        taps += step - 1;
        memcpy(first_coefficients, row, taps * sizeof(double));
    }
    reference_compensation(ratio);
    for (uint32_t channel = 0; channel < 2; channel++) {
        for (uint32_t at = 0; at < frames * 2 + 34; at++) {
            int64_t end = ((int64_t)at + 1) * step - 1;
            double sum = 0;
            for (uint32_t tap = 0; tap < taps; tap++) {
                int64_t bit = end - tap;
                if (bit >= 0 && bit < samples) {
                    int value = (temporal[channel][(uint32_t)bit / 8] >> ((uint32_t)bit % 8)) & 1;
                    sum += (value ? 1 : -1) * first_coefficients[tap];
                }
            }
            stage[channel][at] = sum;
        }
    }
}

static int32_t reference(uint32_t frame, uint32_t channel)
{
    double sum = 0;
    for (uint32_t tap = 0; tap < DSD_SECOND_TAPS; tap++) {
        int32_t at = (int32_t)(33 + frame * 2) - (int32_t)tap;
        if (at >= 0) {
            sum += second_coefficients[tap] * stage[channel][at];
        }
    }
    double value = floor(sum * 16777216.0 + 0.5);
    return value < -8388608 ? -8388608 : value > 8388607 ? 8388607 : (int32_t)value;
}

static void make_virtual(uint32_t rate, uint32_t samples)
{
    dsf(rate, 2, samples);
    uint32_t data_bytes = length - 92;
    for (uint32_t index = 0; index < (samples + 7) / 8; index++) {
        for (uint32_t channel = 0; channel < 2; channel++) {
            uint32_t offset = 92 + (index / 4096) * 8192 + channel * 4096 + index % 4096;
            bytes[offset] = temporal[channel][index];
        }
    }
    memmove(bytes + 32, bytes + 92, data_bytes);
    memset(bytes, 0, 32);
    memcpy(bytes, "DSDP", 4);
    number(4, rate, 4, true);
    number(8, 2, 4, true);
    number(12, 1, 4, true);
    number(16, samples, 8, true);
    number(28, 4096, 4, true);
    length = 32 + data_bytes;
}

static void dsd_matches_a_double_precision_reference_on_sine_sweep_and_silence(void)
{
    for (uint32_t ratio = 1; ratio <= 4; ratio *= 2) {
        uint32_t rate = 2822400 * ratio;
        uint32_t count = 64003;
        uint32_t frames = (count + 32 * ratio - 1) / (32 * ratio);
        for (uint32_t kind = 0; kind < 6; kind++) {
            make_signal(rate, kind, count);
            reference_stages(ratio, count, frames);
            make_virtual(rate, count);
            control = (StreamControl){.generation = 1};
            StreamRegion region = {
                .control = &control, .samples = output, .ring = ring, .ring_bytes = DSD_RING_BYTES};
            Core1Decoder core = core1_dsd_decoder(&decoder, region);
            core.start(core.context, input(), (FlacInfo){0}, 0, NULL);
            uint32_t done = 0;
            Core1Block block;
            Core1DecodeResult result;
            while ((result = core.decode(core.context, &block)) == CORE1_DECODED) {
                CHECK_EQUAL(done, block.first_sample);
                CHECK(block.samples > 0 && block.samples <= DSD_OUTPUT_FRAMES);
                for (uint32_t at = 0; at < block.samples; at++) {
                    for (uint32_t channel = 0; channel < 2; channel++) {
                        int32_t delta = block.channels[channel][at] - reference(done + at, channel);
                        if (abs(delta) > 32) {
                            CHECK_EQUAL(reference(done + at, channel), block.channels[channel][at]);
                            return;
                        }
                    }
                }
                done += block.samples;
            }
            CHECK_EQUAL(CORE1_FINISHED, result);
            CHECK_EQUAL(frames, done);
        }
    }
}

static uint32_t dff_chunk(uint32_t at, const char *tag, uint32_t size)
{
    memcpy(bytes + at, tag, 4);
    number(at + 4, size, 8, false);
    return at + 12;
}

static uint32_t dff(uint32_t rate, uint32_t channels, uint32_t samples)
{
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, "FRM8", 4);
    memcpy(bytes + 12, "DSD ", 4);
    uint32_t prop_size = 52 + 4 * channels;
    uint32_t prop = dff_chunk(16, "PROP", prop_size);
    memcpy(bytes + prop, "SND ", 4);
    uint32_t at = dff_chunk(prop + 4, "FS  ", 4);
    number(at, rate, 4, false);
    at = dff_chunk(at + 4, "CHNL", 2 + channels * 4);
    number(at, channels, 2, false);
    memcpy(bytes + at + 2, "SLFTSRGT", channels * 4);
    at = dff_chunk(at + 2 + channels * 4, "CMPR", 6);
    memcpy(bytes + at, "DSD ", 4);
    at = dff_chunk(at + 6, "DSD ", samples / 8 * channels);
    length = at + samples / 8 * channels;
    number(4, length - 12, 8, false);
    return at;
}

static void dff_properties_and_damage_are_checked(void)
{
    for (uint32_t ratio = 1; ratio <= 4; ratio *= 2) {
        uint32_t offset = dff(2822400 * ratio, 2, 64000);
        DsdTrack track;
        CHECK(dsd_probe(input(), &track));
        CHECK_EQUAL(64000, track.samples);
        CHECK_EQUAL(offset, track.data_offset);
        CHECK_EQUAL(16000, track.data_bytes);
        length--;
        CHECK(!dsd_probe(input(), &track));
        dff(2822400 * ratio, 2, 64000);
        memcpy(bytes + offset - 18, "DST ", 4);
        CHECK(!dsd_probe(input(), &track));
    }
}

static bool open_file(void *context, const char *path, FlacInput *file)
{
    (void)context;
    (void)path;
    *file = input();
    return true;
}

static void close_file(void *context)
{
    (void)context;
    closes++;
}

static uint8_t reversed(uint8_t value)
{
    uint8_t result = 0;
    for (uint32_t bit = 0; bit < 8; bit++) {
        result |= (uint8_t)(((value >> bit) & 1u) << (7 - bit));
    }
    return result;
}

static void container(uint32_t ratio, uint32_t layout, uint32_t channels)
{
    uint32_t base = 92;
    if (layout == 0) {
        base = dff(2822400 * ratio, channels, 64000);
    } else {
        dsf(2822400 * ratio, channels, 64000);
        number(60, layout == 1 ? 1 : 8, 4, true);
    }
    for (uint32_t index = 0; index < 8000; index++) {
        for (uint32_t channel = 0; channel < channels; channel++) {
            uint32_t at = layout == 0 ? base + index * channels + channel
                                      : base + (index / 4096) * 4096 * channels + channel * 4096 +
                                            index % 4096;
            bytes[at] = layout == 1 ? temporal[channel][index] : reversed(temporal[channel][index]);
        }
    }
}

static Core1Source producer;
static DsdStream adapter;
static Core1Stream consumer;
static FlacDecoder unused;

static AudioFrameResult next_audio(AudioSource audio, AudioFrame *frame)
{
    for (uint32_t step = 0; step < 10000; step++) {
        AudioFrameResult result = audio.next(audio.context, frame);
        if (result != AUDIO_FRAME_WAIT) {
            return result;
        }
        core1_stream_step(&consumer);
    }
    return AUDIO_FRAME_WAIT;
}

static void containers_play_seek_and_reopen_through_the_shared_ring(void)
{
    for (uint32_t ratio = 1; ratio <= 4; ratio *= 2) {
        uint32_t frames = 2000 / ratio;
        make_signal(2822400 * ratio, 0, 64000);
        reference_stages(ratio, 64000, frames);
        for (uint32_t layout = 0; layout < 3; layout++) {
            container(ratio, layout, ratio < 4 ? 2u : 1u);
            control = (StreamControl){0};
            StreamRegion region = {
                .control = &control, .samples = output, .ring = ring, .ring_bytes = sizeof ring};
            core1_source_init(&producer, region, (FlacFileOpener){open_file, close_file, NULL},
                              &unused, 127);
            producer.dsd = &adapter;
            region.ring_bytes = DSD_RING_BYTES;
            core1_stream_init(&consumer, region, core1_dsd_decoder(&decoder, region));
            AudioSource audio = core1_source_interface(&producer, CORE1_FORMAT_DSD);
            for (uint32_t seek = 0; seek < 3; seek++) {
                uint32_t start = seek == 0    ? 0
                                 : seek == 2  ? 60000
                                 : ratio == 1 ? 20
                                 : ratio == 2 ? 8
                                              : 4;
                uint32_t target = (uint32_t)((uint64_t)start * 88200 / 1000);
                if (target > frames) {
                    target = frames;
                }
                AudioTrackInfo track;
                bool opened = audio.open(audio.context, "a.dsf", start, &track);
                CHECK(opened);
                if (!opened) {
                    return;
                }
                CHECK_EQUAL(88200, track.stream.sample_rate);
                CHECK_EQUAL(frames, track.stream.total_samples);
                uint32_t done = target;
                AudioFrame frame;
                AudioFrameResult result;
                while ((result = next_audio(audio, &frame)) == AUDIO_FRAME_READY) {
                    CHECK_EQUAL(done, frame.first_sample);
                    for (uint32_t at = 0; at < frame.samples; at++) {
                        CHECK(abs(frame.left[at] - reference(done + at, 0)) <= 32);
                        CHECK(abs(frame.right[at] - reference(done + at, ratio < 4 ? 1u : 0u)) <=
                              32);
                    }
                    done += frame.samples;
                    audio.release(audio.context);
                }
                CHECK_EQUAL(AUDIO_FRAME_END, result);
                CHECK_EQUAL(frames, done);
                audio.close(audio.context);
            }
        }
    }
}

static void failed_dsd_opens_and_reads_release_the_source(void)
{
    make_signal(2822400, 0, 64000);
    container(1, 1, 2);
    control = (StreamControl){0};
    StreamRegion region = {
        .control = &control, .samples = output, .ring = ring, .ring_bytes = sizeof ring};
    core1_source_init(&producer, region, (FlacFileOpener){open_file, close_file, NULL}, &unused,
                      127);
    producer.dsd = &adapter;
    region.ring_bytes = DSD_RING_BYTES;
    core1_stream_init(&consumer, region, core1_dsd_decoder(&decoder, region));
    AudioSource audio = core1_source_interface(&producer, CORE1_FORMAT_DSD);
    AudioTrackInfo track = {.audio_bytes = 123};
    closes = 0;
    fail_reads = true;
    CHECK(!audio.open(audio.context, "broken.dsf", 0, &track));
    CHECK_EQUAL(123, track.audio_bytes);
    CHECK_EQUAL(1, closes);
    CHECK(!producer.file_open && !producer.streaming);
    fail_reads = false;
    CHECK(audio.open(audio.context, "valid.dsf", 0, &track));
    AudioFrame frame;
    CHECK_EQUAL(AUDIO_FRAME_READY, next_audio(audio, &frame));
    audio.release(audio.context);
    fail_reads = true;
    AudioFrameResult result = AUDIO_FRAME_READY;
    for (uint32_t step = 0; step < 20 && result == AUDIO_FRAME_READY; step++) {
        result = next_audio(audio, &frame);
        if (result == AUDIO_FRAME_READY) {
            audio.release(audio.context);
        }
    }
    CHECK_EQUAL(AUDIO_FRAME_ERROR, result);
    CHECK(producer.read_failed);
    audio.close(audio.context);
    CHECK_EQUAL(2, closes);
    fail_reads = false;
}

static void rates_above_the_core1_instruction_floor_are_refused_before_streaming(void)
{
    static const uint32_t RATES[] = {11289600};
    static const uint32_t CHANNELS[] = {2};
    for (uint32_t index = 0; index < 1; index++) {
        dsf(RATES[index], CHANNELS[index], 64000);
        control = (StreamControl){0};
        StreamRegion region = {
            .control = &control, .samples = output, .ring = ring, .ring_bytes = sizeof ring};
        core1_source_init(&producer, region, (FlacFileOpener){open_file, close_file, NULL}, &unused,
                          127);
        producer.dsd = &adapter;
        AudioSource audio = core1_source_interface(&producer, CORE1_FORMAT_DSD);
        AudioTrackInfo track = {.audio_bytes = 123};
        CHECK(!audio.open(audio.context, "too_fast.dsf", 0, &track));
        CHECK_EQUAL(123, track.audio_bytes);
        CHECK_EQUAL(0, control.generation);
        CHECK(!producer.file_open && !producer.streaming);
        audio.close(audio.context);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(dsf_formats_and_padded_endpoints_are_probed),
        TEST_CASE(dsd_matches_a_double_precision_reference_on_sine_sweep_and_silence),
        TEST_CASE(damaged_dsf_is_refused),
        TEST_CASE(dff_properties_and_damage_are_checked),
        TEST_CASE(containers_play_seek_and_reopen_through_the_shared_ring),
        TEST_CASE(failed_dsd_opens_and_reads_release_the_source),
        TEST_CASE(rates_above_the_core1_instruction_floor_are_refused_before_streaming),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
