#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "codec/flac.h"

typedef struct Buffer {
    uint8_t *bytes;
    uint32_t size;
} Buffer;

static FlacDecoder decoder;

static Buffer load(const char *name, const char *extension)
{
    char path[512];
    snprintf(path, sizeof path, "%s/flac/%s.%s", ECHO_FIXTURE_DIR, name, extension);
    Buffer buffer = {NULL, 0};
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return buffer;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    buffer.bytes = malloc((size_t)size);
    if (buffer.bytes != NULL && fread(buffer.bytes, 1, (size_t)size, file) == (size_t)size) {
        buffer.size = (uint32_t)size;
    }
    fclose(file);
    return buffer;
}

static uint32_t bytes_read;

static bool read_memory(void *context, uint32_t offset, void *destination, uint32_t length)
{
    const Buffer *buffer = context;
    bytes_read += length;
    if (offset > buffer->size || length > buffer->size - offset) {
        return false;
    }
    memcpy(destination, buffer->bytes + offset, length);
    return true;
}

static FlacInput input_of(Buffer *buffer)
{
    return (FlacInput){.read = read_memory, .context = buffer, .size = buffer->size};
}

typedef struct FailingInput {
    Buffer *buffer;
    bool fail;
    uint32_t calls;
    uint32_t fail_at;
} FailingInput;

static bool read_failing_input(void *context, uint32_t offset, void *destination, uint32_t length)
{
    FailingInput *input = context;
    input->calls++;
    return !input->fail && input->calls != input->fail_at &&
           read_memory(input->buffer, offset, destination, length);
}

static int32_t expected_at(const Buffer *pcm, uint32_t index)
{
    int32_t value;
    memcpy(&value, pcm->bytes + 4 * index, sizeof value);
    return value;
}

static bool decodes_exactly(const char *name, uint32_t rate, int channels, int bits)
{
    Buffer flac = load(name, "flac");
    Buffer pcm = load(name, "pcm");
    bool ok = flac.size > 0 && pcm.size > 0 && flac_open(&decoder, input_of(&flac)) == FLAC_OK;
    ok = ok && decoder.info.sample_rate == rate && decoder.info.channels == channels &&
         decoder.info.bits_per_sample == bits &&
         decoder.info.total_samples == pcm.size / 4 / (uint32_t)channels;
    uint32_t frame = 0;
    FlacResult result = FLAC_OK;
    while (ok && (result = flac_decode_frame(&decoder)) == FLAC_OK) {
        for (uint32_t sample = 0; ok && sample < decoder.block_samples; sample++) {
            for (int channel = 0; ok && channel < channels; channel++) {
                uint32_t index = (frame + sample) * (uint32_t)channels + (uint32_t)channel;
                ok = 4 * index < pcm.size &&
                     flac_channel(&decoder, channel)[sample] == expected_at(&pcm, index);
            }
        }
        frame += decoder.block_samples;
    }
    ok = ok && result == FLAC_END && frame == decoder.info.total_samples;
    free(flac.bytes);
    free(pcm.bytes);
    return ok;
}

typedef struct HookLog {
    int calls;
    bool accept;
    uint32_t widest;
    uint32_t largest_order;
} HookLog;

static HookLog hook_log;

static bool restore_in_test(void *context, int32_t *samples, FlacLpcJob job)
{
    HookLog *log = context;
    log->calls++;
    log->widest = job.width > log->widest ? job.width : log->widest;
    log->largest_order = job.order > log->largest_order ? job.order : log->largest_order;
    if (!log->accept) {
        return false;
    }
    for (uint32_t index = job.order; index < job.block_size; index++) {
        int64_t sum = 0;
        for (uint32_t tap = 0; tap < job.order; tap++) {
            sum += (int64_t)job.coefficients[tap] * samples[index - 1 - tap];
        }
        samples[index] += (int32_t)(sum >> job.shift);
    }
    return true;
}

static bool decodes_through_hook(const char *name, uint32_t rate, int channels, int bits,
                                 bool accept)
{
    hook_log = (HookLog){.accept = accept};
    flac_set_lpc_hook(&decoder, (FlacLpcHook){restore_in_test, &hook_log});
    bool ok = decodes_exactly(name, rate, channels, bits);
    flac_set_lpc_hook(&decoder, (FlacLpcHook){NULL, NULL});
    return ok;
}

static void an_lpc_hook_that_restores_gives_the_same_samples(void)
{
    CHECK(decodes_through_hook("lpc_24_96k_stereo", 96000, 2, 24, true));
    CHECK(hook_log.calls > 0);
    CHECK(hook_log.widest >= 24);
    CHECK(hook_log.largest_order >= 3);
}

static void a_hook_that_declines_leaves_the_restore_to_software(void)
{
    CHECK(decodes_through_hook("lpc_16_44k_stereo", 44100, 2, 16, false));
    CHECK(hook_log.calls > 0);
}

