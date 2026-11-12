#include <stdio.h>
#include <string.h>

#include "check.h"
#include "codec/ogg_packet.h"
#include "core/bytes.h"

enum {
    FILE_CAPACITY = 131072,
    PACKET_CAPACITY = 65536
};

typedef struct File {
    uint8_t data[FILE_CAPACITY];
    uint32_t size;
    uint32_t fail_at;
    bool failing;
} File;

static File file;
static uint8_t output[PACKET_CAPACITY];

static uint32_t checksum(const uint8_t *bytes, uint32_t length)
{
    uint32_t crc = 0;
    for (uint32_t index = 0; index < length; index++) {
        uint8_t value = index >= 22 && index < 26 ? 0 : bytes[index];
        crc ^= (uint32_t)value << 24;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04C11DB7u : 0);
        }
    }
    return crc;
}

static void repair(uint32_t offset, uint32_t size)
{
    bytes_put_le32(file.data + offset + 22, checksum(file.data + offset, size));
}

static uint32_t page(uint8_t flags, uint32_t sequence, const uint8_t *laces, uint32_t count)
{
    uint32_t start = file.size;
    uint8_t *bytes = file.data + start;
    memcpy(bytes, "OggS", 4);
    bytes[4] = 0;
    bytes[5] = flags;
    memset(bytes + 6, 0xFF, 8);
    bytes_put_le32(bytes + 14, 29);
    bytes_put_le32(bytes + 18, sequence);
    bytes[26] = (uint8_t)count;
    if (count > 0) {
        memcpy(bytes + 27, laces, count);
    }
    uint32_t size = 27 + count;
    for (uint32_t index = 0; index < count; index++) {
        for (uint32_t byte = 0; byte < laces[index]; byte++) {
            bytes[size++] = (uint8_t)(index + byte);
        }
    }
    file.size += size;
    repair(start, size);
    return start;
}

static bool read_file(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    File *source = context;
    CHECK(length > 0);
    CHECK(buffer != NULL);
    CHECK(offset <= source->size && length <= source->size - offset);
    if (source->failing && offset <= source->fail_at && source->fail_at - offset < length) {
        return false;
    }
    memcpy(buffer, source->data + offset, length);
    return true;
}

static OggPackets reader(void)
{
    OggPackets result;
    ogg_packets_init(&result, (FlacInput){read_file, &file, file.size});
    return result;
}

static void reset(void)
{
    memset(&file, 0, sizeof file);
    memset(output, 0xA5, sizeof output);
}

static OggPacketResult next(OggPackets *stream, OggPacket *packet)
{
    return ogg_packets_next(stream, output, sizeof output, packet);
}

static void a_real_vorbis_file_preserves_its_headers_and_final_sample_position(void)
{
    reset();
    FILE *source = fopen(ECHO_FIXTURE_DIR "/tags/tagged.ogg", "rb");
    CHECK(source != NULL);
    if (source == NULL) {
        return;
    }
    file.size = (uint32_t)fread(file.data, 1, sizeof file.data, source);
    CHECK(feof(source));
    CHECK_EQUAL(0, fclose(source));
    OggPackets stream = reader();
    OggPacket packet = {0};
    uint32_t count = 0;
    OggPacketResult result;
    while ((result = next(&stream, &packet)) == OGG_PACKET_READY) {
        CHECK_EQUAL(count == 0, packet.first);
        if (count < 3) {
            CHECK_EQUAL(1 + 2 * count, output[0]);
            CHECK(memcmp(output + 1, "vorbis", 6) == 0);
        }
        count++;
    }
    CHECK_EQUAL(OGG_PACKET_END, result);
    CHECK_EQUAL(48, count);
    CHECK_EQUAL(44160, packet.granule);
    CHECK(packet.last);
}

static void an_input_without_a_read_callback_is_invalid(void)
{
    OggPackets stream;
    OggPacket packet = {0};
    ogg_packets_init(&stream, (FlacInput){.size = 1000});
    CHECK_EQUAL(OGG_PACKET_INVALID, next(&stream, &packet));
}

