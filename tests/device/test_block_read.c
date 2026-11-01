#include "check.h"
#include "device/block_read.h"

enum {
    CALL_CAPACITY = 16,
    TOTAL_SECTORS = 1000
};

typedef struct Call {
    bool multiple;
    uint32_t lba;
    uint32_t count;
} Call;

typedef struct FakeDevice {
    Call calls[CALL_CAPACITY];
    int call_count;
    bool multiple_fails;
    uint32_t failing_single;
} FakeDevice;

static uint32_t buffer[(2 * BLOCK_READ_CHUNK_SECTORS + 3) * BLOCK_READ_SECTOR_WORDS];

static void record(FakeDevice *device, bool multiple, uint32_t lba, uint32_t count)
{
    if (device->call_count < CALL_CAPACITY) {
        device->calls[device->call_count] = (Call){multiple, lba, count};
    }
    device->call_count++;
}

static bool fake_single(void *context, uint32_t lba, uint32_t *words)
{
    FakeDevice *device = context;
    record(device, false, lba, 1);
    words[0] = lba;
    return lba != device->failing_single;
}

static bool fake_multiple(void *context, uint32_t lba, uint32_t count, uint32_t *words)
{
    FakeDevice *device = context;
    record(device, true, lba, count);
    for (uint32_t index = 0; index < count; index++) {
        words[index * BLOCK_READ_SECTOR_WORDS] = lba + index;
    }
    return !device->multiple_fails;
}

static BlockReader reader_for(FakeDevice *device)
{
    return (BlockReader){fake_single, fake_multiple, device, TOTAL_SECTORS};
}

static void check_filled(uint32_t lba, uint32_t count)
{
    for (uint32_t index = 0; index < count; index++) {
        CHECK_EQUAL(lba + index, buffer[index * BLOCK_READ_SECTOR_WORDS]);
    }
}

static void one_sector_is_read_with_a_single_block_command(void)
{
    FakeDevice device = {.failing_single = UINT32_MAX};
    BlockReader reader = reader_for(&device);
    CHECK(block_read(&reader, 7, 1, buffer));
    CHECK_EQUAL(1, device.call_count);
    CHECK(!device.calls[0].multiple);
    CHECK_EQUAL(7, device.calls[0].lba);
    check_filled(7, 1);
}

static void several_sectors_are_read_with_one_multiple_block_command(void)
{
    FakeDevice device = {.failing_single = UINT32_MAX};
    BlockReader reader = reader_for(&device);
    CHECK(block_read(&reader, 100, 8, buffer));
    CHECK_EQUAL(1, device.call_count);
    CHECK(device.calls[0].multiple);
    CHECK_EQUAL(100, device.calls[0].lba);
    CHECK_EQUAL(8, device.calls[0].count);
    check_filled(100, 8);
}

static void a_long_read_is_split_into_chunks_that_follow_each_other(void)
{
    FakeDevice device = {.failing_single = UINT32_MAX};
    BlockReader reader = reader_for(&device);
    uint32_t count = 2 * BLOCK_READ_CHUNK_SECTORS + 3;
    CHECK(block_read(&reader, 10, count, buffer));
    CHECK_EQUAL(3, device.call_count);
    CHECK_EQUAL(10, device.calls[0].lba);
    CHECK_EQUAL(BLOCK_READ_CHUNK_SECTORS, device.calls[0].count);
    CHECK_EQUAL(10 + BLOCK_READ_CHUNK_SECTORS, device.calls[1].lba);
    CHECK_EQUAL(BLOCK_READ_CHUNK_SECTORS, device.calls[1].count);
    CHECK_EQUAL(10 + 2 * BLOCK_READ_CHUNK_SECTORS, device.calls[2].lba);
    CHECK_EQUAL(3, device.calls[2].count);
    check_filled(10, count);
}

static void a_failed_multiple_block_read_is_retried_one_sector_at_a_time(void)
{
    FakeDevice device = {.multiple_fails = true, .failing_single = UINT32_MAX};
    BlockReader reader = reader_for(&device);
    CHECK(block_read(&reader, 50, 3, buffer));
    CHECK_EQUAL(4, device.call_count);
    CHECK(device.calls[0].multiple);
    for (int index = 1; index < 4; index++) {
        CHECK(!device.calls[index].multiple);
        CHECK_EQUAL(50 + index - 1, device.calls[index].lba);
    }
    check_filled(50, 3);
}

static void a_read_fails_when_a_single_sector_cannot_be_read(void)
{
    FakeDevice device = {.multiple_fails = true, .failing_single = 51};
    BlockReader reader = reader_for(&device);
    CHECK(!block_read(&reader, 50, 3, buffer));
    CHECK_EQUAL(3, device.call_count);
}

static void a_read_past_the_end_of_the_device_is_refused_untouched(void)
{
    FakeDevice device = {.failing_single = UINT32_MAX};
    BlockReader reader = reader_for(&device);
    CHECK(!block_read(&reader, TOTAL_SECTORS, 1, buffer));
    CHECK(!block_read(&reader, TOTAL_SECTORS - 2, 3, buffer));
    CHECK(!block_read(&reader, UINT32_MAX, 2, buffer));
    CHECK_EQUAL(0, device.call_count);
    CHECK(block_read(&reader, TOTAL_SECTORS - 2, 2, buffer));
}

static void reading_nothing_succeeds_without_touching_the_device(void)
{
    FakeDevice device = {.failing_single = UINT32_MAX};
    BlockReader reader = reader_for(&device);
    CHECK(block_read(&reader, 5, 0, buffer));
    CHECK_EQUAL(0, device.call_count);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(one_sector_is_read_with_a_single_block_command),
        TEST_CASE(several_sectors_are_read_with_one_multiple_block_command),
        TEST_CASE(a_long_read_is_split_into_chunks_that_follow_each_other),
        TEST_CASE(a_failed_multiple_block_read_is_retried_one_sector_at_a_time),
        TEST_CASE(a_read_fails_when_a_single_sector_cannot_be_read),
        TEST_CASE(a_read_past_the_end_of_the_device_is_refused_untouched),
        TEST_CASE(reading_nothing_succeeds_without_touching_the_device),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
