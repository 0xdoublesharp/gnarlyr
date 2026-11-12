#include <stdio.h>
#include <string.h>

#include "check.h"
#include "codec/ogg_packet.h"
#include "device/vorbis_decoder.h"

enum {
    FILE_BYTES = 65536,
    PACKET_BYTES = 16384,
    SENTINEL = 0x13572468
};

static uint8_t file_data[FILE_BYTES];
static uint8_t packet_data[PACKET_BYTES];
static _Alignas(8) uint8_t workspace[VORBIS_WORKSPACE_BYTES + 16];
static int32_t samples[2][VORBIS_MAX_FRAMES + 1];
static VorbisDecoder decoder;
static uint32_t file_size;

static bool read_file(void *context, uint32_t offset, void *out, uint32_t bytes)
{
    (void)context;
    CHECK(offset <= file_size && bytes <= file_size - offset);
    memcpy(out, file_data + offset, bytes);
    return true;
}

static bool load(const char *name, OggPackets *reader)
{
    char path[512];
    (void)snprintf(path, sizeof path, "%s/vorbis/%s.ogg", ECHO_FIXTURE_DIR, name);
    FILE *input = fopen(path, "rb");
    CHECK(input != NULL);
    if (input == NULL) {
        return false;
    }
    file_size = (uint32_t)fread(file_data, 1, sizeof file_data, input);
    CHECK(feof(input));
    CHECK_EQUAL(0, fclose(input));
    ogg_packets_init(reader, (FlacInput){read_file, NULL, file_size});
    memset(workspace, 0xA5, sizeof workspace);
    return vorbis_decoder_init(&decoder, workspace + 8, VORBIS_WORKSPACE_BYTES);
}

static VorbisPacket packet_of(OggPacket metadata, uint32_t split)
{
    return (VorbisPacket){
        .span = {{packet_data, split}, {packet_data + split, metadata.bytes - split}},
        .granule = metadata.granule,
        .first = metadata.first,
        .last = metadata.last,
    };
}

static bool headers(OggPackets *reader, bool split)
{
    for (uint32_t index = 0; index < 3; index++) {
        OggPacket metadata;
        CHECK_EQUAL(OGG_PACKET_READY,
                    ogg_packets_next(reader, packet_data, sizeof packet_data, &metadata));
        VorbisPacket packet = packet_of(metadata, split ? metadata.bytes / 2 : metadata.bytes);
        bool ready = vorbis_decoder_header(&decoder, packet);
        CHECK(ready);
        if (!ready) {
            return false;
        }
    }
    CHECK(decoder.ready);
    return decoder.ready;
}

static void check_canaries(void)
{
    for (uint32_t index = 0; index < 8; index++) {
        CHECK_EQUAL(0xA5, workspace[index]);
        CHECK_EQUAL(0xA5, workspace[VORBIS_WORKSPACE_BYTES + 8 + index]);
    }
    for (uint32_t channel = 0; channel < 2; channel++) {
        CHECK_EQUAL(SENTINEL, samples[channel][VORBIS_MAX_FRAMES]);
    }
}

static void compare_reference(const char *name, uint32_t rate, uint32_t channels, uint32_t count,
                              bool split)
{
    OggPackets reader;
    CHECK(load(name, &reader));
    for (uint32_t channel = 0; channel < 2; channel++) {
        samples[channel][VORBIS_MAX_FRAMES] = SENTINEL;
    }
    if (!headers(&reader, split)) {
        return;
    }
    CHECK_EQUAL(rate, decoder.rate);
    CHECK_EQUAL(channels, decoder.channels);
    char path[512];
    (void)snprintf(path, sizeof path, "%s/vorbis/%s.pcm", ECHO_FIXTURE_DIR, name);
    FILE *reference = fopen(path, "rb");
    CHECK(reference != NULL);
    if (reference == NULL) {
        return;
    }
    uint32_t frames = 0;
    bool fractional = false;
    OggPacket metadata;
    while (ogg_packets_next(&reader, packet_data, sizeof packet_data, &metadata) ==
           OGG_PACKET_READY) {
        VorbisPacket packet = packet_of(metadata, split ? metadata.bytes / 2 : metadata.bytes);
        VorbisPcm pcm = {.channels = {samples[0], samples[1]}, .capacity = VORBIS_MAX_FRAMES};
        bool decoded = vorbis_decoder_decode(&decoder, packet, &pcm);
        CHECK(decoded);
        if (!decoded) {
            break;
        }
        for (uint32_t frame = 0; frame < pcm.frames; frame++) {
            for (uint32_t channel = 0; channel < channels; channel++) {
                int low = fgetc(reference);
                int high = fgetc(reference);
                CHECK(low != EOF && high != EOF);
                int16_t expected = (int16_t)((uint32_t)low | (uint32_t)high << 8);
                int32_t got = samples[channel][frame];
                int32_t delta = (got >> 8) - expected;
                CHECK(delta >= -2 && delta <= 2);
                CHECK(got >= -8388608 && got <= 8388607);
                fractional = fractional || (got & 255) != 0;
            }
        }
        frames += pcm.frames;
    }
    CHECK_EQUAL(EOF, fgetc(reference));
    CHECK_EQUAL(0, fclose(reference));
    CHECK_EQUAL(count, frames);
    CHECK(decoder.ended);
    CHECK(fractional);
    CHECK(decoder.memory.peak <= VORBIS_WORKSPACE_BYTES);
    CHECK_EQUAL(decoder.memory.capacity, decoder.memory.temporary);
    check_canaries();
}

