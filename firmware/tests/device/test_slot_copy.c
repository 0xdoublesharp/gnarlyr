#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device/slot_copy.h"

enum {
    FW_SIZE = 0x44C000,
    FW_END = 0x500000,
    FW_SECTORS = FW_END / 512,
    CHUNK_SECTORS = 16,
    CHUNKS = FW_END / (CHUNK_SECTORS * 512),
    SLOT_SECTORS = FW_SECTORS + 16,
    SLOT_1 = 16,
    SLOT_2 = SLOT_1 + SLOT_SECTORS,
    STORAGE_SECTORS = SLOT_2 + SLOT_SECTORS,
    LOG_CAPACITY = 4096
};

typedef struct WriteRecord {
    uint32_t lba;
    uint8_t first_byte;
} WriteRecord;

static uint8_t *sectors;
static WriteRecord log_entries[LOG_CAPACITY];
static int writes;
static int write_calls;
static int read_calls;
static int file_read_calls;
static int fail_write_call;
static int fail_read_call;
static int fail_file_read_call;
static uint32_t last_done;
static bool went_backwards;
static uint8_t *file_bytes;
static uint32_t buffer[SLOT_COPY_BUFFER_WORDS];

static bool storage_read(void *context, uint32_t lba, uint32_t count, void *out)
{
    (void)context;
    read_calls++;
    if (read_calls == fail_read_call) {
        return false;
    }
    memcpy(out, sectors + (size_t)lba * 512, (size_t)count * 512);
    return true;
}

static bool storage_write(void *context, uint32_t lba, uint32_t count, const void *bytes)
{
    (void)context;
    write_calls++;
    if (write_calls == fail_write_call) {
        return false;
    }
    memcpy(sectors + (size_t)lba * 512, bytes, (size_t)count * 512);
    if (writes < LOG_CAPACITY) {
        log_entries[writes] = (WriteRecord){lba, ((const uint8_t *)bytes)[0]};
    }
    writes++;
    return true;
}

static bool file_read(void *context, uint32_t offset, void *out, uint32_t length)
{
    (void)context;
    file_read_calls++;
    if (file_read_calls == fail_file_read_call) {
        return false;
    }
    memcpy(out, file_bytes + offset, length);
    return true;
}

static void report(void *context, uint32_t done, uint32_t total)
{
    (void)context;
    CHECK_EQUAL(CHUNKS, total);
    went_backwards = went_backwards || done < last_done;
    last_done = done;
}

static const UpdateStorage STORAGE = {storage_read, storage_write, NULL,
                                      SLOT_1,       SLOT_2,        SLOT_SECTORS};

static uint8_t *image(uint8_t seed)
{
    uint8_t *bytes = calloc(FW_END + 4, 1);
    for (uint32_t index = 0; index < 0x5000; index++) {
        bytes[index] = (uint8_t)(index * 7 + seed);
    }
    memcpy(bytes + 0x1F8, "RKnanoFW", 8);
    bytes[0x1F4] = FW_SIZE & 0xFF;
    bytes[0x1F5] = (FW_SIZE >> 8) & 0xFF;
    bytes[0x1F6] = (FW_SIZE >> 16) & 0xFF;
    bytes[0x1F7] = (uint8_t)(FW_SIZE >> 24);
    memcpy(bytes + FW_SIZE, bytes, 512);
    return bytes;
}

static void reset(void)
{
    free(sectors);
    sectors = calloc(STORAGE_SECTORS, 512);
    writes = 0;
    write_calls = 0;
    read_calls = 0;
    file_read_calls = 0;
    fail_write_call = 0;
    fail_read_call = 0;
    fail_file_read_call = 0;
    last_done = 0;
    went_backwards = false;
}

static bool slot_holds(uint32_t slot, const uint8_t *bytes)
{
    return memcmp(sectors + (size_t)slot * 512, bytes, FW_END) == 0;
}

static int first_write_into(uint32_t slot)
{
    for (int index = 0; index < writes; index++) {
        if (log_entries[index].lba >= slot && log_entries[index].lba < slot + SLOT_SECTORS) {
            return index;
        }
    }
    return -1;
}

static int last_write_into(uint32_t slot)
{
    for (int index = writes - 1; index >= 0; index--) {
        if (log_entries[index].lba >= slot && log_entries[index].lba < slot + SLOT_SECTORS) {
            return index;
        }
    }
    return -1;
}

