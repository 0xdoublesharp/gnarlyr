#include <stdio.h>
#include <string.h>

#include "check.h"
#include "device/ogg_stream.h"
#include "device/opus_track.h"

static uint8_t bytes[65536];
static uint32_t file_size;
static OggStream stream;

static bool read_file(void *context, uint32_t offset, void *out, uint32_t count)
{
    (void)context;
    if (offset > file_size || count > file_size - offset) {
        return false;
    }
    memcpy(out, bytes + offset, count);
    return true;
}

static FlacInput load(const char *name)
{
    char path[512];
    (void)snprintf(path, sizeof path, "%s/opus/%s.opus", ECHO_FIXTURE_DIR, name);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    file_size = file == NULL ? 0 : (uint32_t)fread(bytes, 1, sizeof bytes, file);
    if (file != NULL) {
        CHECK(feof(file));
        CHECK_EQUAL(0, fclose(file));
    }
    return (FlacInput){read_file, NULL, file_size};
}

static uint32_t little32(const uint8_t *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static void repair(uint32_t offset)
{
    uint32_t end = offset + 27 + bytes[offset + 26];
    for (uint32_t lace = 0; lace < bytes[offset + 26]; lace++) {
        end += bytes[offset + 27 + lace];
    }
    memset(bytes + offset + 22, 0, 4);
    uint32_t crc = 0;
    for (uint32_t index = offset; index < end; index++) {
        crc ^= (uint32_t)bytes[index] << 24;
        for (uint32_t bit = 0; bit < 8; bit++) {
            crc = (crc << 1) ^ ((crc & 0x80000000u) != 0 ? 0x04C11DB7u : 0);
        }
    }
    for (uint32_t index = 0; index < 4; index++) {
        bytes[offset + 22 + index] = (uint8_t)(crc >> (8 * index));
    }
}

static void set_granule(uint32_t offset, uint64_t granule)
{
    for (uint32_t index = 0; index < 8; index++) {
        bytes[offset + 6 + index] = (uint8_t)(granule >> (8 * index));
    }
    repair(offset);
}

static uint32_t first_audio_page(void)
{
    uint32_t tags = 27 + bytes[26] + bytes[27];
    uint32_t audio = tags + 27 + bytes[tags + 26];
    for (uint32_t lace = 0; lace < bytes[tags + 26]; lace++) {
        audio += bytes[tags + 27 + lace];
    }
    return audio;
}

static void toc_bytes_give_every_standard_packet_duration(void)
{
    static const struct {
        uint8_t toc[2];
        uint32_t bytes;
        uint32_t frames;
    } cases[] = {
        {{0x00, 0}, 1, 480},  {{0x08, 0}, 1, 960},   {{0x10, 0}, 1, 1920}, {{0x18, 0}, 1, 2880},
        {{0x60, 0}, 1, 480},  {{0x68, 0}, 1, 960},   {{0x80, 0}, 1, 120},  {{0x88, 0}, 1, 240},
        {{0x90, 0}, 1, 480},  {{0xF8, 0}, 1, 960},   {{0xF9, 0}, 1, 1920}, {{0xFA, 0}, 1, 1920},
        {{0xFB, 6}, 2, 5760}, {{0x83, 48}, 2, 5760}, {{0x83, 49}, 2, 0},   {{0xFB, 0}, 2, 0},
        {{0xFB, 6}, 1, 0},    {{0x1B, 2}, 2, 5760},  {{0x1B, 3}, 2, 0},    {{0xFB, 0xC6}, 2, 5760},
    };
    for (uint32_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        CHECK_EQUAL(cases[index].frames, opus_track_frames(cases[index].toc, cases[index].bytes));
    }
}

static void probes_report_pre_skip_trimmed_lengths_and_origins(void)
{
    OggTrack track;
    CHECK(opus_track_probe(load("stereo48"), &track));
    CHECK(track.opus);
    CHECK_EQUAL(48000, track.rate);
    CHECK_EQUAL(2, track.channels);
    CHECK_EQUAL(312, track.pre_skip);
    CHECK_EQUAL(0, track.origin);
    CHECK_EQUAL(96000, track.samples);
    CHECK_EQUAL(96312, track.end_granule);
    CHECK_EQUAL(16 + 19, track.header_bytes);
    CHECK_EQUAL(file_size, track.file_bytes);
    CHECK(track.data_offset > 0 && track.data_offset < file_size);
    CHECK(opus_track_probe(load("origin48"), &track));
    CHECK_EQUAL(4800, track.origin);
    CHECK_EQUAL(96000, track.samples);
    CHECK(opus_track_probe(load("speech16"), &track));
    CHECK_EQUAL(1, track.channels);
    CHECK_EQUAL(48000, track.samples);
    CHECK(opus_track_probe(load("long120"), &track));
    CHECK_EQUAL(48000, track.samples);
}

static void invalid_headers_and_streams_are_refused_without_changing_the_track(void)
{
    static const struct {
        uint32_t at;
        uint8_t value;
    } changes[] = {{28 + 8, 16}, {28 + 9, 0}, {28 + 9, 3}, {28 + 18, 1}, {28 + 4, 'X'}};
    OggTrack track = {.rate = 7};
    OggTrack before = track;
    for (uint32_t index = 0; index < sizeof changes / sizeof changes[0]; index++) {
        load("speech16");
        bytes[changes[index].at] = changes[index].value;
        repair(0);
        CHECK(!opus_track_probe((FlacInput){read_file, NULL, file_size}, &track));
        CHECK(memcmp(&before, &track, sizeof track) == 0);
    }
    load("speech16");
    bytes[30] ^= 1;
    CHECK(!opus_track_probe((FlacInput){read_file, NULL, file_size}, &track));
    load("speech16");
    uint32_t tags = 27 + bytes[26] + bytes[27];
    bytes[tags + 27 + bytes[tags + 26] + 4] = 'X';
    repair(tags);
    CHECK(!opus_track_probe((FlacInput){read_file, NULL, file_size}, &track));
    load("speech16");
    set_granule(first_audio_page(), 0);
    set_granule(file_size - 56, 0);
    CHECK(!opus_track_probe((FlacInput){read_file, NULL, file_size}, &track));
    load("stereo48");
    file_size -= 4000;
    CHECK(!opus_track_probe((FlacInput){read_file, NULL, file_size}, &track));
    CHECK(memcmp(&before, &track, sizeof track) == 0);
    CHECK(!opus_track_probe(load("stereo48"), NULL));
}

static void early_granules_need_an_end_of_stream_page_and_a_positive_length(void)
{
    OggTrack track;
    FlacInput input = load("speech16");
    uint32_t audio = first_audio_page();
    CHECK_EQUAL(0, bytes[audio + 5] & 4);
    CHECK_EQUAL(48000, little32(bytes + audio + 6));
    set_granule(audio, 47999);
    CHECK(!opus_track_probe(input, &track));
    input = load("long120");
    audio = first_audio_page();
    CHECK_EQUAL(4, bytes[audio + 5] & 4);
    CHECK(opus_track_probe(input, &track));
    CHECK_EQUAL(0, track.origin);
    CHECK_EQUAL(48000, track.samples);
    set_granule(audio, 272);
    CHECK(!opus_track_probe(input, &track));
    set_granule(audio, 312);
    CHECK(!opus_track_probe(input, &track));
    set_granule(audio, 313);
    CHECK(opus_track_probe(input, &track));
    CHECK_EQUAL(1, track.samples);
    set_granule(audio, (uint64_t)UINT32_MAX + 1);
    CHECK(!opus_track_probe(input, &track));
}

static void seeks_choose_a_page_at_least_the_pre_roll_before_the_target(void)
{
    FlacInput input = load("stereo48");
    OggTrack track;
    CHECK(opus_track_probe(input, &track));
    uint32_t offset = 1;
    CHECK(opus_track_seek(input, &track, 0, &offset));
    CHECK_EQUAL(0, offset);
    CHECK(opus_track_seek(input, &track, OPUS_PRE_ROLL - 313, &offset));
    CHECK_EQUAL(0, offset);
    CHECK(!opus_track_seek(input, &track, 96001, &offset));
    static const uint32_t targets[] = {4000, 20000, 48000, 95999, 96000};
    for (uint32_t index = 0; index < sizeof targets / sizeof targets[0]; index++) {
        CHECK(opus_track_seek(input, &track, targets[index], &offset));
        uint32_t limit = targets[index] + track.pre_skip - OPUS_PRE_ROLL;
        OggPage page;
        if (offset == 0) {
            CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input, track.data_offset, &page));
            CHECK(page.granule > limit);
            continue;
        }
        CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input, offset, &page));
        CHECK(page.granule <= limit);
        CHECK_EQUAL(0, page.flags & 4);
        OggPage next;
        CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input, page.next, &next));
        CHECK(next.granule > limit || (next.flags & 4) != 0);
    }
}