static void a_complete_stream_reports_packet_boundaries_and_the_final_granule(void)
{
    reset();
    static const uint8_t LACES[] = {3, 0, 5};
    page(6, 0, LACES, sizeof LACES);
    memset(file.data + 6, 0, 8);
    bytes_put_le32(file.data + 6, 12345);
    repair(0, file.size);
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(3, packet.bytes);
    CHECK(packet.first && !packet.last);
    CHECK_EQUAL(UINT64_MAX, packet.granule);
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(0, packet.bytes);
    CHECK(!packet.first && !packet.last);
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(5, packet.bytes);
    CHECK(!packet.first && packet.last);
    CHECK_EQUAL(12345, packet.granule);
    CHECK_EQUAL(2, output[0]);
    CHECK_EQUAL(OGG_PACKET_END, next(&stream, &packet));
}

static void a_packet_can_cross_multiple_continued_pages(void)
{
    reset();
    static const uint8_t LONG[] = {255};
    static const uint8_t END[] = {3};
    page(2, 0, LONG, 1);
    page(1, 1, LONG, 1);
    page(5, 2, END, 1);
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(513, packet.bytes);
    CHECK_EQUAL(254, output[254]);
    CHECK_EQUAL(0, output[255]);
    CHECK_EQUAL(2, output[512]);
    CHECK(packet.first && packet.last);
    CHECK_EQUAL(OGG_PACKET_END, next(&stream, &packet));
}

static void an_exact_multiple_of_255_ends_with_a_zero_lace(void)
{
    reset();
    static const uint8_t LACES[] = {255, 0, 1};
    page(6, 0, LACES, 3);
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(255, packet.bytes);
    CHECK(!packet.last);
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(1, packet.bytes);
    CHECK(packet.last);
}

static void only_the_last_completed_packet_owns_the_page_granule(void)
{
    reset();
    static const uint8_t LACES[] = {2, 255};
    static const uint8_t END[] = {2};
    page(2, 0, LACES, 2);
    memset(file.data + 6, 0, 8);
    bytes_put_le32(file.data + 6, 77);
    repair(0, file.size);
    page(5, 1, END, 1);
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(77, packet.granule);
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(257, packet.bytes);
}

static void empty_pages_do_not_invent_packets(void)
{
    reset();
    static const uint8_t LACES[] = {2};
    page(2, 0, NULL, 0);
    page(0, 1, LACES, 1);
    page(4, 2, NULL, 0);
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(2, packet.bytes);
    CHECK_EQUAL(OGG_PACKET_END, next(&stream, &packet));
}

static void a_maximum_size_page_and_its_terminal_lace_are_read_without_overrun(void)
{
    reset();
    uint8_t laces[255];
    memset(laces, 255, sizeof laces);
    static const uint8_t END[] = {0};
    page(2, 0, laces, sizeof laces);
    page(5, 1, END, 1);
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(65025, packet.bytes);
    CHECK_EQUAL(0xA5, output[65025]);
    CHECK(packet.first && packet.last);
    CHECK_EQUAL(OGG_PACKET_END, next(&stream, &packet));
}

static void null_buffers_only_accept_empty_packets(void)
{
    reset();
    static const uint8_t LACES[] = {0, 1};
    page(6, 0, LACES, 2);
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, ogg_packets_next(&stream, NULL, 0, &packet));
    CHECK_EQUAL(0, packet.bytes);
    CHECK_EQUAL(OGG_PACKET_TOO_LARGE, ogg_packets_next(&stream, NULL, 0, &packet));
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_packets_next(&stream, NULL, 1, &packet));
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_packets_next(&stream, output, sizeof output, NULL));
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(1, packet.bytes);
}

static void damaged_pages_are_rejected_before_emitting_audio(void)
{
    static const uint8_t LACES[] = {255, 45};
    for (uint32_t byte = 0; byte < 329; byte++) {
        reset();
        page(6, 0, LACES, 2);
        CHECK_EQUAL(329, file.size);
        file.data[byte] ^= 1;
        OggPackets stream = reader();
        OggPacket packet = {0};
        CHECK_EQUAL(OGG_PACKET_INVALID, next(&stream, &packet));
    }
}

static void truncated_pages_and_unfinished_packets_are_rejected(void)
{
    static const uint8_t LACES[] = {255, 5};
    for (uint32_t size = 0; size < 289; size++) {
        reset();
        page(6, 0, LACES, 2);
        file.size = size;
        OggPackets stream = reader();
        OggPacket packet = {0};
        CHECK_EQUAL(OGG_PACKET_INVALID, next(&stream, &packet));
    }
    reset();
    page(2, 0, LACES, 1);
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_INVALID, next(&stream, &packet));
}

