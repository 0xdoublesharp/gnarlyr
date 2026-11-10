#include <stdio.h>
#include <string.h>

#include "check.h"
#include "device/vorbis_decoder.h"
#include "device/vorbis_stream.h"

enum {
    FILE_CAPACITY = 196608
};

typedef struct File {
    uint8_t bytes[FILE_CAPACITY];
    uint32_t size;
    uint32_t fail_at;
    bool failing;
} File;

static File original;
static File changed;
static VorbisStream stream;
static uint8_t packet[VORBIS_PACKET_BYTES];
static uint8_t reference_packet[VORBIS_PACKET_BYTES];
static uint8_t metadata[70000];
static _Alignas(8) uint8_t workspace[VORBIS_WORKSPACE_BYTES];
static int32_t pcm_data[2][VORBIS_MAX_FRAMES];
static VorbisDecoder decoder;

static bool read_file(void *context, uint32_t offset, void *out, uint32_t length)
{
    File *file = context;
    if (offset > file->size || length > file->size - offset ||
        (file->failing && offset <= file->fail_at && file->fail_at - offset < length)) {
        return false;
    }
    memcpy(out, file->bytes + offset, length);
    return true;
}

static FlacInput input(File *file)
{
    return (FlacInput){read_file, file, file->size};
}

static bool load(const char *name)
{
    char path[512];
    (void)snprintf(path, sizeof path, "%s/vorbis/%s.ogg", ECHO_FIXTURE_DIR, name);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file == NULL) {
        return false;
    }
    original = (File){0};
    original.size = (uint32_t)fread(original.bytes, 1, sizeof original.bytes, file);
    CHECK(feof(file));
    CHECK_EQUAL(0, fclose(file));
    return true;
}

