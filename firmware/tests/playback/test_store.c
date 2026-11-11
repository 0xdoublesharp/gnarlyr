#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "playback/store.h"

enum {
    BANK_BYTES = 2 * STORE_BLOCK_BYTES,
    DISK_BYTES = 2 * BANK_BYTES,
    VERSION_FIELD = 4,
    CHECK_FIELD = 24
};

static const StoreKind BANKED = {.magic = {'D', 'S', 'X', 'B'}, .version = 3};
static const StoreKind RECORD = {
    .magic = {'D', 'S', 'X', 'R'}, .version = 1, .inline_payload = true};

static uint8_t disk[DISK_BYTES];
static StoreFile file;
static int reads;
static int writes;
static bool refuse_reads;
static bool refuse_writes;
static bool report_failure;
static bool spoil_payload;
static int checks;
static StoreLoad check_result[2];

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    reads++;
    if (refuse_reads) {
        return false;
    }
    memcpy(bytes, disk + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    writes++;
    if (refuse_writes) {
        return false;
    }
    memcpy(disk + offset, bytes, length);
    if (spoil_payload && offset % BANK_BYTES >= STORE_BLOCK_BYTES) {
        disk[offset] ^= 1;
    }
    return !report_failure;
}

static void reset(void)
{
    memset(disk, 0, sizeof disk);
    file = (StoreFile){.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
    reads = 0;
    writes = 0;
    refuse_reads = false;
    refuse_writes = false;
    report_failure = false;
    spoil_payload = false;
    checks = 0;
    check_result[0] = STORE_READY;
    check_result[1] = STORE_READY;
}

static const StoreSlot *slot(const StoreKind *kind, uint8_t bank)
{
    static StoreSlot slots[2];
    slots[bank] = (StoreSlot){.file = &file, .kind = kind, .offset = (uint32_t)bank * BANK_BYTES};
    return &slots[bank];
}

static void begin(StoreWrite *job, StoreHeader header)
{
    *job = (StoreWrite){.header = header};
    store_write_begin(job, NULL);
}

static void seal(uint8_t *block)
{
    store_put32(block + CHECK_FIELD, hash_update(HASH_START, block, CHECK_FIELD));
}

static void put_header(uint8_t bank, uint32_t generation)
{
    StoreHeader header = {.generation = generation, .hash = HASH_START};
    store_encode(&BANKED, &header, disk + (uint32_t)bank * BANK_BYTES);
}

static void a_header_round_trips_and_names_what_is_wrong_with_a_block(void)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    StoreHeader header = {
        .generation = 0x11223344, .bytes = 70000, .hash = 0xa1b2c3d4, .binding = 9, .count = 513};
    StoreHeader decoded;
    CHECK_EQUAL(STORE_EMPTY, store_decode(&BANKED, block, &decoded));
    store_encode(&BANKED, &header, block);
    CHECK(memcmp(block, "DSXB", 4) == 0);
    CHECK_EQUAL(STORE_READY, store_decode(&BANKED, block, &decoded));
    CHECK_EQUAL(header.generation, decoded.generation);
    CHECK_EQUAL(header.bytes, decoded.bytes);
    CHECK_EQUAL(header.hash, decoded.hash);
    CHECK_EQUAL(header.binding, decoded.binding);
    CHECK_EQUAL(header.count, decoded.count);
    CHECK_EQUAL(STORE_CORRUPT, store_decode(&RECORD, block, &decoded));
    block[VERSION_FIELD]++;
    CHECK_EQUAL(STORE_CORRUPT, store_decode(&BANKED, block, &decoded));
    seal(block);
    CHECK_EQUAL(STORE_UNSUPPORTED, store_decode(&BANKED, block, &decoded));
    CHECK_EQUAL(0, decoded.generation);
    memset(block, 0, sizeof block);
    block[STORE_BLOCK_BYTES - 1] = 1;
    CHECK_EQUAL(STORE_CORRUPT, store_decode(&BANKED, block, &decoded));
}

