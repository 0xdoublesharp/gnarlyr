#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/bytes.h"
#include "device/core1_vorbis.h"
#include "device/ogg_stream.h"

enum {
    FILE_CAPACITY = 1048576
};

static uint8_t data[FILE_CAPACITY];
static uint32_t file_size;
static uint32_t fail_at;
static bool failing;
static uint64_t read_bytes;
static VorbisModes captured_modes;
static OggPackets audio_cursor;
static OggStream stream;
static Core1Vorbis vorbis;
static StreamControl control;
static int32_t samples[STREAM_SLOTS][FLAC_MAX_CHANNELS][FLAC_MAX_BLOCK];
static _Alignas(8) uint8_t ring[VORBIS_RING_BYTES + VORBIS_WORKSPACE_BYTES];

static bool read_file(void *context, uint32_t offset, void *out, uint32_t bytes)
{
    (void)context;
    read_bytes += bytes;
    if (offset > file_size || bytes > file_size - offset ||
        (failing && offset <= fail_at && fail_at - offset < bytes)) {
        return false;
    }
    memcpy(out, data + offset, bytes);
    return true;
}

static FlacInput input(void)
{
    return (FlacInput){read_file, NULL, file_size};
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
    file_size = (uint32_t)fread(data, 1, sizeof data, file);
    CHECK(feof(file));
    CHECK_EQUAL(0, fclose(file));
    failing = false;
    return true;
}

static void repair(const OggPage *page)
{
    uint32_t crc = 0;
    memset(data + page->offset + 22, 0, 4);
    for (uint32_t index = page->offset; index < page->next; index++) {
        crc ^= (uint32_t)data[index] << 24;
        for (uint32_t bit = 0; bit < 8; bit++) {
            crc = (crc << 1) ^ ((crc & 0x80000000u) != 0 ? 0x04C11DB7u : 0);
        }
    }
    bytes_put_le32(data + page->offset + 22, crc);
}

static void the_probe_reports_decoded_format_and_exact_final_sample_count(void)
{
    static const char *const NAMES[] = {"mono8", "stereo44", "seek44", "seek8"};
    static const uint32_t RATES[] = {8000, 44100, 44100, 8000};
    static const uint32_t COUNTS[] = {2000, 11025, 143325, 26000};
    for (uint32_t index = 0; index < 4; index++) {
        CHECK(load(NAMES[index]));
        OggTrack track = {0};
        CHECK(vorbis_track_probe(input(), &track));
        CHECK_EQUAL(RATES[index], track.rate);
        CHECK_EQUAL(COUNTS[index], track.samples);
        CHECK_EQUAL(index == 0 || index == 3 ? 1 : 2, track.channels);
        CHECK_EQUAL(42, track.serial);
        CHECK_EQUAL(file_size, track.file_bytes);
        CHECK(track.data_offset > 30 && track.data_offset < file_size);
        CHECK(track.max_block >= 256 && track.max_block <= 8192);
    }
}

static void compare_frame(FILE *reference, const Core1Block *block, uint32_t from,
                          uint32_t channels)
{
    for (uint32_t frame = from; frame < block->samples; frame++) {
        for (uint32_t channel = 0; channel < channels; channel++) {
            int low = fgetc(reference);
            int high = fgetc(reference);
            CHECK(low != EOF && high != EOF);
            int16_t expected = (int16_t)((uint32_t)low | (uint32_t)high << 8);
            int32_t delta = (block->channels[channel][frame] >> 8) - expected;
            CHECK(delta >= -2 && delta <= 2);
        }
    }
}

static bool normalize_track(OggTrack *track)
{
    static uint8_t packet[OGG_PACKET_BYTES];
    VorbisDecoder decoder;
    if (!vorbis_decoder_init(&decoder, ring + VORBIS_RING_BYTES, VORBIS_WORKSPACE_BYTES)) {
        CHECK(false);
        return false;
    }
    OggPackets reader;
    ogg_packets_init_first(&reader, input());
    for (uint32_t index = 0; index < 3; index++) {
        OggPacket packet_info;
        if (ogg_packets_next(&reader, packet, sizeof packet, &packet_info) != OGG_PACKET_READY) {
            CHECK(false);
            return false;
        }
        VorbisPacket header = {.span = {{packet, packet_info.bytes}, {NULL, 0}},
                               .granule = packet_info.granule,
                               .first = packet_info.first};
        if (!vorbis_decoder_header(&decoder, header)) {
            CHECK(false);
            return false;
        }
    }
    VorbisModes modes;
    if (!vorbis_decoder_modes(&decoder, &modes)) {
        CHECK(false);
        return false;
    }
    captured_modes = modes;
    audio_cursor = reader;
    CHECK(modes.count >= 1 && modes.count <= 64);
    CHECK_EQUAL(track->max_block, modes.long_block);
    OggPackets before = reader;
    if (!vorbis_track_origin(&reader, &modes, track)) {
        CHECK(false);
        return false;
    }
    CHECK(memcmp(&before, &reader, sizeof reader) == 0);
    return true;
}