static void mono_speech_decodes_to_the_exact_reference_length(void)
{
    compare_reference("mono8", 8000, 1, 2000, false);
}

static void independent_stereo_channels_match_the_reference(void)
{
    compare_reference("stereo44", 44100, 2, 11025, false);
}

static void a_high_quality_stereo_codebook_fits_the_workspace(void)
{
    compare_reference("stereo48", 48000, 2, 12000, false);
}

static void low_quality_codebooks_fit_with_their_temporary_work_released(void)
{
    compare_reference("low48", 48000, 2, 12000, false);
}

static void packet_payloads_can_wrap_in_a_two_span_ring(void)
{
    compare_reference("stereo44", 44100, 2, 11025, true);
}

static void a_small_workspace_exits_before_an_unchecked_allocation_can_escape(void)
{
    OggPackets reader;
    CHECK(load("stereo44", &reader));
    CHECK(vorbis_decoder_init(&decoder, workspace + 8, 8));
    OggPacket metadata;
    CHECK_EQUAL(OGG_PACKET_READY,
                ogg_packets_next(&reader, packet_data, sizeof packet_data, &metadata));
    CHECK(!vorbis_decoder_header(&decoder, packet_of(metadata, metadata.bytes)));
    CHECK(decoder.failed);
    CHECK_EQUAL(VORBIS_MEMORY_EXHAUSTED, decoder.memory.error);
    CHECK(vorbis_decoder_init(&decoder, workspace + 8, VORBIS_WORKSPACE_BYTES));
    CHECK(vorbis_decoder_header(&decoder, packet_of(metadata, metadata.bytes)));
    CHECK_EQUAL(VORBIS_MEMORY_OK, decoder.memory.error);
}

static void every_short_identification_header_is_refused_without_overrunning_buffers(void)
{
    OggPackets reader;
    CHECK(load("stereo44", &reader));
    OggPacket metadata;
    CHECK_EQUAL(OGG_PACKET_READY,
                ogg_packets_next(&reader, packet_data, sizeof packet_data, &metadata));
    for (uint32_t length = 0; length < metadata.bytes; length++) {
        CHECK(vorbis_decoder_init(&decoder, workspace + 8, VORBIS_WORKSPACE_BYTES));
        VorbisPacket packet = packet_of(metadata, metadata.bytes);
        packet.span[0].size = length;
        CHECK(!vorbis_decoder_header(&decoder, packet));
    }
}

static void invalid_identification_fields_do_not_reach_synthesis(void)
{
    OggPackets reader;
    CHECK(load("stereo44", &reader));
    OggPacket metadata;
    CHECK_EQUAL(OGG_PACKET_READY,
                ogg_packets_next(&reader, packet_data, sizeof packet_data, &metadata));
    static const uint8_t OFFSETS[] = {0, 1, 7, 11, 11, 12, 28, 29};
    static const uint8_t VALUES[] = {2, 'X', 1, 0, 3, 0, 0, 0};
    for (uint32_t index = 0; index < sizeof OFFSETS; index++) {
        CHECK(vorbis_decoder_init(&decoder, workspace + 8, VORBIS_WORKSPACE_BYTES));
        uint8_t saved = packet_data[OFFSETS[index]];
        packet_data[OFFSETS[index]] = VALUES[index];
        if (index == 5) {
            packet_data[13] = 0;
            packet_data[14] = 0;
            packet_data[15] = 0;
        }
        CHECK(!vorbis_decoder_header(&decoder, packet_of(metadata, metadata.bytes)));
        packet_data[OFFSETS[index]] = saved;
        if (index == 5) {
            packet_data[13] = 0xAC;
            packet_data[14] = 0;
            packet_data[15] = 0;
        }
        CHECK(!decoder.ready);
    }
}