static void fixed_predictors_never_reach_the_hook(void)
{
    CHECK(decodes_through_hook("fixed_16_mono", 44100, 1, 16, true));
    CHECK_EQUAL(0, hook_log.calls);
}

static void sixteen_bit_lpc_stereo_decodes_exactly(void)
{
    CHECK(decodes_exactly("lpc_16_44k_stereo", 44100, 2, 16));
}

static void twenty_four_bit_96k_decodes_exactly(void)
{
    CHECK(decodes_exactly("lpc_24_96k_stereo", 96000, 2, 24));
}

static void fixed_predictors_decode_exactly(void)
{
    CHECK(decodes_exactly("fixed_16_mono", 44100, 1, 16));
}

static void constant_and_verbatim_subframes_decode_exactly(void)
{
    CHECK(decodes_exactly("constant_and_noise", 44100, 2, 16));
}

static void wasted_bits_decode_exactly(void)
{
    CHECK(decodes_exactly("wasted_bits_24", 48000, 2, 24));
}

static void odd_block_sizes_decode_exactly(void)
{
    CHECK(decodes_exactly("odd_blocks_16", 44100, 2, 16));
}

static void independent_channels_decode_exactly(void)
{
    CHECK(decodes_exactly("independent_16", 22050, 2, 16));
}

static void rfc_9639_example_one_decodes_the_specified_samples(void)
{
    static uint8_t bytes[] = {
        0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x0f,
        0x00, 0x00, 0x0f, 0x0a, 0xc4, 0x42, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x3e, 0x84, 0xb4, 0x18,
        0x07, 0xdc, 0x69, 0x03, 0x07, 0x58, 0x6a, 0x3d, 0xad, 0x1a, 0x2e, 0x0f, 0xff, 0xf8, 0x69,
        0x18, 0x00, 0x00, 0xbf, 0x03, 0x58, 0xfd, 0x03, 0x12, 0x8b, 0xaa, 0x9a,
    };
    Buffer input = {bytes, sizeof bytes};
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&input)));
    CHECK_EQUAL(44100u, decoder.info.sample_rate);
    CHECK_EQUAL(2, decoder.info.channels);
    CHECK_EQUAL(16, decoder.info.bits_per_sample);
    CHECK_EQUAL(1u, decoder.info.total_samples);
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    CHECK_EQUAL(1u, decoder.block_samples);
    CHECK_EQUAL(25588, flac_channel(&decoder, 0)[0]);
    CHECK_EQUAL(10416, flac_channel(&decoder, 1)[0]);
    CHECK_EQUAL(FLAC_END, flac_decode_frame(&decoder));
}

static uint8_t frame_crc8(const uint8_t *bytes, uint32_t length)
{
    uint32_t crc = 0;
    for (uint32_t index = 0; index < length; index++) {
        crc ^= bytes[index];
        for (uint32_t bit = 0; bit < 8; bit++) {
            crc = (uint8_t)((crc & 0x80u) != 0 ? (crc << 1) ^ 0x07u : crc << 1);
        }
    }
    return (uint8_t)crc;
}

static uint16_t frame_crc16(const uint8_t *bytes, uint32_t length)
{
    uint32_t crc = 0;
    for (uint32_t index = 0; index < length; index++) {
        crc ^= (uint32_t)bytes[index] << 8;
        for (uint32_t bit = 0; bit < 8; bit++) {
            crc = (uint16_t)((crc & 0x8000u) != 0 ? (crc << 1) ^ 0x8005u : crc << 1);
        }
    }
    return (uint16_t)crc;
}

static void extended_sample_rate_headers_decode_their_audio(void)
{
    static const uint8_t header[] = {
        0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00,
        0x0f, 0x00, 0x00, 0x0f, 0x0a, 0xc4, 0x42, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x3e, 0x84,
        0xb4, 0x18, 0x07, 0xdc, 0x69, 0x03, 0x07, 0x58, 0x6a, 0x3d, 0xad, 0x1a, 0x2e, 0x0f,
    };
    static const uint8_t body[] = {0x03, 0x58, 0xfd, 0x03, 0x12, 0x8b};
    for (uint8_t code = 12; code <= 14; code++) {
        uint8_t bytes[80] = {0};
        memcpy(bytes, header, sizeof header);
        if (code == 12) {
            bytes[19] = 0xbe;
            bytes[20] = 0x02;
        }
        uint32_t at = sizeof header;
        uint32_t frame = at;
        bytes[at++] = 0xff;
        bytes[at++] = 0xf8;
        bytes[at++] = (uint8_t)(0x60u | code);
        bytes[at++] = 0x18;
        bytes[at++] = 0;
        bytes[at++] = 0;
        uint32_t rate = code == 12 ? 44 : code == 13 ? 44100 : 4410;
        if (code != 12) {
            bytes[at++] = (uint8_t)(rate >> 8);
        }
        bytes[at++] = (uint8_t)rate;
        bytes[at] = frame_crc8(bytes + frame, at - frame);
        at++;
        memcpy(bytes + at, body, sizeof body);
        at += sizeof body;
        uint16_t crc = frame_crc16(bytes + frame, at - frame);
        bytes[at++] = (uint8_t)(crc >> 8);
        bytes[at++] = (uint8_t)crc;
        Buffer input = {bytes, at};
        CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&input)));
        CHECK_EQUAL(code == 12 ? 44000u : 44100u, decoder.info.sample_rate);
        CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
        CHECK_EQUAL(1u, decoder.block_samples);
        CHECK_EQUAL(25588, flac_channel(&decoder, 0)[0]);
        CHECK_EQUAL(10416, flac_channel(&decoder, 1)[0]);
        CHECK_EQUAL(FLAC_END, flac_decode_frame(&decoder));
    }
}