static void compare_loaded_seek(const char *name, uint32_t target)
{
    OggTrack track;
    if (!vorbis_track_probe(input(), &track)) {
        CHECK(false);
        return;
    }
    if (!normalize_track(&track)) {
        return;
    }
    uint32_t clamped = target < track.samples ? target : track.samples;
    CHECK(ogg_stream_seek(&stream, input(), &track, target));
    control = (StreamControl){.generation = 1};
    StreamRegion region = {
        .control = &control, .samples = samples, .ring = ring, .ring_bytes = VORBIS_RING_BYTES};
    Core1Decoder decoder = core1_vorbis_decoder(&vorbis, region);
    uint32_t first = stream.seek_offset == 0 ? 0 : clamped;
    FlacInput virtual_input = {ogg_stream_read, &stream, OGG_VIRTUAL_END};
    bool starved = false;
    decoder.start(decoder.context, virtual_input, (FlacInfo){0}, first, &starved);
    char path[512];
    (void)snprintf(path, sizeof path, "%s/vorbis/%s.pcm", ECHO_FIXTURE_DIR, name);
    FILE *reference = fopen(path, "rb");
    CHECK(reference != NULL);
    if (reference == NULL) {
        return;
    }
    CHECK_EQUAL(0, fseek(reference, (long)(clamped * track.channels * 2), SEEK_SET));
    uint32_t next_sample = clamped;
    Core1DecodeResult result = CORE1_FAILED;
    for (uint32_t count = 0; count < 2000; count++) {
        Core1Block block;
        result = decoder.decode(decoder.context, &block);
        if (result != CORE1_DECODED) {
            break;
        }
        if (block.samples == 0 || block.first_sample + block.samples <= clamped) {
            continue;
        }
        uint32_t from = block.first_sample < clamped ? clamped - block.first_sample : 0;
        CHECK_EQUAL(next_sample, block.first_sample + from);
        if (next_sample != block.first_sample + from) {
            break;
        }
        compare_frame(reference, &block, from, track.channels);
        next_sample += block.samples - from;
    }
    CHECK_EQUAL(1, control.vorbis_config_generation);
    CHECK_EQUAL(captured_modes.count, control.vorbis_modes.count);
    CHECK_EQUAL(captured_modes.short_block, control.vorbis_modes.short_block);
    CHECK_EQUAL(captured_modes.long_block, control.vorbis_modes.long_block);
    CHECK_EQUAL(captured_modes.long_modes[0], control.vorbis_modes.long_modes[0]);
    CHECK_EQUAL(captured_modes.long_modes[1], control.vorbis_modes.long_modes[1]);
    CHECK_EQUAL(CORE1_FINISHED, result);
    CHECK_EQUAL(track.samples, next_sample);
    CHECK_EQUAL(EOF, fgetc(reference));
    CHECK_EQUAL(0, fclose(reference));
}

static void compare_seek(const char *name, uint32_t target)
{
    if (load(name)) {
        compare_loaded_seek(name, target);
    }
}

static void stereo_seeks_match_reference_samples_at_pages_and_the_trimmed_endpoint(void)
{
    static const uint32_t TARGETS[] = {0,     1,      32000,  47807,  47808,  47809,     60000,
                                       99008, 100000, 143000, 143324, 143325, UINT32_MAX};
    for (uint32_t index = 0; index < sizeof TARGETS / sizeof TARGETS[0]; index++) {
        compare_seek("seek44", TARGETS[index]);
    }
}

static void single_page_speech_and_short_tracks_seek_from_the_true_stream_start(void)
{
    compare_seek("seek8", 0);
    compare_seek("seek8", 1000);
    compare_seek("seek8", 20000);
    compare_seek("seek8", 25999);
    compare_seek("seek8", 26000);
    compare_seek("stereo44", 5000);
}

