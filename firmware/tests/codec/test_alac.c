#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "codec/alac.h"

enum {
    FIXTURE_FRAME = 4096,
    CORRUPTION_ROUNDS = 500,
    TRUNCATION_LIMIT = 120,
    MAX_BUILT_BYTES = 4096
};

typedef struct Buffer {
    uint8_t *bytes;
    uint32_t size;
} Buffer;

typedef struct Stream {
    Buffer file;
    AlacConfig config;
    uint32_t packets;
    uint32_t cursor;
} Stream;

typedef struct Packet {
    const uint8_t *bytes;
    uint32_t length;
} Packet;

typedef struct Writer {
    uint8_t bytes[MAX_BUILT_BYTES];
    uint32_t bits;
} Writer;

static AlacDecoder decoder;

static Buffer load(const char *name, const char *extension)
{
    char path[512];
    snprintf(path, sizeof path, "%s/alac/%s.%s", ECHO_FIXTURE_DIR, name, extension);
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

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static bool open_stream(Stream *stream, const char *name)
{
    stream->file = load(name, "alac");
    stream->cursor = ALAC_COOKIE_BYTES + 4;
    if (stream->file.size < stream->cursor) {
        return false;
    }
    stream->packets = read_le32(stream->file.bytes + ALAC_COOKIE_BYTES);
    return alac_parse_cookie(stream->file.bytes, ALAC_COOKIE_BYTES, &stream->config);
}

static void close_stream(Stream *stream)
{
    free(stream->file.bytes);
    stream->file.bytes = NULL;
}

static Packet next_packet(Stream *stream)
{
    Packet packet = {NULL, 0};
    if (stream->cursor + 4 > stream->file.size) {
        return packet;
    }
    uint32_t length = read_le32(stream->file.bytes + stream->cursor);
    if (length > stream->file.size - stream->cursor - 4) {
        return packet;
    }
    packet.bytes = stream->file.bytes + stream->cursor + 4;
    packet.length = length;
    stream->cursor += 4 + length;
    return packet;
}

static int32_t expected_at(const Buffer *pcm, int bits, uint32_t index)
{
    if (bits == 16) {
        int16_t narrow;
        memcpy(&narrow, pcm->bytes + 2 * index, sizeof narrow);
        return narrow;
    }
    int32_t value;
    memcpy(&value, pcm->bytes + 4 * index, sizeof value);
    return value;
}

static bool packet_matches(const Buffer *pcm, int bits, uint32_t first)
{
    uint32_t channels = decoder.config.channels;
    uint32_t count = alac_samples(&decoder);
    for (uint32_t sample = 0; sample < count; sample++) {
        for (uint32_t channel = 0; channel < channels; channel++) {
            uint32_t index = (first + sample) * channels + channel;
            int32_t expected = expected_at(pcm, bits, index);
            if (alac_channel(&decoder, (int)channel)[sample] != expected) {
                return false;
            }
        }
    }
    return true;
}

static bool decodes_exactly(const char *name, int channels, int bits)
{
    Stream stream;
    Buffer pcm = load(name, "pcm");
    bool ok = open_stream(&stream, name) && pcm.size > 0;
    ok = ok && stream.config.channels == channels && stream.config.bit_depth == bits;
    if (ok) {
        alac_start(&decoder, &stream.config);
    }
    uint32_t position = 0;
    for (uint32_t index = 0; ok && index < stream.packets; index++) {
        Packet packet = next_packet(&stream);
        ok = packet.bytes != NULL &&
             alac_decode_packet(&decoder, packet.bytes, packet.length) == ALAC_OK;
        ok = ok && packet_matches(&pcm, bits, position);
        position += alac_samples(&decoder);
    }
    ok = ok && position * (uint32_t)channels * (uint32_t)(bits == 16 ? 2 : 4) == pcm.size;
    close_stream(&stream);
    free(pcm.bytes);
    return ok;
}

static void put_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void make_cookie(uint8_t *bytes)
{
    memset(bytes, 0, ALAC_COOKIE_BYTES);
    put_be32(bytes, 4096);
    bytes[5] = 16;
    bytes[6] = 40;
    bytes[7] = 10;
    bytes[8] = 14;
    bytes[9] = 2;
    bytes[10] = 0;
    bytes[11] = 255;
    put_be32(bytes + 20, 44100);
}

static void a_valid_cookie_is_parsed(void)
{
    uint8_t cookie[ALAC_COOKIE_BYTES];
    make_cookie(cookie);
    AlacConfig config;
    CHECK(alac_parse_cookie(cookie, sizeof cookie, &config));
    CHECK_EQUAL(4096, config.frame_length);
    CHECK_EQUAL(16, config.bit_depth);
    CHECK_EQUAL(40, config.pb);
    CHECK_EQUAL(10, config.mb);
    CHECK_EQUAL(14, config.kb);
    CHECK_EQUAL(2, config.channels);
    CHECK_EQUAL(255, config.max_run);
    CHECK_EQUAL(44100, config.sample_rate);
}

static void cookies_of_fixtures_report_their_format(void)
{
    Stream stream;
    CHECK(open_stream(&stream, "music_24_48k_stereo"));
    CHECK_EQUAL(24, stream.config.bit_depth);
    CHECK_EQUAL(48000, stream.config.sample_rate);
    CHECK_EQUAL(2, stream.config.channels);
    close_stream(&stream);
    CHECK(open_stream(&stream, "music_16_mono"));
    CHECK_EQUAL(16, stream.config.bit_depth);
    CHECK_EQUAL(1, stream.config.channels);
    close_stream(&stream);
}

static bool cookie_with(int offset, uint8_t value)
{
    uint8_t cookie[ALAC_COOKIE_BYTES];
    make_cookie(cookie);
    cookie[offset] = value;
    AlacConfig config;
    return alac_parse_cookie(cookie, sizeof cookie, &config);
}

static void cookies_with_unsupported_parameters_are_rejected(void)
{
    CHECK(!cookie_with(4, 1));
    CHECK(!cookie_with(5, 8));
    CHECK(!cookie_with(5, 12));
    CHECK(!cookie_with(5, 32));
    CHECK(cookie_with(5, 20));
    CHECK(cookie_with(5, 24));
    CHECK(!cookie_with(9, 0));
    CHECK(!cookie_with(9, 3));
    CHECK(cookie_with(9, 1));
    CHECK(!cookie_with(8, 40));
}

static void cookies_with_bad_frame_lengths_are_rejected(void)
{
    uint8_t cookie[ALAC_COOKIE_BYTES];
    AlacConfig config;
    make_cookie(cookie);
    put_be32(cookie, 0);
    CHECK(!alac_parse_cookie(cookie, sizeof cookie, &config));
    put_be32(cookie, 4097);
    CHECK(!alac_parse_cookie(cookie, sizeof cookie, &config));
    put_be32(cookie, 1);
    CHECK(alac_parse_cookie(cookie, sizeof cookie, &config));
    put_be32(cookie, 352);
    CHECK(alac_parse_cookie(cookie, sizeof cookie, &config));
    CHECK_EQUAL(352, config.frame_length);
}

static void short_or_missing_cookies_are_rejected(void)
{
    uint8_t cookie[ALAC_COOKIE_BYTES];
    AlacConfig config;
    make_cookie(cookie);
    CHECK(!alac_parse_cookie(cookie, ALAC_COOKIE_BYTES - 1, &config));
    CHECK(!alac_parse_cookie(NULL, ALAC_COOKIE_BYTES, &config));
    CHECK(!alac_parse_cookie(cookie, ALAC_COOKIE_BYTES, NULL));
}

static void a_rejected_cookie_leaves_the_config_alone(void)
{
    uint8_t cookie[ALAC_COOKIE_BYTES];
    make_cookie(cookie);
    cookie[5] = 8;
    AlacConfig config = {.frame_length = 77};
    CHECK(!alac_parse_cookie(cookie, sizeof cookie, &config));
    CHECK_EQUAL(77, config.frame_length);
}

static void sixteen_bit_stereo_decodes_exactly(void)
{
    CHECK(decodes_exactly("music_16_44k_stereo", 2, 16));
}

static void sixteen_bit_mono_decodes_exactly(void)
{
    CHECK(decodes_exactly("music_16_mono", 1, 16));
}

static void twenty_four_bit_stereo_decodes_exactly(void)
{
    CHECK(decodes_exactly("music_24_48k_stereo", 2, 24));
}

static void twenty_four_bit_mono_decodes_exactly(void)
{
    CHECK(decodes_exactly("music_24_mono", 1, 24));
}

static void verbatim_sixteen_bit_frames_decode_exactly(void)
{
    CHECK(decodes_exactly("verbatim_16_stereo", 2, 16));
}

static void verbatim_twenty_four_bit_frames_decode_exactly(void)
{
    CHECK(decodes_exactly("verbatim_24_stereo", 2, 24));
}

static void low_order_predictors_decode_exactly(void)
{
    CHECK(decodes_exactly("low_order_16_stereo", 2, 16));
}

static void high_order_predictors_decode_exactly(void)
{
    CHECK(decodes_exactly("high_order_16_stereo", 2, 16));
}

static void silence_and_constant_runs_decode_exactly(void)
{
    CHECK(decodes_exactly("gaps_16_stereo", 2, 16));
    CHECK(decodes_exactly("gaps_24_stereo", 2, 24));
}

static void packets_report_full_and_partial_frame_lengths(void)
{
    Stream stream;
    CHECK(open_stream(&stream, "music_16_44k_stereo"));
    alac_start(&decoder, &stream.config);
    CHECK_EQUAL(3, stream.packets);
    uint32_t expected[] = {FIXTURE_FRAME, FIXTURE_FRAME, 9000 - 2 * FIXTURE_FRAME};
    for (int index = 0; index < 3; index++) {
        Packet packet = next_packet(&stream);
        CHECK_EQUAL(ALAC_OK, alac_decode_packet(&decoder, packet.bytes, packet.length));
        CHECK_EQUAL(expected[index], alac_samples(&decoder));
    }
    close_stream(&stream);
}

static void mono_streams_return_the_same_channel_for_both_indices(void)
{
    Stream stream;
    CHECK(open_stream(&stream, "music_16_mono"));
    alac_start(&decoder, &stream.config);
    Packet packet = next_packet(&stream);
    CHECK_EQUAL(ALAC_OK, alac_decode_packet(&decoder, packet.bytes, packet.length));
    CHECK(alac_channel(&decoder, 0) == alac_channel(&decoder, 1));
    CHECK(alac_samples(&decoder) > 0);
    close_stream(&stream);
}

static void stereo_streams_return_distinct_channels(void)
{
    Stream stream;
    CHECK(open_stream(&stream, "music_16_44k_stereo"));
    alac_start(&decoder, &stream.config);
    CHECK(alac_channel(&decoder, 0) != alac_channel(&decoder, 1));
    close_stream(&stream);
}

static void put_bits(Writer *writer, uint32_t value, uint32_t count)
{
    for (uint32_t bit = 0; bit < count; bit++) {
        uint32_t source = (value >> (count - 1 - bit)) & 1u;
        writer->bytes[writer->bits >> 3] |= (uint8_t)(source << (7 - (writer->bits & 7u)));
        writer->bits++;
    }
}

static uint32_t finish(const Writer *writer)
{
    return (writer->bits + 7) >> 3;
}

static void start_element(Writer *writer, uint32_t tag, uint32_t flags)
{
    memset(writer, 0, sizeof *writer);
    put_bits(writer, tag, 3);
    put_bits(writer, 0, 4 + 12);
    put_bits(writer, flags, 4);
}

static AlacConfig test_config(int channels, int depth)
{
    AlacConfig config = {.frame_length = 64,
                         .sample_rate = 44100,
                         .bit_depth = (uint8_t)depth,
                         .pb = 40,
                         .mb = 10,
                         .kb = 14,
                         .channels = (uint8_t)channels,
                         .max_run = 255};
    return config;
}

static void a_hand_built_stereo_escape_frame_decodes(void)
{
    Writer writer;
    AlacConfig config = test_config(2, 16);
    alac_start(&decoder, &config);
    start_element(&writer, 1, 1);
    for (int32_t sample = 0; sample < 64; sample++) {
        put_bits(&writer, (uint32_t)(sample * 500 - 16000) & 0xFFFFu, 16);
        put_bits(&writer, (uint32_t)(-sample * 300 + 5) & 0xFFFFu, 16);
    }
    put_bits(&writer, 7, 3);
    CHECK_EQUAL(ALAC_OK, alac_decode_packet(&decoder, writer.bytes, finish(&writer)));
    CHECK_EQUAL(64, alac_samples(&decoder));
    CHECK_EQUAL(-16000, alac_channel(&decoder, 0)[0]);
    CHECK_EQUAL(5, alac_channel(&decoder, 1)[0]);
    CHECK_EQUAL(63 * 500 - 16000, alac_channel(&decoder, 0)[63]);
    CHECK_EQUAL(-63 * 300 + 5, alac_channel(&decoder, 1)[63]);
}

static void a_hand_built_partial_mono_escape_frame_decodes(void)
{
    Writer writer;
    AlacConfig config = test_config(1, 24);
    alac_start(&decoder, &config);
    start_element(&writer, 0, 8 | 1);
    put_bits(&writer, 3, 32);
    put_bits(&writer, 0x800000u, 24);
    put_bits(&writer, 0x7FFFFFu, 24);
    put_bits(&writer, 0xFFFFFFu, 24);
    CHECK_EQUAL(ALAC_OK, alac_decode_packet(&decoder, writer.bytes, finish(&writer)));
    CHECK_EQUAL(3, alac_samples(&decoder));
    CHECK_EQUAL(-8388608, alac_channel(&decoder, 0)[0]);
    CHECK_EQUAL(8388607, alac_channel(&decoder, 0)[1]);
    CHECK_EQUAL(-1, alac_channel(&decoder, 0)[2]);
}

static void an_empty_partial_frame_is_rejected(void)
{
    Writer writer;
    AlacConfig config = test_config(1, 16);
    alac_start(&decoder, &config);
    start_element(&writer, 0, 8 | 1);
    put_bits(&writer, 0, 32);
    CHECK_EQUAL(ALAC_BAD_DATA, alac_decode_packet(&decoder, writer.bytes, finish(&writer)));
    CHECK_EQUAL(0, alac_samples(&decoder));
}

static void a_partial_frame_longer_than_the_frame_length_is_rejected(void)
{
    Writer writer;
    AlacConfig config = test_config(1, 16);
    alac_start(&decoder, &config);
    start_element(&writer, 0, 8 | 1);
    put_bits(&writer, 65, 32);
    CHECK_EQUAL(ALAC_BAD_DATA, alac_decode_packet(&decoder, writer.bytes, finish(&writer)));
    CHECK_EQUAL(0, alac_samples(&decoder));
}

static void three_shifted_bytes_are_rejected(void)
{
    Writer writer;
    AlacConfig config = test_config(1, 24);
    alac_start(&decoder, &config);
    start_element(&writer, 0, 3 << 1);
    CHECK_EQUAL(ALAC_BAD_DATA, alac_decode_packet(&decoder, writer.bytes, finish(&writer)));
}

static void an_escape_frame_cut_short_is_rejected(void)
{
    Writer writer;
    AlacConfig config = test_config(2, 16);
    alac_start(&decoder, &config);
    start_element(&writer, 1, 1);
    for (int index = 0; index < 100; index++) {
        put_bits(&writer, 0x1234, 16);
    }
    CHECK_EQUAL(ALAC_BAD_DATA, alac_decode_packet(&decoder, writer.bytes, finish(&writer)));
    CHECK_EQUAL(0, alac_samples(&decoder));
}

static void empty_packets_and_end_tags_end_the_stream(void)
{
    AlacConfig config = test_config(2, 16);
    alac_start(&decoder, &config);
    uint8_t end = 0xE0;
    CHECK_EQUAL(ALAC_END, alac_decode_packet(&decoder, &end, 0));
    CHECK_EQUAL(ALAC_END, alac_decode_packet(&decoder, &end, 1));
    CHECK_EQUAL(0, alac_samples(&decoder));
}

static void other_element_tags_are_unsupported(void)
{
    AlacConfig config = test_config(2, 16);
    alac_start(&decoder, &config);
    for (uint8_t tag = 2; tag < 7; tag++) {
        uint8_t packet[8] = {(uint8_t)(tag << 5)};
        CHECK_EQUAL(ALAC_UNSUPPORTED, alac_decode_packet(&decoder, packet, sizeof packet));
    }
}

static void an_element_for_another_channel_count_is_unsupported(void)
{
    Writer writer;
    AlacConfig config = test_config(1, 16);
    alac_start(&decoder, &config);
    start_element(&writer, 1, 1);
    CHECK_EQUAL(ALAC_UNSUPPORTED, alac_decode_packet(&decoder, writer.bytes, finish(&writer)));
}

static bool result_is_known(AlacResult result)
{
    return result == ALAC_OK || result == ALAC_END || result == ALAC_BAD_DATA ||
           result == ALAC_UNSUPPORTED;
}

static bool decodes_safely(const uint8_t *bytes, uint32_t length)
{
    uint8_t *exact = malloc(length > 0 ? length : 1);
    if (exact == NULL) {
        return false;
    }
    memcpy(exact, bytes, length);
    AlacResult result = alac_decode_packet(&decoder, exact, length);
    free(exact);
    bool bounded = alac_samples(&decoder) <= decoder.config.frame_length;
    return result_is_known(result) && bounded && (result == ALAC_OK || alac_samples(&decoder) == 0);
}

static bool survives_truncation(const char *name)
{
    Stream stream;
    bool ok = open_stream(&stream, name);
    alac_start(&decoder, &stream.config);
    Packet packet = next_packet(&stream);
    uint32_t limit = packet.length < TRUNCATION_LIMIT ? packet.length : TRUNCATION_LIMIT;
    for (uint32_t length = 0; ok && length <= limit; length++) {
        ok = decodes_safely(packet.bytes, length);
    }
    for (uint32_t length = packet.length - limit; ok && length < packet.length; length++) {
        ok = decodes_safely(packet.bytes, length);
    }
    close_stream(&stream);
    return ok;
}

static void truncated_packets_never_read_out_of_bounds(void)
{
    CHECK(survives_truncation("music_16_44k_stereo"));
    CHECK(survives_truncation("music_24_48k_stereo"));
    CHECK(survives_truncation("music_16_mono"));
    CHECK(survives_truncation("verbatim_16_stereo"));
    CHECK(survives_truncation("gaps_24_stereo"));
}

static void a_packet_cut_to_a_few_bytes_is_an_error(void)
{
    Stream stream;
    CHECK(open_stream(&stream, "music_16_44k_stereo"));
    alac_start(&decoder, &stream.config);
    Packet packet = next_packet(&stream);
    for (uint32_t length = 1; length < 8; length++) {
        AlacResult result = alac_decode_packet(&decoder, packet.bytes, length);
        CHECK(result == ALAC_BAD_DATA);
        CHECK_EQUAL(0, alac_samples(&decoder));
    }
    close_stream(&stream);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static bool survives_corruption(const char *name, uint32_t seed)
{
    Stream stream;
    bool ok = open_stream(&stream, name);
    alac_start(&decoder, &stream.config);
    Packet packet = next_packet(&stream);
    uint8_t *copy = malloc(packet.length);
    ok = ok && copy != NULL;
    uint32_t state = seed;
    for (uint32_t round = 0; ok && round < CORRUPTION_ROUNDS; round++) {
        memcpy(copy, packet.bytes, packet.length);
        uint32_t flips = 1 + next_random(&state) % 3;
        for (uint32_t flip = 0; flip < flips; flip++) {
            uint32_t where =
                round < 250 ? next_random(&state) % 64 : next_random(&state) % packet.length;
            copy[where] ^= (uint8_t)(1u << (next_random(&state) % 8));
        }
        ok = decodes_safely(copy, packet.length);
    }
    free(copy);
    close_stream(&stream);
    return ok;
}

static void random_byte_flips_stay_within_bounds(void)
{
    CHECK(survives_corruption("music_16_44k_stereo", 1));
    CHECK(survives_corruption("music_24_48k_stereo", 2));
    CHECK(survives_corruption("music_16_mono", 3));
    CHECK(survives_corruption("music_24_mono", 4));
    CHECK(survives_corruption("gaps_16_stereo", 5));
    CHECK(survives_corruption("high_order_16_stereo", 6));
    CHECK(survives_corruption("verbatim_24_stereo", 7));
}

static void random_garbage_packets_stay_within_bounds(void)
{
    AlacConfig config = test_config(2, 24);
    config.frame_length = FIXTURE_FRAME;
    alac_start(&decoder, &config);
    uint8_t packet[300];
    uint32_t state = 99;
    for (uint32_t round = 0; round < CORRUPTION_ROUNDS; round++) {
        for (uint32_t index = 0; index < sizeof packet; index++) {
            packet[index] = (uint8_t)next_random(&state);
        }
        packet[0] = (uint8_t)((packet[0] & 0x1F) | 0x20);
        CHECK(decodes_safely(packet, sizeof packet));
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_valid_cookie_is_parsed),
        TEST_CASE(cookies_of_fixtures_report_their_format),
        TEST_CASE(cookies_with_unsupported_parameters_are_rejected),
        TEST_CASE(cookies_with_bad_frame_lengths_are_rejected),
        TEST_CASE(short_or_missing_cookies_are_rejected),
        TEST_CASE(a_rejected_cookie_leaves_the_config_alone),
        TEST_CASE(sixteen_bit_stereo_decodes_exactly),
        TEST_CASE(sixteen_bit_mono_decodes_exactly),
        TEST_CASE(twenty_four_bit_stereo_decodes_exactly),
        TEST_CASE(twenty_four_bit_mono_decodes_exactly),
        TEST_CASE(verbatim_sixteen_bit_frames_decode_exactly),
        TEST_CASE(verbatim_twenty_four_bit_frames_decode_exactly),
        TEST_CASE(low_order_predictors_decode_exactly),
        TEST_CASE(high_order_predictors_decode_exactly),
        TEST_CASE(silence_and_constant_runs_decode_exactly),
        TEST_CASE(packets_report_full_and_partial_frame_lengths),
        TEST_CASE(mono_streams_return_the_same_channel_for_both_indices),
        TEST_CASE(stereo_streams_return_distinct_channels),
        TEST_CASE(a_hand_built_stereo_escape_frame_decodes),
        TEST_CASE(a_hand_built_partial_mono_escape_frame_decodes),
        TEST_CASE(an_empty_partial_frame_is_rejected),
        TEST_CASE(a_partial_frame_longer_than_the_frame_length_is_rejected),
        TEST_CASE(three_shifted_bytes_are_rejected),
        TEST_CASE(an_escape_frame_cut_short_is_rejected),
        TEST_CASE(empty_packets_and_end_tags_end_the_stream),
        TEST_CASE(other_element_tags_are_unsupported),
        TEST_CASE(an_element_for_another_channel_count_is_unsupported),
        TEST_CASE(truncated_packets_never_read_out_of_bounds),
        TEST_CASE(a_packet_cut_to_a_few_bytes_is_an_error),
        TEST_CASE(random_byte_flips_stay_within_bounds),
        TEST_CASE(random_garbage_packets_stay_within_bounds),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