static uint32_t read_record(uint32_t *position, uint8_t *out, uint32_t *flags, uint64_t *granule)
{
    uint8_t head[OGG_RECORD_BYTES];
    CHECK(ogg_stream_read(&stream, *position, head, sizeof head));
    uint32_t size = little32(head);
    *flags = little32(head + 4);
    *granule = (uint64_t)little32(head + 8) | (uint64_t)little32(head + 12) << 32;
    CHECK(size <= OGG_PACKET_BYTES);
    CHECK(ogg_stream_read(&stream, *position + sizeof head, out, size));
    *position += (uint32_t)sizeof head + size;
    return size;
}

static void the_record_stream_sends_the_head_alone_then_origin_and_audio(void)
{
    static uint8_t body[OGG_PACKET_BYTES];
    FlacInput input = load("origin48");
    OggTrack track;
    CHECK(opus_track_probe(input, &track));
    CHECK(ogg_stream_resume(&stream, input, &track, 0));
    uint32_t position = 0;
    uint32_t flags;
    uint64_t granule;
    CHECK_EQUAL(19, read_record(&position, body, &flags, &granule));
    CHECK_EQUAL(OGG_RECORD_FIRST, flags);
    CHECK(memcmp(body, "OpusHead", 8) == 0);
    CHECK_EQUAL(track.header_bytes, position);
    CHECK_EQUAL(0, read_record(&position, body, &flags, &granule));
    CHECK_EQUAL(OGG_RECORD_ORIGIN, flags);
    CHECK_EQUAL(4800, granule);
    uint32_t packets = 0;
    uint64_t last = 0;
    for (;;) {
        uint32_t size = read_record(&position, body, &flags, &granule);
        if ((flags & OGG_RECORD_END) != 0) {
            CHECK_EQUAL(0, size);
            break;
        }
        CHECK(size > 0);
        CHECK_EQUAL(960, opus_track_frames(body, size));
        last = granule != UINT64_MAX ? granule : last;
        packets++;
    }
    CHECK_EQUAL(101, packets);
    CHECK_EQUAL(track.end_granule, last);
    uint8_t padding[8] = {1};
    CHECK(ogg_stream_read(&stream, position, padding, sizeof padding));
    CHECK_EQUAL(0, padding[0]);
}