static void standard_and_extended_block_sizes_decode_constant_channels(void)
{
    static const struct {
        uint8_t code;
        uint16_t samples;
    } cases[] = {{1, 192}, {2, 576}, {3, 1152},  {4, 2304},  {5, 4608}, {7, 257},
                 {8, 256}, {9, 512}, {10, 1024}, {11, 2048}, {12, 4096}};
    for (uint32_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        uint32_t samples = cases[index].samples;
        uint8_t bytes[80] = {
            0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x10, 0x00, 0x10, 0x00, 0x00,
            0x00, 0x0f, 0x00, 0x00, 0x0f, 0x0a, 0xc4, 0x42, 0xf0, 0x00, 0x00, 0x00, 0x00,
        };
        bytes[8] = bytes[10] = (uint8_t)(samples >> 8);
        bytes[9] = bytes[11] = (uint8_t)samples;
        bytes[24] = (uint8_t)(samples >> 8);
        bytes[25] = (uint8_t)samples;
        uint32_t at = 42;
        uint32_t frame = at;
        bytes[at++] = 0xff;
        bytes[at++] = 0xf8;
        bytes[at++] = (uint8_t)((cases[index].code << 4) | 9);
        bytes[at++] = 0x18;
        bytes[at++] = 0;
        if (cases[index].code == 7) {
            bytes[at++] = (uint8_t)((samples - 1) >> 8);
            bytes[at++] = (uint8_t)(samples - 1);
        }
        bytes[at] = frame_crc8(bytes + frame, at - frame);
        at++;
        static const uint8_t body[] = {0, 0x12, 0x34, 0, 0xff, 0xfe};
        memcpy(bytes + at, body, sizeof body);
        at += sizeof body;
        uint16_t crc = frame_crc16(bytes + frame, at - frame);
        bytes[at++] = (uint8_t)(crc >> 8);
        bytes[at++] = (uint8_t)crc;
        Buffer input = {bytes, at};
        CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&input)));
        CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
        CHECK_EQUAL(samples, decoder.block_samples);
        for (uint32_t sample = 0; sample < samples; sample++) {
            CHECK_EQUAL(0x1234, flac_channel(&decoder, 0)[sample]);
            CHECK_EQUAL(-2, flac_channel(&decoder, 1)[sample]);
        }
        CHECK_EQUAL(FLAC_END, flac_decode_frame(&decoder));
    }
}

static void coded_frame_numbers_report_the_correct_sample_and_reject_bad_encodings(void)
{
    static const struct {
        uint8_t number[4];
        uint8_t length;
        uint32_t first_sample;
        bool valid;
    } cases[] = {
        {{0x7f}, 1, 127, true},
        {{0xc2, 0x80}, 2, 128, true},
        {{0xe0, 0xa0, 0x80}, 3, 2048, true},
        {{0xf0, 0x90, 0x80, 0x80}, 4, 65536, true},
        {{0x80}, 1, 0, false},
        {{0xfe}, 1, 0, false},
        {{0xc2, 0x41}, 2, 0, false},
    };
    for (uint32_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        uint8_t bytes[32] = {0xff, 0xf9, 0x19, 0x18};
        uint32_t at = 4;
        memcpy(bytes + at, cases[index].number, cases[index].length);
        at += cases[index].length;
        bytes[at] = frame_crc8(bytes, at);
        at++;
        static const uint8_t body[] = {0, 0x12, 0x34, 0, 0xff, 0xfe};
        memcpy(bytes + at, body, sizeof body);
        at += sizeof body;
        uint16_t crc = frame_crc16(bytes, at);
        bytes[at++] = (uint8_t)(crc >> 8);
        bytes[at++] = (uint8_t)crc;
        Buffer input = {bytes, at};
        FlacInfo info = {.sample_rate = 44100,
                         .channels = 2,
                         .bits_per_sample = 16,
                         .min_block = 192,
                         .max_block = 192};
        flac_start(&decoder, input_of(&input), info);
        FlacResult result = flac_decode_frame(&decoder);
        CHECK_EQUAL(cases[index].valid ? FLAC_OK : FLAC_ERROR, result);
        if (cases[index].valid) {
            CHECK_EQUAL(cases[index].first_sample, decoder.frame_first_sample);
            CHECK_EQUAL(192u, decoder.block_samples);
            CHECK_EQUAL(0x1234, flac_channel(&decoder, 0)[0]);
            CHECK_EQUAL(-2, flac_channel(&decoder, 1)[191]);
        } else {
            CHECK_EQUAL(0u, decoder.block_samples);
        }
    }
}