static void failed_reads_leave_the_reader_ready_for_an_exact_retry(void)
{
    static const uint8_t LACES[] = {255};
    static const uint8_t END[] = {5};
    reset();
    page(2, 0, LACES, 1);
    page(5, 1, END, 1);
    for (uint32_t byte = 0; byte < file.size; byte++) {
        file.fail_at = byte;
        file.failing = true;
        OggPackets stream = reader();
        OggPackets before = stream;
        OggPacket packet = {.bytes = 99};
        CHECK_EQUAL(OGG_PACKET_WAIT, next(&stream, &packet));
        CHECK(memcmp(&stream, &before, sizeof stream) == 0);
        CHECK_EQUAL(99, packet.bytes);
        file.failing = false;
        CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
        CHECK_EQUAL(260, packet.bytes);
    }
}

static void packet_limits_leave_the_reader_and_buffer_bounds_intact(void)
{
    reset();
    static const uint8_t LACES[] = {255, 2};
    page(6, 0, LACES, 2);
    OggPackets stream = reader();
    OggPackets before = stream;
    OggPacket packet = {.bytes = 99};
    CHECK_EQUAL(OGG_PACKET_TOO_LARGE, ogg_packets_next(&stream, output, 256, &packet));
    CHECK_EQUAL(0xA5, output[256]);
    CHECK(memcmp(&before, &stream, sizeof stream) == 0);
    CHECK_EQUAL(99, packet.bytes);
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(257, packet.bytes);
}

static void malformed_stream_headers_are_rejected_even_with_valid_checksums(void)
{
    static const uint8_t LACES[] = {2};
    for (uint32_t variant = 0; variant < 6; variant++) {
        reset();
        page(6, 0, LACES, 1);
        if (variant == 0) {
            file.data[4] = 1;
        }
        if (variant == 1) {
            file.data[5] |= 8;
        }
        if (variant == 2) {
            file.data[5] &= (uint8_t)~2;
        }
        if (variant == 3) {
            file.data[5] |= 1;
        }
        if (variant == 4) {
            file.data[18] = 1;
        }
        if (variant == 5) {
            file.data[27] = 255;
        }
        repair(0, file.size);
        OggPackets stream = reader();
        OggPacket packet = {0};
        CHECK_EQUAL(OGG_PACKET_INVALID, next(&stream, &packet));
    }
}

static void sequence_holes_serial_changes_and_bad_continuations_are_rejected(void)
{
    static const uint8_t LACES[] = {255};
    static const uint8_t END[] = {2};
    for (uint32_t variant = 0; variant < 6; variant++) {
        reset();
        page(2, 0, LACES, 1);
        uint32_t at = page(5, 1, END, 1);
        if (variant == 0) {
            file.data[at + 18] = 2;
        }
        if (variant == 1) {
            file.data[at + 14] = 30;
        }
        if (variant == 2) {
            file.data[at + 5] &= (uint8_t)~1;
        }
        if (variant == 3) {
            file.data[at + 5] |= 2;
        }
        if (variant == 4) {
            file.data[at + 5] |= 8;
        }
        if (variant == 5) {
            file.data[at + 5] = 1;
        }
        repair(at, file.size - at);
        OggPackets stream = reader();
        OggPacket packet = {0};
        if (variant == 5) {
            CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
        }
        CHECK_EQUAL(OGG_PACKET_INVALID, next(&stream, &packet));
    }
}

static void long_packet(void)
{
    reset();
    uint8_t laces[255];
    memset(laces, 255, sizeof laces);
    page(2, 0, laces, sizeof laces);
    static const uint8_t END[] = {255, 7, 2};
    page(5, 1, END, sizeof END);
}

static void prefixes_skip_large_metadata_without_narrowing_the_packet_limit(void)
{
    long_packet();
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, ogg_packets_prefix(&stream, output, 7, &packet));
    CHECK_EQUAL(65287, packet.bytes);
    CHECK(packet.first && !packet.last);
    CHECK_EQUAL(UINT64_MAX, packet.granule);
    CHECK_EQUAL(6, output[6]);
    CHECK_EQUAL(0xA5, output[7]);
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(2, packet.bytes);
    CHECK_EQUAL(2, output[0]);
    CHECK(packet.last);
    CHECK_EQUAL(OGG_PACKET_END, next(&stream, &packet));
}