static void later_seeks_choose_a_preceding_marked_page_and_keep_the_eos_preroll(void)
{
    CHECK(load("seek44"));
    OggTrack track;
    CHECK(vorbis_track_probe(input(), &track));
    uint32_t offset = UINT32_MAX;
    CHECK(ogg_track_seek(input(), &track, 100, &offset));
    CHECK_EQUAL(0, offset);
    CHECK(ogg_track_seek(input(), &track, 60000, &offset));
    CHECK_EQUAL(4311, offset);
    CHECK(ogg_track_seek(input(), &track, 143324, &offset));
    CHECK_EQUAL(8526, offset);
    OggPage page;
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input(), offset, &page));
    CHECK(page.granule <= 143324 && (page.flags & 4) == 0);
}

static void failed_or_invalid_probes_leave_the_track_and_seek_result_untouched(void)
{
    CHECK(load("seek44"));
    OggTrack track = {.rate = 77};
    OggTrack before = track;
    failing = true;
    fail_at = file_size - 1;
    CHECK(!vorbis_track_probe(input(), &track));
    CHECK(memcmp(&before, &track, sizeof track) == 0);
    failing = false;
    CHECK(vorbis_track_probe(input(), &track));
    uint32_t offset = 99;
    failing = true;
    fail_at = 9000;
    CHECK(!ogg_track_seek(input(), &track, 143000, &offset));
    CHECK_EQUAL(99, offset);
    failing = false;
    FlacInput short_input = input();
    short_input.size--;
    CHECK(!ogg_track_seek(short_input, &track, 143000, &offset));
    CHECK_EQUAL(99, offset);
    OggPage first;
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input(), 0, &first));
    data[first.body + 12] = 0;
    data[first.body + 13] = 0;
    data[first.body + 14] = 0;
    data[first.body + 15] = 0;
    repair(&first);
    before = track;
    CHECK(!vorbis_track_probe(input(), &track));
    CHECK(memcmp(&before, &track, sizeof track) == 0);
}

static void seek_rejects_each_invalid_public_argument(void)
{
    CHECK(load("seek44"));
    OggTrack track;
    CHECK(vorbis_track_probe(input(), &track));
    uint32_t offset = 99;
    CHECK(!ogg_track_seek(input(), NULL, 0, &offset));
    CHECK(!ogg_track_seek(input(), &track, 0, NULL));

    OggTrack invalid = track;
    invalid.samples = 0;
    CHECK(!ogg_track_seek(input(), &invalid, 0, &offset));
    FlacInput unreadable = input();
    unreadable.read = NULL;
    CHECK(!ogg_track_seek(unreadable, &track, 0, &offset));
    invalid = track;
    invalid.file_bytes--;
    CHECK(!ogg_track_seek(input(), &invalid, 0, &offset));
    invalid = track;
    invalid.data_offset = file_size;
    CHECK(!ogg_track_seek(input(), &invalid, 0, &offset));
    invalid = track;
    invalid.origin = UINT32_MAX;
    CHECK(!ogg_track_seek(input(), &invalid, 1, &offset));
    CHECK_EQUAL(99, offset);
}

static void wrong_serial_bad_eos_and_unrepresentable_granules_are_refused(void)
{
    for (uint32_t variant = 0; variant < 4; variant++) {
        CHECK(load("seek44"));
        OggPage last;
        CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input(), 12752, &last));
        if (variant == 0) {
            bytes_put_le32(data + last.offset + 14, 43);
        } else if (variant == 1) {
            data[last.offset + 5] &= (uint8_t)~4;
        } else if (variant == 2) {
            bytes_put_le32(data + last.offset + 10, 1);
        } else {
            memset(data + last.offset + 6, 255, 8);
        }
        repair(&last);
        OggTrack track = {.rate = 77};
        OggTrack before = track;
        CHECK(!vorbis_track_probe(input(), &track));
        CHECK(memcmp(&before, &track, sizeof track) == 0);
    }
}

