#include <string.h>

#include "check.h"
#include "device/asf_stream.h"
#include "device/asf_track.h"
#include "m4a_fixture_files.h"

static void standard_v1_v2_are_probed_with_bounded_packets_and_playable_timing(void)
{
    static const struct {
        const char *path;
        uint32_t codec;
        uint32_t channels;
        uint32_t rate;
    } cases[] = {{"wma/v1mono.wma", 0x160, 1, 16000},
                 {"wma/v2stereo.wma", 0x161, 2, 48000},
                 {"wma/v2seek.wma", 0x161, 2, 44100}};
    for (uint32_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        M4aFile file = m4a_file_load(cases[index].path);
        AsfTrack track;
        CHECK(asf_track_probe(m4a_file_input(&file), &track));
        CHECK_EQUAL(cases[index].codec, track.codec);
        CHECK_EQUAL(cases[index].channels, track.channels);
        CHECK_EQUAL(cases[index].rate, track.rate);
        CHECK(track.packet_bytes <= ASF_PACKET_BYTES);
        CHECK(track.block_align <= WMA_PACKET_BYTES);
        CHECK(track.samples > 0);
        CHECK(track.packets > 0);
        m4a_file_free(&file);
    }
}

static void truncated_objects_and_nonstandard_codecs_are_refused(void)
{
    M4aFile file = m4a_file_load("wma/v2stereo.wma");
    AsfTrack track = {.rate = 7};
    AsfTrack before = track;
    FlacInput input = m4a_file_input(&file);
    for (uint32_t length = 0; length < 256; length++) {
        input.size = length;
        CHECK(!asf_track_probe(input, &track));
        CHECK(memcmp(&track, &before, sizeof track) == 0);
    }
    input = m4a_file_input(&file);
    for (uint32_t index = 0; index + 18 < file.size; index++) {
        if (file.bytes[index] == 0x61 && file.bytes[index + 1] == 1 && file.bytes[index + 2] == 2 &&
            file.bytes[index + 3] == 0) {
            file.bytes[index] = 0x62;
            CHECK(!asf_track_probe(input, &track));
            file.bytes[index] = 0x63;
            CHECK(!asf_track_probe(input, &track));
            file.bytes[index] = 0x0a;
            file.bytes[index + 1] = 0;
            CHECK(!asf_track_probe(input, &track));
            break;
        }
    }
    m4a_file_free(&file);
}

static void compressed_payloads_keep_their_presentation_timestamp(void)
{
    static AsfStream stream;
    M4aFile file = m4a_file_load("wma/v1compressed.wma");
    AsfTrack track;
    CHECK(asf_track_probe(m4a_file_input(&file), &track));
    uint32_t timestamp = track.preroll_ms + 256;
    uint32_t offset = track.data_offset + 15;
    CHECK(offset + 4 < file.size);
    for (uint32_t byte = 0; byte < 4; byte++) {
        file.bytes[offset + byte] = (uint8_t)(timestamp >> (byte * 8));
    }
    CHECK(asf_stream_start(&stream, m4a_file_input(&file), &track));
    CHECK_EQUAL(4096, stream.first_sample);
    m4a_file_free(&file);
}

static uint8_t unstamped_packets[64 * 256];

static bool read_unstamped(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    (void)context;
    if (offset > sizeof unstamped_packets || length > sizeof unstamped_packets - offset) {
        return false;
    }
    memcpy(buffer, unstamped_packets + offset, length);
    return true;
}

static void seeking_without_object_timestamps_decodes_from_the_beginning(void)
{
    static AsfStream stream;
    memset(unstamped_packets, 0, sizeof unstamped_packets);
    for (uint32_t index = 0; index < 64; index++) {
        uint8_t *packet = unstamped_packets + index * 256;
        packet[0] = 0x82;
        packet[3] = 0x10;
        packet[4] = 0x5D;
        packet[5] = 108;
        uint32_t time = index * 32;
        for (uint32_t byte = 0; byte < 4; byte++) {
            packet[7 + byte] = (uint8_t)(time >> (byte * 8));
        }
        packet[11] = 32;
        packet[13] = 1;
        packet[14] = (uint8_t)index;
    }
    AsfTrack track = {.data_end = sizeof unstamped_packets,
                      .packets = 64,
                      .packet_bytes = 256,
                      .rate = 16000,
                      .bitrate = 32000,
                      .duration_ms = 2048,
                      .samples = 32256,
                      .preroll_ms = 3100,
                      .frame_samples = 512,
                      .codec = 0x160,
                      .channels = 1,
                      .block_align = 128,
                      .extra_bytes = 4,
                      .stream = 1,
                      .extra = {0, 0, 1, 0}};
    CHECK(asf_stream_start_at(&stream, (FlacInput){read_unstamped, NULL, sizeof unstamped_packets},
                              &track, 1300));
    CHECK_EQUAL(1, stream.packet_index);
    CHECK_EQUAL(0, stream.object_number);
    CHECK_EQUAL(0, stream.first_sample);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(compressed_payloads_keep_their_presentation_timestamp),
        TEST_CASE(seeking_without_object_timestamps_decodes_from_the_beginning),
        TEST_CASE(standard_v1_v2_are_probed_with_bounded_packets_and_playable_timing),
        TEST_CASE(truncated_objects_and_nonstandard_codecs_are_refused),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