static void a_file_is_written_into_one_slot_header_last(void)
{
    reset();
    uint8_t *old = image(7);
    file_bytes = image(8);
    memcpy(sectors + (size_t)SLOT_2 * 512, old, FW_END);
    UpdateFile file = {file_read, FW_END + 4, NULL};
    SlotCopyJob job = {&STORAGE, &file, 0, SLOT_1, buffer, {report, NULL}, CHUNKS};
    CHECK(slot_copy_run(&job));
    CHECK(slot_holds(SLOT_1, file_bytes));
    CHECK(slot_holds(SLOT_2, old));
    CHECK_EQUAL(-1, first_write_into(SLOT_2));
    CHECK_EQUAL(SLOT_1, log_entries[first_write_into(SLOT_1)].lba);
    CHECK_EQUAL(0xFF, log_entries[first_write_into(SLOT_1)].first_byte);
    CHECK_EQUAL(SLOT_1, log_entries[last_write_into(SLOT_1)].lba);
    CHECK_EQUAL(file_bytes[0], log_entries[last_write_into(SLOT_1)].first_byte);
    CHECK_EQUAL(CHUNKS, last_done);
    CHECK(!went_backwards);
    free(old);
    free(file_bytes);
}

static void a_valid_slot_is_copied_into_the_other_and_matching_chunks_are_kept(void)
{
    reset();
    uint8_t *bytes = image(9);
    memcpy(sectors + (size_t)SLOT_2 * 512, bytes, FW_END);
    CHECK(slot_copy_is_valid(&STORAGE, SLOT_2, buffer));
    CHECK(!slot_copy_is_valid(&STORAGE, SLOT_1, buffer));
    SlotCopyJob job = {&STORAGE, NULL, SLOT_2, SLOT_1, buffer, {NULL, NULL}, CHUNKS};
    CHECK(slot_copy_run(&job));
    CHECK(slot_holds(SLOT_1, bytes));
    CHECK_EQUAL(-1, first_write_into(SLOT_2));
    int first = writes;
    CHECK(slot_copy_run(&job));
    CHECK_EQUAL(2, writes - first);
    free(bytes);
}

static void a_damaged_header_copy_makes_a_slot_invalid(void)
{
    reset();
    uint8_t *bytes = image(10);
    memcpy(sectors + (size_t)SLOT_2 * 512, bytes, FW_END);
    sectors[(size_t)SLOT_2 * 512 + FW_SIZE + 5] ^= 1;
    CHECK(!slot_copy_is_valid(&STORAGE, SLOT_2, buffer));
    memcpy(sectors + (size_t)SLOT_2 * 512, bytes, FW_END);
    sectors[(size_t)SLOT_2 * 512 + 0x1F8] = 'X';
    CHECK(!slot_copy_is_valid(&STORAGE, SLOT_2, buffer));
    free(bytes);
}

static void every_copy_io_failure_is_reported(void)
{
    enum {
        SMALL_CHUNKS = 3
    };
    file_bytes = image(11);
    UpdateFile file = {file_read, FW_END + 4, NULL};
    SlotCopyJob job = {&STORAGE, &file, 0, SLOT_1, buffer, {NULL, NULL}, SMALL_CHUNKS};
    reset();
    CHECK(slot_copy_run(&job));
    int successful_reads = read_calls;
    int successful_writes = write_calls;
    int successful_file_reads = file_read_calls;
    CHECK(successful_reads > 0);
    CHECK(successful_writes > 0);
    CHECK_EQUAL(SMALL_CHUNKS, successful_file_reads);
    for (int failure = 1; failure <= successful_reads; failure++) {
        reset();
        fail_read_call = failure;
        CHECK(!slot_copy_run(&job));
    }
    for (int failure = 1; failure <= successful_writes; failure++) {
        reset();
        fail_write_call = failure;
        CHECK(!slot_copy_run(&job));
    }
    for (int failure = 1; failure <= successful_file_reads; failure++) {
        reset();
        fail_file_read_call = failure;
        CHECK(!slot_copy_run(&job));
    }
    free(file_bytes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_file_is_written_into_one_slot_header_last),
        TEST_CASE(a_valid_slot_is_copied_into_the_other_and_matching_chunks_are_kept),
        TEST_CASE(a_damaged_header_copy_makes_a_slot_invalid),
        TEST_CASE(every_copy_io_failure_is_reported),
    };
    int result = test_run_all(cases, TEST_CASE_COUNT(cases));
    free(sectors);
    return result;
}