static int32_t fixed_predictor_sample(uint32_t order, uint32_t index)
{
    int32_t x = (int32_t)index + 1;
    return order == 0 ? 0 : order == 1 ? 1 : order == 2 ? x : order == 3 ? x * x : x * x * x;
}

static void all_fixed_predictor_orders_restore_their_expected_samples(void)
{
    for (uint32_t order = 0; order <= 4; order++) {
        for (uint32_t method = 0; method <= 1; method++) {
            uint8_t bytes[64] = {0xff, 0xf8, 0x69, 0x08, 0, 15};
            uint32_t at = 6;
            bytes[at] = frame_crc8(bytes, at);
            at++;
            bytes[at++] = (uint8_t)(0x10u | (order << 1));
            for (uint32_t index = 0; index < order; index++) {
                uint32_t sample = (uint32_t)fixed_predictor_sample(order, index);
                bytes[at++] = (uint8_t)(sample >> 8);
                bytes[at++] = (uint8_t)sample;
            }
            uint32_t header_bits = 2 + 4 + (method == 0 ? 4 : 5);
            uint32_t bits = header_bits + 16 - order;
            if (method != 0) {
                bytes[at] |= 0x40;
            }
            for (uint32_t bit = header_bits; bit < bits; bit++) {
                bytes[at + bit / 8] |= (uint8_t)(0x80u >> (bit % 8));
            }
            at += (bits + 7) / 8;
            uint16_t crc = frame_crc16(bytes, at);
            bytes[at++] = (uint8_t)(crc >> 8);
            bytes[at++] = (uint8_t)crc;
            Buffer input = {bytes, at};
            FlacInfo info = {.sample_rate = 44100,
                             .channels = 1,
                             .bits_per_sample = 16,
                             .min_block = 16,
                             .max_block = 16,
                             .total_samples = 16};
            flac_start(&decoder, input_of(&input), info);
            CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
            CHECK_EQUAL(16u, decoder.block_samples);
            for (uint32_t index = 0; index < 16; index++) {
                CHECK_EQUAL(fixed_predictor_sample(order, index), flac_channel(&decoder, 0)[index]);
            }
            CHECK_EQUAL(FLAC_END, flac_decode_frame(&decoder));
        }
    }
}

static void put_frame_bits(uint8_t *bytes, uint32_t *position, uint32_t value, uint32_t count)
{
    for (uint32_t index = count; index > 0; index--) {
        if ((value & (1u << (index - 1))) != 0) {
            bytes[*position / 8] |= (uint8_t)(0x80u >> (*position % 8));
        }
        (*position)++;
    }
}

static void escaped_residuals_decode_signed_samples_with_both_rice_methods(void)
{
    for (uint32_t method = 0; method <= 1; method++) {
        uint8_t bytes[64] = {0xff, 0xf8, 0x69, 0x08, 0, 15};
        uint32_t at = 6;
        bytes[at] = frame_crc8(bytes, at);
        at++;
        bytes[at++] = 0x10;
        uint32_t bit = at * 8;
        put_frame_bits(bytes, &bit, method, 2);
        put_frame_bits(bytes, &bit, 0, 4);
        put_frame_bits(bytes, &bit, method == 0 ? 15 : 31, method == 0 ? 4 : 5);
        put_frame_bits(bytes, &bit, 4, 5);
        for (uint32_t index = 0; index < 16; index++) {
            put_frame_bits(bytes, &bit, index, 4);
        }
        at = (bit + 7) / 8;
        uint16_t crc = frame_crc16(bytes, at);
        bytes[at++] = (uint8_t)(crc >> 8);
        bytes[at++] = (uint8_t)crc;
        Buffer input = {bytes, at};
        FlacInfo info = {.sample_rate = 44100,
                         .channels = 1,
                         .bits_per_sample = 16,
                         .min_block = 16,
                         .max_block = 16,
                         .total_samples = 16};
        flac_start(&decoder, input_of(&input), info);
        CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
        CHECK_EQUAL(16u, decoder.block_samples);
        for (uint32_t index = 0; index < 16; index++) {
            int32_t expected = index < 8 ? (int32_t)index : (int32_t)index - 16;
            CHECK_EQUAL(expected, flac_channel(&decoder, 0)[index]);
        }
        CHECK_EQUAL(FLAC_END, flac_decode_frame(&decoder));
    }
}