static void an_inline_payload_is_sealed_with_its_header(void)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    StoreHeader decoded;
    memcpy(block + STORE_FIELD_BYTES, "payload", 7);
    store_encode(&RECORD, &(StoreHeader){.generation = 4, .bytes = 7}, block);
    CHECK_EQUAL(STORE_READY, store_decode(&RECORD, block, &decoded));
    CHECK_EQUAL(hash_update(HASH_START, "payload", 7), decoded.hash);
    block[STORE_FIELD_BYTES + 6] ^= 1;
    CHECK_EQUAL(STORE_CORRUPT, store_decode(&RECORD, block, &decoded));
    block[STORE_FIELD_BYTES + 6] ^= 1;
    block[STORE_FIELD_BYTES + 7] = 1;
    CHECK_EQUAL(STORE_READY, store_decode(&RECORD, block, &decoded));
    memset(block, 0, sizeof block);
    store_encode(&RECORD, &(StoreHeader){.generation = 4, .bytes = STORE_INLINE_BYTES + 1}, block);
    CHECK_EQUAL(STORE_CORRUPT, store_decode(&RECORD, block, &decoded));
    CHECK_EQUAL(STORE_FAILED, store_publish(slot(&RECORD, 0), block, NULL));
    CHECK_EQUAL(0, writes);
}

static void a_kind_reads_the_older_versions_it_names_and_writes_its_own(void)
{
    static const StoreKind OLD = {.magic = {'D', 'S', 'X', 'R'}, .version = 1};
    static const StoreKind NEWER = {
        .magic = {'D', 'S', 'X', 'R'}, .version = 3, .oldest_version = 2};
    static const StoreKind FUTURE = {.magic = {'D', 'S', 'X', 'R'}, .version = 4};
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    StoreHeader decoded;
    store_encode(&OLD, &(StoreHeader){.generation = 9}, block);
    CHECK_EQUAL(STORE_UNSUPPORTED, store_decode(&NEWER, block, &decoded));
    memset(block, 0, sizeof block);
    store_encode(&(StoreKind){.magic = {'D', 'S', 'X', 'R'}, .version = 2},
                 &(StoreHeader){.generation = 9}, block);
    CHECK_EQUAL(STORE_READY, store_decode(&NEWER, block, &decoded));
    CHECK_EQUAL(2, decoded.version);
    CHECK_EQUAL(9, decoded.generation);
    CHECK_EQUAL(STORE_UNSUPPORTED, store_decode(&FUTURE, block, &decoded));
    memset(block, 0, sizeof block);
    store_encode(&NEWER, &(StoreHeader){.generation = 10}, block);
    CHECK_EQUAL(3, block[VERSION_FIELD]);
    CHECK_EQUAL(STORE_UNSUPPORTED, store_decode(&OLD, block, &decoded));
}

static void generations_compare_as_serial_numbers(void)
{
    CHECK(store_newer(2, 1));
    CHECK(!store_newer(1, 2));
    CHECK(!store_newer(5, 5));
    CHECK(store_newer(0, UINT32_MAX));
    CHECK(!store_newer(UINT32_MAX, 0));
    CHECK(store_newer(UINT32_C(0x7fffffff), 0));
    CHECK(!store_newer(UINT32_C(0x80000000), 0));
}

static void transfers_outside_the_file_never_reach_the_callbacks(void)
{
    uint8_t bytes[4] = {0};
    reset();
    CHECK(store_file_read(&file, sizeof disk - 4, bytes, 4));
    CHECK(store_file_write(&file, sizeof disk - 4, bytes, 4));
    CHECK(!store_file_read(&file, sizeof disk - 3, bytes, 4));
    CHECK(!store_file_write(&file, sizeof disk - 3, bytes, 4));
    CHECK(!store_file_read(&file, sizeof disk + 1, bytes, 0));
    CHECK(!store_file_write(&file, UINT32_MAX, bytes, 4));
    CHECK_EQUAL(1, reads);
    CHECK_EQUAL(1, writes);
    file.write = NULL;
    CHECK(!store_file_write(&file, 0, bytes, 4));
    file.source.read = NULL;
    CHECK(!store_file_read(&file, 0, bytes, 4));
}