static void distant_seeks_read_a_bounded_fraction_of_a_large_file(void)
{
    if (!load("seek44")) {
        return;
    }
    OggPage page;
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input(), 4311, &page));
    uint32_t page_size = page.next - page.offset;
    uint32_t first = page.offset;
    CHECK(first + 200 * page_size <= sizeof data);
    if (first + 200 * page_size > sizeof data) {
        return;
    }
    for (uint32_t index = 0; index < 200; index++) {
        uint32_t at = first + index * page_size;
        if (index != 0) {
            memcpy(data + at, data + first, page_size);
        }
        bytes_put_le32(data + at + 18, 2 + index);
        bytes_put_le32(data + at + 6, (index + 1) * 47808);
        bytes_put_le32(data + at + 10, 0);
        data[at + 5] = index == 199 ? 4 : 0;
        OggPage changed = {.offset = at, .next = at + page_size};
        repair(&changed);
    }
    file_size = first + 200 * page_size;
    OggTrack track;
    CHECK(vorbis_track_probe(input(), &track));
    CHECK_EQUAL(200 * 47808, track.samples);
    uint32_t offset = 0;
    read_bytes = 0;
    CHECK(ogg_track_seek(input(), &track, track.samples - 1, &offset));
    CHECK(offset > file_size / 2);
    CHECK(read_bytes < file_size / 2);
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input(), offset, &page));
    CHECK_EQUAL(198 * 47808 + 47808, page.granule);
    CHECK((page.flags & 4) == 0);
}

static void an_empty_timestamp_page_does_not_replace_the_decodable_seek_anchor(void)
{
    if (!load("seek44")) {
        return;
    }
    uint32_t at = 8526;
    memmove(data + at + 27, data + at, file_size - at);
    file_size += 27;
    memset(data + at, 0, 27);
    memcpy(data + at, "OggS", 4);
    bytes_put_le32(data + at + 6, 47808);
    bytes_put_le32(data + at + 14, 42);
    bytes_put_le32(data + at + 18, 3);
    OggPage page = {.offset = at, .next = at + 27};
    repair(&page);
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input(), at + 27, &page));
    bytes_put_le32(data + page.offset + 18, 4);
    repair(&page);
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input(), page.next, &page));
    bytes_put_le32(data + page.offset + 18, 5);
    repair(&page);
    OggTrack track;
    CHECK(vorbis_track_probe(input(), &track));
    uint32_t offset = 0;
    CHECK(ogg_track_seek(input(), &track, 60000, &offset));
    CHECK_EQUAL(4311, offset);
    if (offset != 4311) {
        return;
    }
    compare_loaded_seek("seek44", 60000);
}

static void shift_origin(uint32_t delta)
{
    OggPage page;
    uint32_t offset = 0;
    while (offset < file_size) {
        CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input(), offset, &page));
        if (page.granule > 0 && page.granule != UINT64_MAX) {
            uint64_t granule = page.granule + delta;
            bytes_put_le32(data + offset + 6, (uint32_t)granule);
            bytes_put_le32(data + offset + 10, (uint32_t)(granule >> 32));
            repair(&page);
        }
        offset = page.next;
    }
}

static void nonzero_origins_keep_duration_samples_and_seek_positions_on_the_same_timeline(void)
{
    static const uint32_t TARGETS[] = {0, 1, 47808, 60000, 100000, 143324, 143325};
    for (uint32_t index = 0; index < sizeof TARGETS / sizeof TARGETS[0]; index++) {
        CHECK(load("seek44"));
        shift_origin(500);
        OggTrack track;
        CHECK(vorbis_track_probe(input(), &track));
        CHECK_EQUAL(143825, track.samples);
        if (!normalize_track(&track)) {
            return;
        }
        CHECK_EQUAL(500, track.origin);
        CHECK_EQUAL(143325, track.samples);
        CHECK_EQUAL(143825, track.end_granule);
        compare_loaded_seek("seek44", TARGETS[index]);
    }
}

static void mode_snapshots_require_a_ready_decoder_and_preserve_the_output_on_failure(void)
{
    VorbisDecoder decoder = {0};
    VorbisModes modes = {.count = 99};
    VorbisModes before = modes;
    CHECK(!vorbis_decoder_modes(&decoder, &modes));
    CHECK(memcmp(&before, &modes, sizeof modes) == 0);
    CHECK(!vorbis_decoder_modes(&decoder, NULL));
}