static void setup_truncation_never_writes_outside_the_workspace(void)
{
    static uint8_t setup[PACKET_BYTES];
    OggPackets original;
    CHECK(load("stereo44", &original));
    OggPacket metadata;
    for (uint32_t header = 0; header < 3; header++) {
        CHECK_EQUAL(OGG_PACKET_READY,
                    ogg_packets_next(&original, packet_data, sizeof packet_data, &metadata));
    }
    uint32_t size = metadata.bytes;
    memcpy(setup, packet_data, size);
    for (uint32_t length = 0; length < size; length += 13) {
        OggPackets reader;
        CHECK(load("stereo44", &reader));
        for (uint32_t header = 0; header < 2; header++) {
            CHECK_EQUAL(OGG_PACKET_READY,
                        ogg_packets_next(&reader, packet_data, sizeof packet_data, &metadata));
            CHECK(vorbis_decoder_header(&decoder, packet_of(metadata, metadata.bytes)));
        }
        memcpy(packet_data, setup, length);
        VorbisPacket packet = {.span = {{packet_data, length}, {NULL, 0}}, .granule = UINT64_MAX};
        CHECK(!vorbis_decoder_header(&decoder, packet));
        for (uint32_t index = 0; index < 8; index++) {
            CHECK_EQUAL(0xA5, workspace[index]);
            CHECK_EQUAL(0xA5, workspace[VORBIS_WORKSPACE_BYTES + 8 + index]);
        }
    }
}

static uint32_t setup_bits(uint32_t bit, uint32_t count)
{
    uint32_t value = 0;
    for (uint32_t index = 0; index < count; index++) {
        value |= ((uint32_t)packet_data[(bit + index) / 8] >> ((bit + index) % 8) & 1u) << index;
    }
    return value;
}

static void zero_codebook_dimension(uint32_t wanted, uint32_t size)
{
    uint32_t found = 0;
    for (uint32_t bit = 64; bit + 64 < size * 8; bit++) {
        if (setup_bits(bit, 24) != 0x564342u) {
            continue;
        }
        if (found++ != wanted) {
            continue;
        }
        CHECK_EQUAL(wanted == 27 ? 2 : 8, setup_bits(bit + 24, 16));
        for (uint32_t index = 0; index < 16; index++) {
            uint32_t at = bit + 24 + index;
            packet_data[at / 8] &= (uint8_t)~(1u << (at % 8));
        }
        return;
    }
    CHECK(false);
}

static void vector_and_partition_books_cannot_use_zero_dimensions(void)
{
    for (uint32_t index = 27; index <= 28; index++) {
        OggPackets reader;
        CHECK(load("stereo44", &reader));
        OggPacket metadata;
        for (uint32_t header = 0; header < 3; header++) {
            CHECK_EQUAL(OGG_PACKET_READY,
                        ogg_packets_next(&reader, packet_data, sizeof packet_data, &metadata));
            if (header < 2) {
                CHECK(vorbis_decoder_header(&decoder, packet_of(metadata, metadata.bytes)));
            }
        }
        zero_codebook_dimension(index, metadata.bytes);
        CHECK(!vorbis_decoder_header(&decoder, packet_of(metadata, metadata.bytes)));
        CHECK(!decoder.ready);
    }
}

static void scalar_books_keep_their_valid_dimension_independent_behavior(void)
{
    OggPackets reader;
    CHECK(load("stereo44", &reader));
    OggPacket metadata;
    for (uint32_t header = 0; header < 3; header++) {
        CHECK_EQUAL(OGG_PACKET_READY,
                    ogg_packets_next(&reader, packet_data, sizeof packet_data, &metadata));
        if (header == 2) {
            packet_data[11] = 0;
            packet_data[12] = 0;
        }
        CHECK(vorbis_decoder_header(&decoder, packet_of(metadata, metadata.bytes)));
    }
    CHECK(decoder.ready);
}