static StoreResult publish(uint8_t bank, uint32_t generation, const uint32_t *active)
{
    uint8_t block[STORE_BLOCK_BYTES] = {0};
    store_encode(&BANKED, &(StoreHeader){.generation = generation, .hash = HASH_START}, block);
    return store_publish(slot(&BANKED, bank), block, active);
}

static void publishing_reports_what_the_read_back_shows(void)
{
    uint32_t active = 6;
    reset();
    CHECK_EQUAL(STORE_DONE, publish(1, 7, &active));
    report_failure = true;
    CHECK_EQUAL(STORE_DONE, publish(1, 8, &active));
    report_failure = false;
    refuse_writes = true;
    CHECK_EQUAL(STORE_UNCERTAIN, publish(1, 9, &active));
    put_header(1, 5);
    CHECK_EQUAL(STORE_FAILED, publish(1, 9, &active));
    CHECK_EQUAL(STORE_UNCERTAIN, publish(1, 9, NULL));
    memset(disk + BANK_BYTES, 0, STORE_BLOCK_BYTES);
    CHECK_EQUAL(STORE_FAILED, publish(1, 9, &active));
    disk[BANK_BYTES + 100] = 1;
    CHECK_EQUAL(STORE_FAILED, publish(1, 9, &active));
    put_header(1, 5);
    disk[BANK_BYTES + VERSION_FIELD]++;
    seal(disk + BANK_BYTES);
    CHECK_EQUAL(STORE_UNCERTAIN, publish(1, 9, &active));
    refuse_writes = false;
    refuse_reads = true;
    CHECK_EQUAL(STORE_UNCERTAIN, publish(1, 9, &active));
}

static bool fill_pattern(void *context, uint32_t position, StoreBlock *block, uint32_t count)
{
    const bool *allowed = context;
    for (uint32_t index = 0; index < count; index++) {
        block->bytes[index] = (uint8_t)(position + index);
    }
    return *allowed;
}

static void an_empty_payload_is_published_in_two_writes(void)
{
    StoreWrite job;
    reset();
    begin(&job, (StoreHeader){.generation = 1});
    CHECK_EQUAL(0, store_write_progress(&job));
    CHECK_EQUAL(STORE_WORKING, store_write_step(&job, slot(&BANKED, 0), (StoreFill){0}));
    CHECK_EQUAL(STORE_DONE, store_write_step(&job, slot(&BANKED, 0), (StoreFill){0}));
    CHECK_EQUAL(2, writes);
    CHECK_EQUAL(100, store_write_progress(&job));
    CHECK_EQUAL(HASH_START, job.header.hash);
}

static void a_payload_is_written_verified_and_only_then_published(void)
{
    StoreWrite job;
    StoreHeader header;
    uint8_t block[STORE_BLOCK_BYTES];
    bool allowed = true;
    reset();
    begin(&job, (StoreHeader){.generation = 3, .bytes = 300, .count = 5});
    StoreFill fill = {fill_pattern, &allowed};
    CHECK_EQUAL(STORE_WORKING, store_write_step(&job, slot(&BANKED, 1), fill));
    CHECK_EQUAL(STORE_WORKING, store_write_step(&job, slot(&BANKED, 1), fill));
    CHECK_EQUAL(50, store_write_progress(&job));
    CHECK_EQUAL(STORE_EMPTY, store_load(slot(&BANKED, 1), block, &header));
    CHECK_EQUAL(STORE_WORKING, store_write_step(&job, slot(&BANKED, 1), fill));
    CHECK_EQUAL(STORE_EMPTY, store_load(slot(&BANKED, 1), block, &header));
    CHECK_EQUAL(STORE_DONE, store_write_step(&job, slot(&BANKED, 1), fill));
    CHECK_EQUAL(STORE_READY, store_load(slot(&BANKED, 1), block, &header));
    CHECK_EQUAL(300, header.bytes);
    CHECK_EQUAL(5, header.count);
    CHECK_EQUAL(hash_update(HASH_START, disk + BANK_BYTES + STORE_BLOCK_BYTES, 300), header.hash);
    CHECK_EQUAL(43, disk[BANK_BYTES + STORE_BLOCK_BYTES + 299]);
    CHECK_EQUAL(0, disk[BANK_BYTES + STORE_BLOCK_BYTES + 300]);
    CHECK_EQUAL(STORE_DONE, store_write_step(&job, slot(&BANKED, 1), fill));
}

