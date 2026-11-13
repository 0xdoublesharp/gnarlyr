#include <string.h>

#include "check.h"
#include "device/dsd_stream.h"

enum {
    FILE_BYTES = 10000,
    DSD_RATE = 2822400,
    DSD_SAMPLES = 32768
};

typedef struct DsdFile {
    uint8_t bytes[FILE_BYTES];
    uint32_t size;
    uint32_t reads;
    uint32_t fail_read;
} DsdFile;

static void put_little(uint8_t *bytes, uint64_t value, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static void put_big(uint8_t *bytes, uint64_t value, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        bytes[count - index - 1] = (uint8_t)(value >> (index * 8));
    }
}

static bool read_file(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    DsdFile *file = context;
    file->reads++;
    if (file->reads == file->fail_read || offset > file->size || length > file->size - offset) {
        return false;
    }
    memcpy(bytes, file->bytes + offset, length);
    return true;
}

static FlacInput input(DsdFile *file)
{
    return (FlacInput){read_file, file, file->size};
}

static DsdFile dsf(uint32_t channels, uint32_t order)
{
    DsdFile file = {0};
    uint32_t data_bytes = DSD_BLOCK_BYTES * channels;
    file.size = 80 + 12 + data_bytes;
    memcpy(file.bytes, "DSD ", 4);
    put_little(file.bytes + 4, 28, 8);
    put_little(file.bytes + 12, file.size, 8);
    memcpy(file.bytes + 28, "fmt ", 4);
    put_little(file.bytes + 32, 52, 8);
    put_little(file.bytes + 40, 1, 4);
    put_little(file.bytes + 44, 0, 4);
    put_little(file.bytes + 48, channels, 4);
    put_little(file.bytes + 52, channels, 4);
    put_little(file.bytes + 56, DSD_RATE, 4);
    put_little(file.bytes + 60, order, 4);
    put_little(file.bytes + 64, DSD_SAMPLES, 8);
    put_little(file.bytes + 72, DSD_BLOCK_BYTES, 4);
    memcpy(file.bytes + 80, "data", 4);
    put_little(file.bytes + 84, 12 + data_bytes, 8);
    return file;
}

static uint32_t dff_chunk(uint8_t *bytes, uint32_t at, const char *tag, const uint8_t *body,
                          uint32_t length)
{
    memcpy(bytes + at, tag, 4);
    put_big(bytes + at + 4, length, 8);
    memcpy(bytes + at + 12, body, length);
    if ((length & 1u) != 0) {
        bytes[at + 12 + length] = 0;
    }
    return at + 12 + length + (length & 1u);
}

static DsdFile dff(uint32_t channels)
{
    DsdFile file = {0};
    uint8_t properties[96] = {0};
    uint32_t property = 0;
    memcpy(properties, "SND ", 4);
    property = 4;
    uint8_t rate[4];
    put_big(rate, DSD_RATE, sizeof rate);
    property = dff_chunk(properties, property, "FS  ", rate, sizeof rate);
    uint8_t channel[10] = {0};
    put_big(channel, channels, 2);
    property = dff_chunk(properties, property, "CHNL", channel, 2 + 4 * channels);
    property = dff_chunk(properties, property, "CMPR", (const uint8_t *)"DSD ", 4);

    memcpy(file.bytes, "FRM8", 4);
    memcpy(file.bytes + 12, "DSD ", 4);
    uint32_t cursor = dff_chunk(file.bytes, 16, "PROP", properties, property);
    uint8_t audio[16] = {0};
    cursor = dff_chunk(file.bytes, cursor, "DSD ", audio, channels * 8);
    file.size = cursor;
    put_big(file.bytes + 4, file.size - 12, 8);
    return file;
}

static uint8_t *find_tag(DsdFile *file, const char *tag)
{
    for (uint32_t index = 0; index + 4 <= file->size; index++) {
        if (memcmp(file->bytes + index, tag, 4) == 0) {
            return file->bytes + index;
        }
    }
    return NULL;
}