static void rfc_9639_partitioned_lpc_decodes_the_specified_samples(void)
{
    static uint8_t bytes[] = {
        0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x1f,
        0x00, 0x00, 0x1f, 0x07, 0xd0, 0x00, 0x70, 0x00, 0x00, 0x00, 0x18, 0xf8, 0xf9, 0xe3, 0x96,
        0xf5, 0xcb, 0xcf, 0xc6, 0xdc, 0x80, 0x7f, 0x99, 0x77, 0x90, 0x6b, 0x32, 0xff, 0xf8, 0x68,
        0x02, 0x00, 0x17, 0xe9, 0x44, 0x00, 0x4f, 0x6f, 0x31, 0x3d, 0x10, 0x47, 0xd2, 0x27, 0xcb,
        0x6d, 0x09, 0x08, 0x31, 0x45, 0x2b, 0xdc, 0x28, 0x22, 0x22, 0x80, 0x57, 0xa3,
    };
    static const int32_t expected[] = {0,  79,  111, 78,  8,   -61, -90, -68, -13, 42, 67, 53,
                                       13, -27, -46, -38, -12, 14,  24,  19,  6,   -4, -5, 0};
    Buffer input = {bytes, sizeof bytes};
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&input)));
    CHECK_EQUAL(32000u, decoder.info.sample_rate);
    CHECK_EQUAL(1, decoder.info.channels);
    CHECK_EQUAL(8, decoder.info.bits_per_sample);
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    CHECK_EQUAL(24u, decoder.block_samples);
    for (uint32_t index = 0; index < sizeof expected / sizeof expected[0]; index++) {
        CHECK_EQUAL(expected[index], flac_channel(&decoder, 0)[index]);
    }
    CHECK_EQUAL(FLAC_END, flac_decode_frame(&decoder));
}

static void blocks_larger_than_the_buffer_are_refused(void)
{
    Buffer flac = load("big_blocks_16", "flac");
    CHECK(flac.size > 0);
    CHECK_EQUAL(FLAC_UNSUPPORTED, flac_open(&decoder, input_of(&flac)));
    free(flac.bytes);
}

static void unsupported_streaminfo_limits_are_rejected(void)
{
    Buffer flac = load("fixed_16_mono", "flac");
    CHECK(flac.size >= 42);
    uint8_t original[42];
    memcpy(original, flac.bytes, sizeof original);
    flac.bytes[10] = 0x12;
    flac.bytes[11] = 0x01;
    CHECK_EQUAL(FLAC_UNSUPPORTED, flac_open(&decoder, input_of(&flac)));
    flac.bytes[11] = 0x00;
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    memcpy(flac.bytes, original, sizeof original);
    flac.bytes[20] = (uint8_t)((flac.bytes[20] & 0xf1u) | 0x04u);
    CHECK_EQUAL(FLAC_UNSUPPORTED, flac_open(&decoder, input_of(&flac)));
    memcpy(flac.bytes, original, sizeof original);
    flac.bytes[20] |= 1;
    flac.bytes[21] = (uint8_t)((flac.bytes[21] & 0x0fu) | 0x80u);
    CHECK_EQUAL(FLAC_UNSUPPORTED, flac_open(&decoder, input_of(&flac)));
    memcpy(flac.bytes, original, sizeof original);
    flac.bytes[21] |= 1;
    CHECK_EQUAL(FLAC_UNSUPPORTED, flac_open(&decoder, input_of(&flac)));
    free(flac.bytes);
}

static void a_file_that_is_not_flac_is_refused(void)
{
    Buffer pcm = load("fixed_16_mono", "pcm");
    CHECK_EQUAL(FLAC_ERROR, flac_open(&decoder, input_of(&pcm)));
    free(pcm.bytes);
}

static void an_id3_prefix_with_footer_preserves_decoded_audio(void)
{
    Buffer flac = load("fixed_16_mono", "flac");
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    uint32_t original_offset = decoder.audio_offset;
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    int32_t first_sample = flac_channel(&decoder, 0)[0];
    uint32_t block_samples = decoder.block_samples;
    enum {
        PREFIX = 23
    };
    uint8_t *bytes = calloc(flac.size + PREFIX, 1);
    CHECK(bytes != NULL);
    if (bytes == NULL) {
        free(flac.bytes);
        return;
    }
    memcpy(bytes, "ID3", 3);
    bytes[3] = 4;
    bytes[5] = 0x10;
    bytes[9] = 3;
    memcpy(bytes + 13, bytes, 10);
    memcpy(bytes + 13, "3DI", 3);
    memcpy(bytes + PREFIX, flac.bytes, flac.size);
    Buffer tagged = {bytes, flac.size + PREFIX};
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&tagged)));
    CHECK_EQUAL(original_offset + PREFIX, decoder.audio_offset);
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    CHECK_EQUAL(block_samples, decoder.block_samples);
    CHECK_EQUAL(first_sample, flac_channel(&decoder, 0)[0]);
    free(bytes);
    free(flac.bytes);
}