static void a_payload_that_reads_back_differently_is_never_published(void)
{
    StoreWrite job;
    StoreHeader header;
    uint8_t block[STORE_BLOCK_BYTES];
    bool allowed = true;
    reset();
    put_header(1, 2);
    spoil_payload = true;
    begin(&job, (StoreHeader){.generation = 3, .bytes = 300});
    StoreFill fill = {fill_pattern, &allowed};
    while (store_write_step(&job, slot(&BANKED, 1), fill) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_FAILED, job.result);
    CHECK_EQUAL(2, writes);
    CHECK_EQUAL(STORE_EMPTY, store_load(slot(&BANKED, 1), block, &header));
}

static void a_refused_fill_an_abort_and_an_idle_job_write_nothing_more(void)
{
    StoreWrite job = {0};
    bool allowed = false;
    reset();
    StoreFill fill = {fill_pattern, &allowed};
    CHECK_EQUAL(STORE_IDLE, store_write_step(&job, slot(&BANKED, 0), fill));
    store_write_abort(&job);
    CHECK_EQUAL(STORE_IDLE, job.result);
    begin(&job, (StoreHeader){.generation = 3, .bytes = 300});
    CHECK_EQUAL(STORE_WORKING, store_write_step(&job, slot(&BANKED, 0), fill));
    CHECK_EQUAL(STORE_FAILED, store_write_step(&job, slot(&BANKED, 0), fill));
    CHECK_EQUAL(1, writes);
    begin(&job, (StoreHeader){.generation = 3, .bytes = 300});
    store_write_abort(&job);
    CHECK_EQUAL(STORE_FAILED, store_write_step(&job, slot(&BANKED, 0), fill));
    CHECK_EQUAL(1, writes);
    begin(&job, (StoreHeader){.generation = 3, .bytes = 300});
    CHECK_EQUAL(STORE_WORKING, store_write_step(&job, slot(&BANKED, 0), (StoreFill){0}));
    CHECK_EQUAL(STORE_FAILED, store_write_step(&job, slot(&BANKED, 0), (StoreFill){0}));
}

static void progress_stays_exact_for_the_largest_payloads(void)
{
    StoreWrite job;
    begin(&job, (StoreHeader){.bytes = UINT32_C(17563380)});
    job.written.position = job.header.bytes;
    CHECK_EQUAL(50, store_write_progress(&job));
    job.verified.position = job.header.bytes / 2;
    CHECK_EQUAL(75, store_write_progress(&job));
    job.verified.position = job.header.bytes;
    CHECK(store_write_progress(&job) >= 99);
    job.header.bytes = UINT32_MAX;
    job.written.position = UINT32_MAX;
    job.verified.position = UINT32_MAX;
    CHECK(store_write_progress(&job) >= 99 && store_write_progress(&job) <= 100);
}

static StoreLoad check_bank(void *context, uint8_t bank, const StoreHeader *header)
{
    (void)context;
    (void)header;
    checks++;
    return check_result[bank];
}