static void a_resumed_record_stream_replays_the_head_then_the_chosen_page(void)
{
    static uint8_t body[OGG_PACKET_BYTES];
    FlacInput input = load("stereo48");
    OggTrack track;
    CHECK(opus_track_probe(input, &track));
    uint32_t offset;
    CHECK(opus_track_seek(input, &track, 48000, &offset));
    CHECK(offset > track.data_offset);
    CHECK(ogg_stream_resume(&stream, input, &track, offset));
    uint32_t position = 0;
    uint32_t flags;
    uint64_t granule;
    CHECK_EQUAL(19, read_record(&position, body, &flags, &granule));
    uint32_t frames = 0;
    do {
        uint32_t size = read_record(&position, body, &flags, &granule);
        frames += opus_track_frames(body, size);
    } while (granule == UINT64_MAX);
    OggPage page;
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(input, offset, &page));
    CHECK_EQUAL(page.granule, granule);
    CHECK(granule - frames <= 48000 + track.pre_skip - OPUS_PRE_ROLL);
}

static void a_damaged_audio_page_is_a_sticky_stream_error(void)
{
    static uint8_t body[OGG_PACKET_BYTES];
    FlacInput input = load("speech16");
    OggTrack track;
    CHECK(opus_track_probe(input, &track));
    bytes[file_size - 10] ^= 0x55;
    CHECK(ogg_stream_resume(&stream, input, &track, 0));
    uint32_t position = 0;
    uint32_t flags;
    uint64_t granule;
    CHECK_EQUAL(19, read_record(&position, body, &flags, &granule));
    uint8_t head[OGG_RECORD_BYTES];
    uint32_t records = 0;
    while (records < 100 && ogg_stream_read(&stream, position, head, sizeof head)) {
        CHECK_EQUAL(0, little32(head + 4) & OGG_RECORD_END);
        uint32_t size = little32(head);
        CHECK(ogg_stream_read(&stream, position + sizeof head, body, size));
        position += (uint32_t)sizeof head + size;
        records++;
    }
    CHECK_EQUAL(50, records);
    CHECK(stream.failed);
    CHECK(!ogg_stream_read(&stream, position, head, sizeof head));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(toc_bytes_give_every_standard_packet_duration),
        TEST_CASE(probes_report_pre_skip_trimmed_lengths_and_origins),
        TEST_CASE(invalid_headers_and_streams_are_refused_without_changing_the_track),
        TEST_CASE(early_granules_need_an_end_of_stream_page_and_a_positive_length),
        TEST_CASE(seeks_choose_a_page_at_least_the_pre_roll_before_the_target),
        TEST_CASE(the_record_stream_sends_the_head_alone_then_origin_and_audio),
        TEST_CASE(a_resumed_record_stream_replays_the_head_then_the_chosen_page),
        TEST_CASE(a_damaged_audio_page_is_a_sticky_stream_error),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