static FlacResult decode_to_the_end(void)
{
    FlacResult result;
    while ((result = flac_decode_frame(&decoder)) == FLAC_OK) {
    }
    return result;
}

static void a_truncated_file_ends_with_an_error(void)
{
    Buffer flac = load("lpc_16_44k_stereo", "flac");
    flac.size -= 100;
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    CHECK_EQUAL(FLAC_ERROR, decode_to_the_end());
    free(flac.bytes);
}

static void a_corrupted_frame_is_caught(void)
{
    Buffer flac = load("lpc_16_44k_stereo", "flac");
    flac.bytes[flac.size - 2000] ^= 0x10;
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    CHECK_EQUAL(FLAC_ERROR, decode_to_the_end());
    free(flac.bytes);
}

static void a_storage_failure_never_publishes_a_partial_frame_and_can_recover(void)
{
    Buffer flac = load("fixed_16_mono", "flac");
    Buffer pcm = load("fixed_16_mono", "pcm");
    FailingInput source = {.buffer = &flac};
    FlacInput input = {.read = read_failing_input, .context = &source, .size = flac.size};
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input));
    source.fail = true;
    CHECK_EQUAL(FLAC_ERROR, flac_decode_frame(&decoder));
    CHECK_EQUAL(0u, decoder.block_samples);
    source.fail = false;
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input));
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    CHECK(decoder.block_samples > 0);
    CHECK_EQUAL(expected_at(&pcm, 0), flac_channel(&decoder, 0)[0]);
    free(flac.bytes);
    free(pcm.bytes);
}

static void every_frame_read_failure_rejects_the_frame_without_losing_recovery(void)
{
    Buffer flac = load("lpc_24_96k_stereo", "flac");
    Buffer pcm = load("lpc_24_96k_stereo", "pcm");
    FailingInput source = {.buffer = &flac};
    FlacInput input = {.read = read_failing_input, .context = &source, .size = flac.size};
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input));
    source.calls = 0;
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    uint32_t reads = source.calls;
    CHECK(reads > 1);
    for (uint32_t at = 1; at <= reads; at++) {
        source.fail_at = 0;
        CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input));
        source.calls = 0;
        source.fail_at = at;
        CHECK_EQUAL(FLAC_ERROR, flac_decode_frame(&decoder));
        CHECK_EQUAL(0u, decoder.block_samples);
        source.fail_at = 0;
        CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input));
        CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
        CHECK_EQUAL(expected_at(&pcm, 0), flac_channel(&decoder, 0)[0]);
        CHECK_EQUAL(expected_at(&pcm, 1), flac_channel(&decoder, 1)[0]);
    }
    free(flac.bytes);
    free(pcm.bytes);
}

static bool seeks_to(const char *name, uint32_t target)
{
    Buffer flac = load(name, "flac");
    Buffer pcm = load(name, "pcm");
    bool ok = flac_open(&decoder, input_of(&flac)) == FLAC_OK &&
              flac_seek(&decoder, target) == FLAC_OK && flac_decode_frame(&decoder) == FLAC_OK;
    uint32_t first = decoder.frame_first_sample;
    ok = ok && first <= target && target < first + decoder.block_samples;
    int channels = decoder.info.channels;
    for (uint32_t sample = 0; ok && sample < decoder.block_samples; sample++) {
        for (int channel = 0; ok && channel < channels; channel++) {
            uint32_t index = (first + sample) * (uint32_t)channels + (uint32_t)channel;
            ok = flac_channel(&decoder, channel)[sample] == expected_at(&pcm, index);
        }
    }
    free(flac.bytes);
    free(pcm.bytes);
    return ok;
}

static void seeking_lands_on_the_frame_that_holds_the_target(void)
{
    CHECK(seeks_to("lpc_16_44k_stereo", 0));
    CHECK(seeks_to("lpc_16_44k_stereo", 4095));
    CHECK(seeks_to("lpc_16_44k_stereo", 4096));
    CHECK(seeks_to("lpc_16_44k_stereo", 5999));
    CHECK(seeks_to("lpc_24_96k_stereo", 2500));
    CHECK(seeks_to("odd_blocks_16", 1152));
    CHECK(seeks_to("odd_blocks_16", 3000));
    CHECK(seeks_to("constant_and_noise", 7777));
}