static void probes_dsf_bit_orders_and_channel_layouts(void)
{
    for (uint32_t channels = 1; channels <= 2; channels++) {
        for (uint32_t order = 1; order <= 8; order += 7) {
            DsdFile file = dsf(channels, order);
            DsdTrack track;
            CHECK(dsd_probe(input(&file), &track));
            CHECK_EQUAL(DSD_RATE, track.rate);
            CHECK_EQUAL(DSD_SAMPLES, track.samples);
            CHECK_EQUAL(channels, track.channels);
            CHECK_EQUAL(order == 1 ? 1 : 2, track.layout);
            CHECK_EQUAL(92, track.data_offset);
            CHECK_EQUAL(DSD_BLOCK_BYTES * channels, track.data_bytes);
        }
    }
}

static void probes_uncompressed_dff_channels(void)
{
    for (uint32_t channels = 1; channels <= 2; channels++) {
        DsdFile file = dff(channels);
        DsdTrack track;
        CHECK(dsd_probe(input(&file), &track));
        CHECK_EQUAL(DSD_RATE, track.rate);
        CHECK_EQUAL(channels, track.channels);
        CHECK_EQUAL(64, track.samples);
        CHECK_EQUAL(channels * 8, track.data_bytes);
    }
}

static void rejects_dsf_metadata_and_data_outside_the_file(void)
{
    DsdTrack track = {.rate = 17};
    DsdTrack before = track;
    DsdFile file = dsf(1, 1);
    put_little(file.bytes + 20, 80, 8);
    CHECK(!dsd_probe(input(&file), &track));
    CHECK(memcmp(&track, &before, sizeof track) == 0);

    file = dsf(1, 1);
    put_little(file.bytes + 84, 11, 8);
    CHECK(!dsd_probe(input(&file), &track));
    file = dsf(1, 1);
    put_little(file.bytes + 84, file.size, 8);
    CHECK(!dsd_probe(input(&file), &track));
    file = dsf(1, 1);
    put_little(file.bytes + 60, 2, 4);
    CHECK(!dsd_probe(input(&file), &track));
}

static void rejects_duplicate_and_compressed_dff_chunks(void)
{
    DsdTrack track = {.rate = 17};
    DsdTrack before = track;
    DsdFile file = dff(2);
    uint8_t *data = file.bytes + file.size - (12 + 16);
    memcpy(data, "DST ", 4);
    CHECK(!dsd_probe(input(&file), &track));
    CHECK(memcmp(&track, &before, sizeof track) == 0);

    file = dff(2);
    data = file.bytes + file.size - (12 + 16);
    memcpy(data, "PROP", 4);
    CHECK(!dsd_probe(input(&file), &track));

    file = dff(2);
    uint32_t old_size = file.size;
    uint8_t more[4] = {0};
    file.size = dff_chunk(file.bytes, file.size, "DSD ", more, sizeof more);
    put_big(file.bytes + 4, file.size - 12, 8);
    CHECK(file.size > old_size);
    CHECK(!dsd_probe(input(&file), &track));

    file = dff(2);
    uint8_t *compression = find_tag(&file, "CMPR");
    CHECK(compression != NULL);
    memcpy(compression + 12, "DST ", 4);
    CHECK(!dsd_probe(input(&file), &track));
}

static void read_failures_never_publish_partial_tracks(void)
{
    for (uint32_t kind = 0; kind < 2; kind++) {
        for (uint32_t failure = 1; failure <= 8; failure++) {
            DsdFile file = kind == 0 ? dsf(2, 8) : dff(2);
            file.fail_read = failure;
            DsdTrack track = {.rate = 17};
            DsdTrack before = track;
            bool parsed = dsd_probe(input(&file), &track);
            if (!parsed) {
                CHECK(memcmp(&track, &before, sizeof track) == 0);
            }
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(probes_dsf_bit_orders_and_channel_layouts),
        TEST_CASE(probes_uncompressed_dff_channels),
        TEST_CASE(rejects_dsf_metadata_and_data_outside_the_file),
        TEST_CASE(rejects_duplicate_and_compressed_dff_chunks),
        TEST_CASE(read_failures_never_publish_partial_tracks),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