static void packet_metadata_boundaries_are_rejected_before_payload_access(void)
{
    CHECK(vorbis_decoder_init(&decoder, workspace + 8, VORBIS_WORKSPACE_BYTES));
    VorbisPacket packet = {
        .span = {{packet_data, 1}, {NULL, 0}}, .granule = UINT64_MAX, .first = true};
    packet.granule = (uint64_t)INT64_MAX + 1;
    CHECK(!vorbis_decoder_header(&decoder, packet));
    packet.granule = UINT64_MAX;
    packet.span[0].size = (uint32_t)INT32_MAX + 1;
    CHECK(!vorbis_decoder_header(&decoder, packet));
    packet.span[0].size = 1;
    packet.span[1].size = INT32_MAX;
    CHECK(!vorbis_decoder_header(&decoder, packet));
    packet.span[1].size = 0;
    packet.span[0] = (VorbisSpan){NULL, 1};
    CHECK(!vorbis_decoder_header(&decoder, packet));
    packet.span[0] = (VorbisSpan){packet_data, 0};
    packet.span[1] = (VorbisSpan){NULL, 0};
    CHECK(!vorbis_decoder_header(&decoder, packet));
    packet.span[0] = (VorbisSpan){packet_data, 1};
    packet.first = false;
    CHECK(!vorbis_decoder_header(&decoder, packet));
    packet.first = true;
    packet.last = true;
    CHECK(!vorbis_decoder_header(&decoder, packet));
}

static void decode_and_mode_snapshots_validate_output_contracts(void)
{
    OggPackets reader;
    CHECK(load("stereo44", &reader));
    VorbisPcm pcm = {.channels = {samples[0], samples[1]}, .capacity = VORBIS_MAX_FRAMES};
    pcm.frames = 123;
    CHECK(!vorbis_decoder_decode(&decoder, (VorbisPacket){0}, &pcm));
    CHECK_EQUAL(0, pcm.frames);
    VorbisModes modes;
    CHECK(!vorbis_decoder_modes(&decoder, &modes));
    CHECK(headers(&reader, false));
    CHECK(vorbis_decoder_modes(&decoder, &modes));
    CHECK(modes.count > 0);
    CHECK(modes.short_block > 0);
    CHECK(modes.long_block >= modes.short_block);
    CHECK(!vorbis_decoder_modes(&decoder, NULL));

    OggPacket metadata;
    CHECK_EQUAL(OGG_PACKET_READY,
                ogg_packets_next(&reader, packet_data, sizeof packet_data, &metadata));
    VorbisPacket packet = packet_of(metadata, metadata.bytes);
    packet.first = true;
    CHECK(!vorbis_decoder_decode(&decoder, packet, &pcm));
    packet.first = false;
    pcm.channels[0] = NULL;
    CHECK(!vorbis_decoder_decode(&decoder, packet, &pcm));
    pcm.channels[0] = samples[0];
    pcm.channels[1] = NULL;
    CHECK(!vorbis_decoder_decode(&decoder, packet, &pcm));
    pcm.channels[1] = samples[1];
    pcm.capacity = 0;
    CHECK(!vorbis_decoder_decode(&decoder, packet, &pcm));
    pcm.capacity = VORBIS_MAX_FRAMES;
    packet.span[0] = (VorbisSpan){NULL, 1};
    packet.span[1] = (VorbisSpan){NULL, 0};
    CHECK(!vorbis_decoder_decode(&decoder, packet, &pcm));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(mono_speech_decodes_to_the_exact_reference_length),
        TEST_CASE(independent_stereo_channels_match_the_reference),
        TEST_CASE(a_high_quality_stereo_codebook_fits_the_workspace),
        TEST_CASE(low_quality_codebooks_fit_with_their_temporary_work_released),
        TEST_CASE(packet_payloads_can_wrap_in_a_two_span_ring),
        TEST_CASE(a_small_workspace_exits_before_an_unchecked_allocation_can_escape),
        TEST_CASE(every_short_identification_header_is_refused_without_overrunning_buffers),
        TEST_CASE(invalid_identification_fields_do_not_reach_synthesis),
        TEST_CASE(setup_truncation_never_writes_outside_the_workspace),
        TEST_CASE(vector_and_partition_books_cannot_use_zero_dimensions),
        TEST_CASE(scalar_books_keep_their_valid_dimension_independent_behavior),
        TEST_CASE(packet_metadata_boundaries_are_rejected_before_payload_access),
        TEST_CASE(decode_and_mode_snapshots_validate_output_contracts),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