static void skipped_packets_still_check_every_page_and_can_retry_late_read_failures(void)
{
    long_packet();
    static const uint32_t FAILURES[] = {0, 26, 200, 60000, 65310, 65592};
    for (uint32_t index = 0; index < sizeof FAILURES / sizeof FAILURES[0]; index++) {
        OggPackets stream = reader();
        OggPackets before = stream;
        OggPacket packet = {.bytes = 99};
        file.failing = true;
        file.fail_at = FAILURES[index];
        CHECK_EQUAL(OGG_PACKET_WAIT, ogg_packets_prefix(&stream, output, 7, &packet));
        CHECK(memcmp(&stream, &before, sizeof stream) == 0);
        CHECK_EQUAL(99, packet.bytes);
        CHECK_EQUAL(0xA5, output[7]);
        file.failing = false;
        CHECK_EQUAL(OGG_PACKET_READY, ogg_packets_prefix(&stream, output, 7, &packet));
        CHECK_EQUAL(65287, packet.bytes);
    }
    file.data[file.size - 1] ^= 1;
    OggPackets stream = reader();
    OggPackets before = stream;
    OggPacket packet = {.bytes = 99};
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_packets_prefix(&stream, output, 7, &packet));
    CHECK(memcmp(&stream, &before, sizeof stream) == 0);
    CHECK_EQUAL(99, packet.bytes);
}

static void a_zero_length_prefix_discards_only_one_complete_checked_packet(void)
{
    long_packet();
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, ogg_packets_prefix(&stream, NULL, 0, &packet));
    CHECK_EQUAL(65287, packet.bytes);
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(2, packet.bytes);
    CHECK(packet.last);
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_packets_prefix(&stream, NULL, 1, &packet));
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_packets_prefix(&stream, output, 7, NULL));
}

static void checked_pages_can_be_probed_without_sequential_packet_state(void)
{
    reset();
    static const uint8_t LACES[] = {3, 2};
    page(2, 0, LACES, 2);
    uint32_t at = page(4, 1, LACES, 2);
    memset(file.data + at + 6, 0, 8);
    bytes_put_le32(file.data + at + 6, 12345);
    repair(at, file.size - at);
    OggPage checked = {0};
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_read(reader().input, at, &checked));
    CHECK_EQUAL(at, checked.offset);
    CHECK_EQUAL(at + 29, checked.body);
    CHECK_EQUAL(file.size, checked.next);
    CHECK_EQUAL(29, checked.serial);
    CHECK_EQUAL(1, checked.sequence);
    CHECK_EQUAL(12345, checked.granule);
    CHECK_EQUAL(4, checked.flags);
    CHECK_EQUAL(2, checked.last_complete);
    CHECK_EQUAL(3, checked.laces[0]);
    OggPage before = checked;
    file.data[file.size - 1] ^= 1;
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_page_read(reader().input, at, &checked));
    CHECK(memcmp(&before, &checked, sizeof checked) == 0);
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_page_read(reader().input, UINT32_MAX, &checked));
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_page_read(reader().input, at, NULL));
}

static void page_search_checks_capture_candidates_and_respects_the_search_range(void)
{
    reset();
    static const uint8_t LACES[] = {255, 10};
    page(2, 0, LACES, 2);
    memcpy(file.data + 50, "OggS", 4);
    memset(file.data + 54, 0, 23);
    repair(0, file.size);
    uint32_t at = page(4, 1, LACES, 2);
    OggPage checked = {.offset = 99};
    CHECK_EQUAL(OGG_PACKET_END, ogg_page_find(reader().input, 28, at, &checked));
    CHECK_EQUAL(99, checked.offset);
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_find(reader().input, 28, file.size, &checked));
    CHECK_EQUAL(at, checked.offset);
    CHECK_EQUAL(OGG_PACKET_READY, ogg_page_find(reader().input, at, at + 1, &checked));
    CHECK_EQUAL(OGG_PACKET_END, ogg_page_find(reader().input, at + 1, file.size, &checked));
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_page_find(reader().input, at, at - 1, &checked));
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_page_find(reader().input, 0, file.size + 1, &checked));
}