static uint32_t number(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static void put32(uint8_t *bytes, uint32_t value)
{
    for (uint32_t index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

static void finish_page(uint32_t start)
{
    uint32_t crc = 0;
    for (uint32_t index = start; index < changed.size; index++) {
        crc ^= (uint32_t)changed.bytes[index] << 24;
        for (uint32_t bit = 0; bit < 8; bit++) {
            crc = (crc << 1) ^ ((crc & 0x80000000u) != 0 ? 0x04C11DB7u : 0);
        }
    }
    put32(changed.bytes + start + 22, crc);
}

static void append_packet(const uint8_t *bytes, OggPacket info, uint32_t *sequence)
{
    uint32_t done = 0;
    for (;;) {
        uint32_t start = changed.size;
        uint32_t left = info.bytes - done;
        uint32_t body = left < 65025 ? left : 65025;
        uint32_t laces = body / 255 + (body < 65025 ? 1u : 0u);
        bool terminal = body < 65025;
        uint8_t *page = changed.bytes + start;
        memcpy(page, "OggS", 4);
        page[5] = (uint8_t)((done == 0 && info.first ? 2 : 0) | (done != 0 ? 1 : 0) |
                            (terminal && info.last ? 4 : 0));
        uint64_t granule = terminal ? info.granule : UINT64_MAX;
        put32(page + 6, (uint32_t)granule);
        put32(page + 10, (uint32_t)(granule >> 32));
        put32(page + 14, 42);
        put32(page + 18, (*sequence)++);
        page[26] = (uint8_t)laces;
        for (uint32_t index = 0; index < laces; index++) {
            page[27 + index] = (uint8_t)(terminal && index + 1 == laces ? body % 255 : 255);
        }
        memcpy(page + 27 + laces, bytes + done, body);
        changed.size += 27 + laces + body;
        CHECK(changed.size <= sizeof changed.bytes);
        finish_page(start);
        done += body;
        if (terminal) {
            return;
        }
    }
}

static void replace_packet(uint32_t which, uint32_t bytes)
{
    changed = (File){0};
    OggPackets reader;
    ogg_packets_init(&reader, input(&original));
    uint32_t count = 0;
    uint32_t sequence = 0;
    OggPacket info;
    while (ogg_packets_next(&reader, reference_packet, sizeof reference_packet, &info) ==
           OGG_PACKET_READY) {
        const uint8_t *data = reference_packet;
        if (count++ == which) {
            info.bytes = bytes;
            data = metadata;
        }
        append_packet(data, info, &sequence);
    }
}

static bool next_record(uint32_t *position, VorbisPacket *out, uint32_t fragment)
{
    uint8_t head[VORBIS_RECORD_BYTES];
    for (uint32_t done = 0; done < sizeof head;) {
        uint32_t size = fragment < sizeof head - done ? fragment : (uint32_t)sizeof head - done;
        if (!vorbis_stream_read(&stream, *position, head + done, size)) {
            return false;
        }
        *position += size;
        done += size;
    }
    uint32_t bytes = number(head);
    uint32_t flags = number(head + 4);
    CHECK(bytes <= sizeof packet);
    if (bytes > sizeof packet || flags == VORBIS_RECORD_END) {
        return false;
    }
    for (uint32_t done = 0; done < bytes;) {
        uint32_t size = fragment < bytes - done ? fragment : bytes - done;
        if (!vorbis_stream_read(&stream, *position, packet + done, size)) {
            return false;
        }
        done += size;
        *position += size;
    }
    *out = (VorbisPacket){.span = {{packet, bytes}, {NULL, 0}},
                          .granule = (uint64_t)number(head + 8) | (uint64_t)number(head + 12) << 32,
                          .first = (flags & VORBIS_RECORD_FIRST) != 0,
                          .last = (flags & VORBIS_RECORD_LAST) != 0};
    return true;
}

static void compare_pcm(File *file, const char *name, uint32_t expected, uint32_t fragment)
{
    CHECK(vorbis_stream_start(&stream, input(file)));
    CHECK(vorbis_decoder_init(&decoder, workspace, sizeof workspace));
    char path[512];
    (void)snprintf(path, sizeof path, "%s/vorbis/%s.pcm", ECHO_FIXTURE_DIR, name);
    FILE *reference = fopen(path, "rb");
    CHECK(reference != NULL);
    if (reference == NULL) {
        return;
    }
    uint32_t position = 0;
    uint32_t frames = 0;
    uint32_t packets = 0;
    VorbisPacket record;
    while (next_record(&position, &record, fragment)) {
        if (packets++ < 3) {
            CHECK(vorbis_decoder_header(&decoder, record));
            if (packets == 2) {
                CHECK_EQUAL(16, record.span[0].size);
            }
            continue;
        }
        VorbisPcm pcm = {.channels = {pcm_data[0], pcm_data[1]}, .capacity = VORBIS_MAX_FRAMES};
        if (!vorbis_decoder_decode(&decoder, record, &pcm)) {
            CHECK(false);
            break;
        }
        for (uint32_t frame = 0; frame < pcm.frames; frame++) {
            for (uint32_t channel = 0; channel < decoder.channels; channel++) {
                int low = fgetc(reference);
                int high = fgetc(reference);
                CHECK(low != EOF && high != EOF);
                int16_t value = (int16_t)((uint32_t)low | (uint32_t)high << 8);
                int32_t delta = (pcm_data[channel][frame] >> 8) - value;
                CHECK(delta >= -2 && delta <= 2);
            }
        }
        frames += pcm.frames;
    }
    CHECK(!stream.failed);
    CHECK(stream.ended);
    CHECK_EQUAL(expected, frames);
    CHECK_EQUAL(EOF, fgetc(reference));
    CHECK_EQUAL(0, fclose(reference));
}

static void real_files_decode_through_fragmented_virtual_records(void)
{
    static const char *const NAMES[] = {"mono8", "stereo44", "stereo48", "low48"};
    static const uint32_t FRAMES[] = {2000, 11025, 12000, 12000};
    static const uint32_t FRAGMENTS[] = {1, 7, 16, 1024};
    for (uint32_t index = 0; index < 4; index++) {
        if (load(NAMES[index])) {
            compare_pcm(&original, NAMES[index], FRAMES[index], FRAGMENTS[index]);
        }
    }
}

static void large_embedded_metadata_does_not_change_audio_or_consume_decoder_workspace(void)
{
    if (!load("stereo44")) {
        return;
    }
    memset(metadata, 'A', sizeof metadata);
    metadata[0] = 3;
    memcpy(metadata + 1, "vorbis", 6);
    put32(metadata + 7, sizeof metadata - 16);
    put32(metadata + sizeof metadata - 5, 0);
    metadata[sizeof metadata - 1] = 1;
    replace_packet(1, sizeof metadata);
    compare_pcm(&changed, "stereo44", 11025, 13);
}

static void failed_storage_and_damaged_metadata_are_errors_rather_than_a_clean_end(void)
{
    if (!load("stereo44")) {
        return;
    }
    original.failing = true;
    original.fail_at = original.size - 1;
    CHECK(vorbis_stream_start(&stream, input(&original)));
    uint32_t position = 0;
    VorbisPacket record;
    while (next_record(&position, &record, 19)) {
    }
    CHECK(stream.failed);
    CHECK(!stream.ended);
    original.failing = false;
    CHECK(!vorbis_stream_read(&stream, stream.position, packet, 1));
    compare_pcm(&original, "stereo44", 11025, 19);
    original.bytes[original.size - 1] ^= 1;
    CHECK(vorbis_stream_start(&stream, input(&original)));
    position = 0;
    while (next_record(&position, &record, 19)) {
    }
    CHECK(stream.failed);
    CHECK(!stream.ended);
}

static void oversized_audio_is_refused_and_never_masquerades_as_end_of_stream(void)
{
    if (!load("stereo44")) {
        return;
    }
    memset(metadata, 0, sizeof metadata);
    replace_packet(3, sizeof metadata);
    CHECK(vorbis_stream_start(&stream, input(&changed)));
    uint32_t position = 0;
    VorbisPacket record;
    for (uint32_t index = 0; index < 3; index++) {
        CHECK(next_record(&position, &record, 23));
    }
    CHECK(!next_record(&position, &record, 23));
    CHECK(stream.failed);
    CHECK(!stream.ended);
}

static void incomplete_or_wrong_configuration_packets_do_not_end_successfully(void)
{
    if (!load("stereo44")) {
        return;
    }
    OggPackets reader;
    ogg_packets_init(&reader, input(&original));
    OggPacket info;
    CHECK_EQUAL(OGG_PACKET_READY,
                ogg_packets_next(&reader, reference_packet, sizeof reference_packet, &info));
    changed = (File){0};
    info.last = true;
    uint32_t sequence = 0;
    append_packet(reference_packet, info, &sequence);
    CHECK(vorbis_stream_start(&stream, input(&changed)));
    uint32_t position = 0;
    VorbisPacket record;
    CHECK(!next_record(&position, &record, 17));
    CHECK(stream.failed);
    CHECK(!stream.ended);
    memset(metadata, 0, 16);
    replace_packet(1, 16);
    CHECK(vorbis_stream_start(&stream, input(&changed)));
    position = 0;
    CHECK(next_record(&position, &record, 17));
    CHECK(!next_record(&position, &record, 17));
    CHECK(stream.failed);
}

static void invalid_requests_preserve_the_current_stream_and_end_padding_is_zero(void)
{
    if (!load("mono8")) {
        return;
    }
    CHECK(!vorbis_stream_start(&stream, (FlacInput){0}));
    CHECK(vorbis_stream_start(&stream, input(&original)));
    CHECK(!vorbis_stream_read(&stream, 1, packet, 1));
    CHECK(!vorbis_stream_read(&stream, 0, NULL, 1));
    CHECK(!vorbis_stream_read(&stream, 0, packet, UINT32_MAX));
    CHECK(!stream.failed);
    CHECK_EQUAL(0, stream.position);
    uint32_t position = 0;
    VorbisPacket record;
    while (next_record(&position, &record, 101)) {
    }
    CHECK(stream.ended && !stream.failed);
    memset(packet, 0xA5, 128);
    CHECK(vorbis_stream_read(&stream, position, packet, 127));
    for (uint32_t index = 0; index < 127; index++) {
        CHECK_EQUAL(0, packet[index]);
    }
    CHECK_EQUAL(0xA5, packet[127]);
    CHECK_EQUAL(position + 127, stream.position);
}

static void bulk_reads_cross_packet_boundaries_and_match_independent_record_framing(void)
{
    if (!load("stereo44")) {
        return;
    }
    OggPackets reader;
    ogg_packets_init(&reader, input(&original));
    changed = (File){0};
    uint32_t count = 0;
    OggPacket info;
    while (ogg_packets_next(&reader, reference_packet, sizeof reference_packet, &info) ==
           OGG_PACKET_READY) {
        if (count++ == 1) {
            static const uint8_t EMPTY[] = {3, 'v', 'o', 'r', 'b', 'i', 's', 0,
                                            0, 0,   0,   0,   0,   0,   0,   1};
            memcpy(reference_packet, EMPTY, sizeof EMPTY);
            info.bytes = sizeof EMPTY;
        }
        uint8_t *head = changed.bytes + changed.size;
        put32(head, info.bytes);
        put32(head + 4, (uint32_t)(info.first ? 1 : 0) | (uint32_t)(info.last ? 2 : 0));
        put32(head + 8, (uint32_t)info.granule);
        put32(head + 12, (uint32_t)(info.granule >> 32));
        memcpy(head + 16, reference_packet, info.bytes);
        changed.size += 16 + info.bytes;
    }
    uint8_t *end = changed.bytes + changed.size;
    put32(end + 4, VORBIS_RECORD_END);
    changed.size += VORBIS_RECORD_BYTES;
    static const uint32_t CHUNKS[] = {3, 17, 257, 4096};
    for (uint32_t index = 0; index < sizeof CHUNKS / sizeof CHUNKS[0]; index++) {
        CHECK(vorbis_stream_start(&stream, input(&original)));
        uint32_t position = 0;
        while (position < changed.size + 127) {
            uint32_t bytes = CHUNKS[index];
            CHECK(vorbis_stream_read(&stream, position, packet, bytes));
            CHECK(memcmp(packet, changed.bytes + position, bytes) == 0);
            CHECK_EQUAL(position + bytes, stream.position);
            position += bytes;
        }
        CHECK(stream.ended && !stream.failed);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(bulk_reads_cross_packet_boundaries_and_match_independent_record_framing),
        TEST_CASE(real_files_decode_through_fragmented_virtual_records),
        TEST_CASE(large_embedded_metadata_does_not_change_audio_or_consume_decoder_workspace),
        TEST_CASE(failed_storage_and_damaged_metadata_are_errors_rather_than_a_clean_end),
        TEST_CASE(oversized_audio_is_refused_and_never_masquerades_as_end_of_stream),
        TEST_CASE(incomplete_or_wrong_configuration_packets_do_not_end_successfully),
        TEST_CASE(invalid_requests_preserve_the_current_stream_and_end_padding_is_zero),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