static void decoding_carries_on_after_a_seek(void)
{
    Buffer flac = load("odd_blocks_16", "flac");
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    CHECK_EQUAL(FLAC_OK, flac_seek(&decoder, 1500));
    uint32_t total = 0;
    FlacResult result;
    while ((result = flac_decode_frame(&decoder)) == FLAC_OK) {
        total += decoder.block_samples;
    }
    CHECK_EQUAL(FLAC_END, result);
    CHECK_EQUAL(3001u - 1152u, total);
    free(flac.bytes);
}

static void seeking_past_the_end_is_refused(void)
{
    Buffer flac = load("odd_blocks_16", "flac");
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    CHECK_EQUAL(FLAC_END, flac_seek(&decoder, 3001));
    free(flac.bytes);
}

static void frames_report_their_first_sample(void)
{
    Buffer flac = load("odd_blocks_16", "flac");
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    CHECK_EQUAL(0u, decoder.frame_first_sample);
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    CHECK_EQUAL(1152u, decoder.frame_first_sample);
    free(flac.bytes);
}

static void a_seek_reads_little_of_the_file(void)
{
    Buffer flac = load("lpc_24_96k_stereo", "flac");
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    bytes_read = 0;
    CHECK_EQUAL(FLAC_OK, flac_seek(&decoder, 4500));
    CHECK(bytes_read < 64 * 1024);
    free(flac.bytes);
}

static void a_seek_read_failure_never_reports_the_wrong_frame(void)
{
    Buffer flac = load("lpc_24_96k_stereo", "flac");
    FailingInput source = {.buffer = &flac};
    FlacInput input = {.read = read_failing_input, .context = &source, .size = flac.size};
    const uint32_t target = 4500;
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input));
    source.calls = 0;
    CHECK_EQUAL(FLAC_OK, flac_seek(&decoder, target));
    uint32_t reads = source.calls;
    CHECK(reads > 1);
    for (uint32_t at = 1; at <= reads; at++) {
        source.fail_at = 0;
        CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input));
        source.calls = 0;
        source.fail_at = at;
        FlacResult result = flac_seek(&decoder, target);
        CHECK(source.calls >= at);
        source.fail_at = 0;
        if (result == FLAC_OK) {
            CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
            CHECK(decoder.frame_first_sample <= target);
            CHECK(target - decoder.frame_first_sample < decoder.block_samples);
        } else {
            CHECK_EQUAL(FLAC_ERROR, result);
        }
    }
    free(flac.bytes);
}

static FlacBits header_bits;

static void the_header_alone_gives_the_stream_info_and_audio_offset(void)
{
    Buffer flac = load("lpc_24_96k_stereo", "flac");
    FlacInfo info = {0};
    uint32_t audio_offset = 0;
    CHECK_EQUAL(FLAC_OK, flac_read_header(&header_bits, input_of(&flac), &info, &audio_offset));
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    CHECK_EQUAL(decoder.audio_offset, audio_offset);
    CHECK_EQUAL(decoder.info.sample_rate, info.sample_rate);
    CHECK_EQUAL(decoder.info.bits_per_sample, info.bits_per_sample);
    CHECK_EQUAL(decoder.info.channels, info.channels);
    CHECK_EQUAL(decoder.info.max_block, info.max_block);
    CHECK_EQUAL(decoder.info.total_samples, info.total_samples);
    free(flac.bytes);
}

static void a_header_that_is_not_flac_is_refused(void)
{
    uint8_t bytes[64] = {'R', 'I', 'F', 'F'};
    Buffer buffer = {bytes, sizeof bytes};
    FlacInfo info = {0};
    uint32_t audio_offset = 0;
    CHECK_EQUAL(FLAC_ERROR,
                flac_read_header(&header_bits, input_of(&buffer), &info, &audio_offset));
}

static void truncated_streaminfo_headers_are_rejected(void)
{
    Buffer flac = load("fixed_16_mono", "flac");
    CHECK(flac.size >= 42);
    CHECK_EQUAL(0, memcmp(flac.bytes, "fLaC", 4));
    uint32_t size = flac.size;
    for (uint32_t length = 0; length < 42; length++) {
        flac.size = length;
        CHECK_EQUAL(FLAC_ERROR, flac_open(&decoder, input_of(&flac)));
    }
    flac.size = size;
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    free(flac.bytes);
}

static void metadata_without_streaminfo_or_past_eof_is_rejected(void)
{
    Buffer flac = load("fixed_16_mono", "flac");
    CHECK(flac.size >= 42);
    flac.bytes[4] = 0x81;
    CHECK_EQUAL(FLAC_ERROR, flac_open(&decoder, input_of(&flac)));
    flac.bytes[5] = 0xff;
    flac.bytes[6] = 0xff;
    flac.bytes[7] = 0xff;
    CHECK_EQUAL(FLAC_ERROR, flac_open(&decoder, input_of(&flac)));
    free(flac.bytes);
}