static void resume_discards_an_orphan_continuation_and_preserves_following_packets(void)
{
    reset();
    static const uint8_t LONG[] = {255};
    static const uint8_t END[] = {7, 2};
    page(2, 0, LONG, 1);
    uint32_t at = page(1, 1, LONG, 1);
    page(5, 2, END, 2);
    OggPackets stream = reader();
    OggPacket packet = {0};
    CHECK_EQUAL(OGG_PACKET_READY, ogg_packets_resume(&stream, stream.input, at, 29));
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(2, packet.bytes);
    CHECK_EQUAL(1, output[0]);
    CHECK(!packet.first && packet.last);
    CHECK_EQUAL(OGG_PACKET_END, next(&stream, &packet));
    OggPackets before = stream;
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_packets_resume(&stream, stream.input, at, 30));
    CHECK(memcmp(&stream, &before, sizeof stream) == 0);
    CHECK_EQUAL(OGG_PACKET_INVALID, ogg_packets_resume(&stream, stream.input, 0, 29));
}

static void page_probe_search_and_resume_preserve_outputs_after_failed_reads(void)
{
    reset();
    static const uint8_t LACES[] = {255, 2};
    page(2, 0, LACES, 2);
    uint32_t at = page(4, 1, LACES, 2);
    file.failing = true;
    file.fail_at = file.size - 1;
    OggPage checked = {.offset = 99};
    OggPage page_before = checked;
    CHECK_EQUAL(OGG_PACKET_WAIT, ogg_page_read(reader().input, at, &checked));
    CHECK(memcmp(&page_before, &checked, sizeof checked) == 0);
    CHECK_EQUAL(OGG_PACKET_WAIT, ogg_page_find(reader().input, at, file.size, &checked));
    CHECK(memcmp(&page_before, &checked, sizeof checked) == 0);
    OggPackets stream = reader();
    OggPackets before = stream;
    CHECK_EQUAL(OGG_PACKET_WAIT, ogg_packets_resume(&stream, stream.input, at, 29));
    CHECK(memcmp(&before, &stream, sizeof stream) == 0);
    file.failing = false;
    CHECK_EQUAL(OGG_PACKET_READY, ogg_packets_resume(&stream, stream.input, at, 29));
    OggPacket packet;
    CHECK_EQUAL(OGG_PACKET_READY, next(&stream, &packet));
    CHECK_EQUAL(257, packet.bytes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(checked_pages_can_be_probed_without_sequential_packet_state),
        TEST_CASE(page_search_checks_capture_candidates_and_respects_the_search_range),
        TEST_CASE(resume_discards_an_orphan_continuation_and_preserves_following_packets),
        TEST_CASE(page_probe_search_and_resume_preserve_outputs_after_failed_reads),
        TEST_CASE(prefixes_skip_large_metadata_without_narrowing_the_packet_limit),
        TEST_CASE(skipped_packets_still_check_every_page_and_can_retry_late_read_failures),
        TEST_CASE(a_zero_length_prefix_discards_only_one_complete_checked_packet),
        TEST_CASE(a_real_vorbis_file_preserves_its_headers_and_final_sample_position),
        TEST_CASE(an_input_without_a_read_callback_is_invalid),
        TEST_CASE(a_complete_stream_reports_packet_boundaries_and_the_final_granule),
        TEST_CASE(a_packet_can_cross_multiple_continued_pages),
        TEST_CASE(an_exact_multiple_of_255_ends_with_a_zero_lace),
        TEST_CASE(only_the_last_completed_packet_owns_the_page_granule),
        TEST_CASE(empty_pages_do_not_invent_packets),
        TEST_CASE(a_maximum_size_page_and_its_terminal_lace_are_read_without_overrun),
        TEST_CASE(null_buffers_only_accept_empty_packets),
        TEST_CASE(damaged_pages_are_rejected_before_emitting_audio),
        TEST_CASE(truncated_pages_and_unfinished_packets_are_rejected),
        TEST_CASE(failed_reads_leave_the_reader_ready_for_an_exact_retry),
        TEST_CASE(packet_limits_leave_the_reader_and_buffer_bounds_intact),
        TEST_CASE(malformed_stream_headers_are_rejected_even_with_valid_checksums),
        TEST_CASE(sequence_holes_serial_changes_and_bad_continuations_are_rejected),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