static StoreLoad open_pair(StoreChoice *choice)
{
    StorePair pair = {
        .file = &file, .kind = &BANKED, .bank_bytes = BANK_BYTES, .check = check_bank};
    return store_pair_open(&pair, choice);
}

static void the_newest_bank_that_passes_its_check_is_chosen(void)
{
    StoreChoice choice = {.bank = 9};
    reset();
    CHECK_EQUAL(STORE_EMPTY, open_pair(&choice));
    CHECK_EQUAL(0, checks);
    CHECK_EQUAL(9, choice.bank);
    put_header(0, UINT32_MAX);
    put_header(1, 0);
    CHECK_EQUAL(STORE_READY, open_pair(&choice));
    CHECK_EQUAL(1, choice.bank);
    CHECK_EQUAL(0, choice.header.generation);
    CHECK_EQUAL(1, checks);
    check_result[1] = STORE_CORRUPT;
    CHECK_EQUAL(STORE_READY, open_pair(&choice));
    CHECK_EQUAL(0, choice.bank);
    CHECK_EQUAL(UINT32_MAX, choice.header.generation);
    CHECK_EQUAL(3, checks);
    check_result[0] = STORE_EMPTY;
    CHECK_EQUAL(STORE_CORRUPT, open_pair(&choice));
    check_result[1] = STORE_EMPTY;
    CHECK_EQUAL(STORE_EMPTY, open_pair(&choice));
    check_result[1] = STORE_UNREADABLE;
    check_result[0] = STORE_READY;
    checks = 0;
    CHECK_EQUAL(STORE_UNREADABLE, open_pair(&choice));
    CHECK_EQUAL(1, checks);
    StorePair unchecked = {.file = &file, .kind = &BANKED, .bank_bytes = BANK_BYTES};
    CHECK_EQUAL(STORE_READY, store_pair_open(&unchecked, &choice));
    CHECK_EQUAL(1, choice.bank);
}

static void a_bank_that_cannot_be_judged_stops_the_choice(void)
{
    StoreChoice choice;
    reset();
    put_header(0, 4);
    put_header(1, 5);
    disk[VERSION_FIELD]++;
    seal(disk);
    CHECK_EQUAL(STORE_UNSUPPORTED, open_pair(&choice));
    CHECK_EQUAL(0, checks);
    file.size = BANK_BYTES;
    CHECK_EQUAL(STORE_UNSUPPORTED, open_pair(&choice));
    disk[CHECK_FIELD] ^= 1;
    CHECK_EQUAL(STORE_UNREADABLE, open_pair(&choice));
    CHECK_EQUAL(0, checks);
    file.size = sizeof disk;
    CHECK_EQUAL(STORE_READY, open_pair(&choice));
    CHECK_EQUAL(1, choice.bank);
    check_result[1] = STORE_EMPTY;
    CHECK_EQUAL(STORE_CORRUPT, open_pair(&choice));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_header_round_trips_and_names_what_is_wrong_with_a_block),
        TEST_CASE(an_inline_payload_is_sealed_with_its_header),
        TEST_CASE(a_kind_reads_the_older_versions_it_names_and_writes_its_own),
        TEST_CASE(generations_compare_as_serial_numbers),
        TEST_CASE(transfers_outside_the_file_never_reach_the_callbacks),
        TEST_CASE(publishing_reports_what_the_read_back_shows),
        TEST_CASE(an_empty_payload_is_published_in_two_writes),
        TEST_CASE(a_payload_is_written_verified_and_only_then_published),
        TEST_CASE(a_payload_that_reads_back_differently_is_never_published),
        TEST_CASE(a_refused_fill_an_abort_and_an_idle_job_write_nothing_more),
        TEST_CASE(progress_stays_exact_for_the_largest_payloads),
        TEST_CASE(the_newest_bank_that_passes_its_check_is_chosen),
        TEST_CASE(a_bank_that_cannot_be_judged_stops_the_choice),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