static void header_only_prefill_stops_before_audio_and_origin_failure_keeps_the_track_intact(void)
{
    CHECK(load("seek44"));
    OggTrack track;
    CHECK(vorbis_track_probe(input(), &track));
    CHECK(normalize_track(&track));
    CHECK(ogg_stream_start(&stream, input()));
    uint8_t head[64];
    for (uint32_t position = 0; position < track.header_bytes;) {
        uint32_t count = track.header_bytes - position;
        count = count < sizeof head ? count : (uint32_t)sizeof head;
        CHECK(ogg_stream_read(&stream, position, head, count));
        position += count;
    }
    CHECK_EQUAL(3, stream.headers);
    CHECK_EQUAL(track.data_offset, stream.packets.body);
    CHECK_EQUAL(track.header_bytes, stream.position);
    OggTrack before = track;
    VorbisModes bad = captured_modes;
    bad.count = 65;
    CHECK(!vorbis_track_origin(&audio_cursor, &bad, &track));
    CHECK(memcmp(&before, &track, sizeof track) == 0);
    bad = captured_modes;
    bad.short_block++;
    CHECK(!vorbis_track_origin(&audio_cursor, &bad, &track));
    CHECK(memcmp(&before, &track, sizeof track) == 0);
    failing = true;
    fail_at = 8525;
    CHECK(!vorbis_track_origin(&audio_cursor, &captured_modes, &track));
    CHECK(memcmp(&before, &track, sizeof track) == 0);
    failing = false;
    OggPage page;
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input(), 4311, &page));
    data[page.body] |= 1;
    repair(&page);
    CHECK(!vorbis_track_origin(&audio_cursor, &captured_modes, &track));
    CHECK(memcmp(&before, &track, sizeof track) == 0);
}

static void cropped_first_pages_match_the_independent_reference_before_and_after_seeking(void)
{
    static const uint32_t TARGETS[] = {0, 1, 47308, 60000, 142824, 142825};
    for (uint32_t index = 0; index < sizeof TARGETS / sizeof TARGETS[0]; index++) {
        compare_seek("cropped44", TARGETS[index]);
    }
}

static void chained_and_multiplexed_files_play_and_seek_only_the_first_vorbis_link(void)
{
    static const char *const NAMES[] = {"chained44", "multiplexed44"};
    static const char *const REFERENCES[] = {"stereo44", "seek44"};
    for (uint32_t index = 0; index < 2; index++) {
        CHECK(load(NAMES[index]));
        OggTrack track;
        CHECK(vorbis_track_probe(input(), &track));
        compare_loaded_seek(REFERENCES[index], 0);
        compare_loaded_seek(REFERENCES[index], 5000);
        compare_loaded_seek(REFERENCES[index], index == 0 ? 11025 : 100000);
    }
    CHECK(load("chained44"));
    uint32_t original = file_size;
    memset(data + original, 0, 3 * 65307);
    file_size += 3 * 65307;
    compare_loaded_seek("stereo44", 5000);
    CHECK(load("stereo44"));
    original = file_size;
    for (uint32_t index = 1; index < 50; index++) {
        memcpy(data + index * original, data, original);
    }
    file_size = 50 * original;
    compare_loaded_seek("stereo44", 5000);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(chained_and_multiplexed_files_play_and_seek_only_the_first_vorbis_link),
        TEST_CASE(cropped_first_pages_match_the_independent_reference_before_and_after_seeking),
        TEST_CASE(header_only_prefill_stops_before_audio_and_origin_failure_keeps_the_track_intact),
        TEST_CASE(nonzero_origins_keep_duration_samples_and_seek_positions_on_the_same_timeline),
        TEST_CASE(mode_snapshots_require_a_ready_decoder_and_preserve_the_output_on_failure),
        TEST_CASE(an_empty_timestamp_page_does_not_replace_the_decodable_seek_anchor),
        TEST_CASE(distant_seeks_read_a_bounded_fraction_of_a_large_file),
        TEST_CASE(the_probe_reports_decoded_format_and_exact_final_sample_count),
        TEST_CASE(stereo_seeks_match_reference_samples_at_pages_and_the_trimmed_endpoint),
        TEST_CASE(single_page_speech_and_short_tracks_seek_from_the_true_stream_start),
        TEST_CASE(later_seeks_choose_a_preceding_marked_page_and_keep_the_eos_preroll),
        TEST_CASE(failed_or_invalid_probes_leave_the_track_and_seek_result_untouched),
        TEST_CASE(seek_rejects_each_invalid_public_argument),
        TEST_CASE(wrong_serial_bad_eos_and_unrepresentable_granules_are_refused),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