static void decoding_can_start_from_audio_bytes_alone(void)
{
    Buffer flac = load("lpc_24_96k_stereo", "flac");
    Buffer pcm = load("lpc_24_96k_stereo", "pcm");
    FlacInfo info = {0};
    uint32_t audio_offset = 0;
    CHECK_EQUAL(FLAC_OK, flac_read_header(&header_bits, input_of(&flac), &info, &audio_offset));
    Buffer audio = {flac.bytes + audio_offset, flac.size - audio_offset};
    flac_start(&decoder, input_of(&audio), info);
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    CHECK_EQUAL(0u, decoder.frame_first_sample);
    CHECK(decoder.block_samples > 0);
    bool exact = true;
    for (uint32_t sample = 0; sample < decoder.block_samples; sample++) {
        exact = exact && flac_channel(&decoder, 0)[sample] == expected_at(&pcm, 2 * sample) &&
                flac_channel(&decoder, 1)[sample] == expected_at(&pcm, 2 * sample + 1);
    }
    CHECK(exact);
    free(flac.bytes);
    free(pcm.bytes);
}

static void the_position_moves_from_the_audio_start_to_the_next_frame(void)
{
    Buffer flac = load("fixed_16_mono", "flac");
    CHECK_EQUAL(FLAC_OK, flac_open(&decoder, input_of(&flac)));
    CHECK_EQUAL(decoder.audio_offset, flac_position(&decoder));
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    uint32_t second = flac_position(&decoder);
    CHECK(second > decoder.audio_offset);
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    CHECK_EQUAL(1152u, decoder.frame_first_sample);
    flac_rewind(&decoder, second);
    CHECK_EQUAL(FLAC_OK, flac_decode_frame(&decoder));
    CHECK_EQUAL(1152u, decoder.frame_first_sample);
    free(flac.bytes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(sixteen_bit_lpc_stereo_decodes_exactly),
        TEST_CASE(an_lpc_hook_that_restores_gives_the_same_samples),
        TEST_CASE(a_hook_that_declines_leaves_the_restore_to_software),
        TEST_CASE(fixed_predictors_never_reach_the_hook),
        TEST_CASE(twenty_four_bit_96k_decodes_exactly),
        TEST_CASE(fixed_predictors_decode_exactly),
        TEST_CASE(constant_and_verbatim_subframes_decode_exactly),
        TEST_CASE(wasted_bits_decode_exactly),
        TEST_CASE(odd_block_sizes_decode_exactly),
        TEST_CASE(independent_channels_decode_exactly),
        TEST_CASE(rfc_9639_example_one_decodes_the_specified_samples),
        TEST_CASE(extended_sample_rate_headers_decode_their_audio),
        TEST_CASE(standard_and_extended_block_sizes_decode_constant_channels),
        TEST_CASE(coded_frame_numbers_report_the_correct_sample_and_reject_bad_encodings),
        TEST_CASE(rfc_9639_partitioned_lpc_decodes_the_specified_samples),
        TEST_CASE(all_fixed_predictor_orders_restore_their_expected_samples),
        TEST_CASE(escaped_residuals_decode_signed_samples_with_both_rice_methods),
        TEST_CASE(blocks_larger_than_the_buffer_are_refused),
        TEST_CASE(unsupported_streaminfo_limits_are_rejected),
        TEST_CASE(a_file_that_is_not_flac_is_refused),
        TEST_CASE(an_id3_prefix_with_footer_preserves_decoded_audio),
        TEST_CASE(a_truncated_file_ends_with_an_error),
        TEST_CASE(a_corrupted_frame_is_caught),
        TEST_CASE(a_storage_failure_never_publishes_a_partial_frame_and_can_recover),
        TEST_CASE(every_frame_read_failure_rejects_the_frame_without_losing_recovery),
        TEST_CASE(frames_report_their_first_sample),
        TEST_CASE(seeking_lands_on_the_frame_that_holds_the_target),
        TEST_CASE(decoding_carries_on_after_a_seek),
        TEST_CASE(seeking_past_the_end_is_refused),
        TEST_CASE(a_seek_reads_little_of_the_file),
        TEST_CASE(a_seek_read_failure_never_reports_the_wrong_frame),
        TEST_CASE(the_header_alone_gives_the_stream_info_and_audio_offset),
        TEST_CASE(a_header_that_is_not_flac_is_refused),
        TEST_CASE(truncated_streaminfo_headers_are_rejected),
        TEST_CASE(metadata_without_streaminfo_or_past_eof_is_rejected),
        TEST_CASE(decoding_can_start_from_audio_bytes_alone),
        TEST_CASE(the_position_moves_from_the_audio_start_to_the_next_frame),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
