#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "core/random.h"
#include "playback/track_order.h"

enum {
    FINGERPRINT = 0x12345678,
    TRACK_LIMIT = 32,
    VERSION_FIELD = 4,
    GENERATION_FIELD = 8,
    CHECK_FIELD = 24
};

static uint8_t disk[TRACK_ORDER_FILE_BYTES];
static TrackOrder order;
static int writes;
static int fail_write_at;
static uint32_t torn_bytes;
static bool fail_reads;
static bool fail_commit_read;
static bool commit_written;
static bool corrupt_payload;
static bool refuse_source;
static uint32_t max_transfer;
static int payload_reads[2];

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    max_transfer = length > max_transfer ? length : max_transfer;
    if (offset < sizeof disk && offset % TRACK_ORDER_BANK_BYTES >= STORE_BLOCK_BYTES) {
        payload_reads[offset / TRACK_ORDER_BANK_BYTES]++;
    }
    if (fail_reads || (fail_commit_read && commit_written && offset % TRACK_ORDER_BANK_BYTES == 0 &&
                       length == STORE_BLOCK_BYTES)) {
        return false;
    }
    if (offset > sizeof disk || length > sizeof disk - offset) {
        return false;
    }
    memcpy(bytes, disk + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    max_transfer = length > max_transfer ? length : max_transfer;
    if (offset > sizeof disk || length > sizeof disk - offset) {
        return false;
    }
    bool failed = writes++ == fail_write_at;
    uint32_t amount = failed && torn_bytes < length ? torn_bytes : length;
    memcpy(disk + offset, bytes, amount);
    if (corrupt_payload && offset % TRACK_ORDER_BANK_BYTES >= STORE_BLOCK_BYTES) {
        disk[offset] ^= 1;
    }
    commit_written |= length == STORE_BLOCK_BYTES && memcmp(bytes, "DSTO", 4) == 0;
    return !failed;
}

static TrackOrderConfig configuration(void)
{
    return (TrackOrderConfig){
        .file = {.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk},
        .fingerprint = FINGERPRINT,
        .track_limit = TRACK_LIMIT};
}

static bool read_tracks(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    uint16_t *source = context;
    if (refuse_source) {
        return false;
    }
    memcpy(tracks, source + first, (size_t)count * sizeof *tracks);
    return true;
}

static void reset(void)
{
    memset(disk, 0, sizeof disk);
    writes = 0;
    fail_write_at = -1;
    torn_bytes = 0;
    fail_reads = false;
    fail_commit_read = false;
    commit_written = false;
    corrupt_payload = false;
    refuse_source = false;
    max_transfer = 0;
    CHECK_EQUAL(STORE_EMPTY, track_order_open(&order, configuration()));
}

static int last_steps;

static StoreResult finish(TrackOrderJob *job)
{
    StoreResult result;
    int steps = 0;
    do {
        result = track_order_job_step(job);
        CHECK(++steps < 1024);
    } while (result == STORE_WORKING);
    last_steps = steps;
    return result;
}

static void a_committed_order_survives_reopening(void)
{
    static uint16_t TRACKS[] = {7, 3, 7, 9};
    reset();
    TrackOrderSource source = {read_tracks, TRACKS, 4};
    TrackOrderJob job;
    CHECK(track_order_job_begin(&job, &order, source, (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
    CHECK_EQUAL(STORE_DONE, finish(&job));
    TrackOrder reopened;
    CHECK_EQUAL(STORE_READY, track_order_open(&reopened, configuration()));
    uint16_t tracks[4] = {0};
    CHECK(track_order_read(&reopened, 0, 4, tracks));
    CHECK(memcmp(tracks, TRACKS, sizeof tracks) == 0);
    CHECK_EQUAL(4, reopened.count);
}

static void check_tracks(const TrackOrder *stored, const uint16_t *expected, uint16_t count)
{
    CHECK_EQUAL(count, stored->count);
    uint16_t tracks[16] = {0};
    CHECK(count <= 16);
    CHECK(track_order_read(stored, 0, count, tracks));
    CHECK(memcmp(tracks, expected, (size_t)count * sizeof *tracks) == 0);
}

static StoreResult apply(TrackOrderEdit edit)
{
    TrackOrderJob job;
    CHECK(track_order_job_begin(&job, &order, track_order_source(&order), edit));
    return finish(&job);
}

static void make_order(void)
{
    a_committed_order_survives_reopening();
}

static void make_order_in_place(void)
{
    static uint16_t tracks[] = {7, 3, 7, 9};
    TrackOrderSource source = {read_tracks, tracks, 4};
    TrackOrderJob job;
    CHECK(track_order_job_begin(&job, &order, source, (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
    CHECK_EQUAL(STORE_DONE, finish(&job));
}

static void moves_removals_and_insertions_preserve_duplicate_entries(void)
{
    static const uint16_t MOVED[] = {9, 7, 3, 7};
    static const uint16_t BACK[] = {7, 3, 7, 9};
    static const uint16_t REMOVED[] = {7, 3, 9};
    static const uint16_t INSERTED[] = {7, 11, 3, 9};
    make_order();
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    check_tracks(&order, MOVED, 4);
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_MOVE, 0, 3, 0}));
    check_tracks(&order, BACK, 4);
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_REMOVE, 2, 0, 0}));
    check_tracks(&order, REMOVED, 3);
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_INSERT, 1, 0, 11}));
    check_tracks(&order, INSERTED, 4);
    TrackOrder reopened;
    CHECK_EQUAL(STORE_READY, track_order_open(&reopened, configuration()));
    check_tracks(&reopened, INSERTED, 4);
}

static void empty_and_cleared_orders_are_valid_persistent_lists(void)
{
    reset();
    TrackOrderJob job;
    CHECK(track_order_job_begin(&job, &order, (TrackOrderSource){0},
                                (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
    CHECK_EQUAL(STORE_DONE, finish(&job));
    CHECK(order.valid);
    CHECK(track_order_read(&order, 0, 0, NULL));
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_INSERT, 0, 0, 5}));
    CHECK_EQUAL(1, order.count);
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_CLEAR, 0, 0, 0}));
    CHECK_EQUAL(0, order.count);
    CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    CHECK_EQUAL(0, order.count);
}

static void invalid_edits_and_ranges_never_touch_storage(void)
{
    make_order();
    int before = writes;
    TrackOrderJob job;
    TrackOrderSource source = track_order_source(&order);
    CHECK(
        !track_order_job_begin(&job, &order, source, (TrackOrderEdit){TRACK_ORDER_MOVE, 4, 0, 0}));
    CHECK(
        !track_order_job_begin(&job, &order, source, (TrackOrderEdit){TRACK_ORDER_MOVE, 0, 4, 0}));
    CHECK(!track_order_job_begin(&job, &order, source,
                                 (TrackOrderEdit){TRACK_ORDER_REMOVE, 4, 0, 0}));
    CHECK(!track_order_job_begin(&job, &order, source,
                                 (TrackOrderEdit){TRACK_ORDER_INSERT, 5, 0, 1}));
    CHECK(!track_order_job_begin(&job, &order, source,
                                 (TrackOrderEdit){TRACK_ORDER_INSERT, 0, 0, TRACK_LIMIT}));
    CHECK(!track_order_job_begin(&job, &order, source,
                                 (TrackOrderEdit){(TrackOrderOperation)99, 0, 0, 0}));
    uint16_t track = 0;
    CHECK(!track_order_read(&order, 4, 1, &track));
    CHECK(!track_order_read(&order, UINT16_MAX, 2, &track));
    CHECK(!track_order_read(&order, 0, 1, NULL));
    CHECK_EQUAL(before, writes);
    CHECK(!order.busy);
}

static void abort_and_each_precommit_power_cut_preserve_the_previous_order(void)
{
    static uint8_t original[TRACK_ORDER_FILE_BYTES];
    static const uint16_t TRACKS[] = {7, 3, 7, 9};
    static const uint16_t MOVED[] = {9, 7, 3, 7};
    make_order();
    memcpy(original, disk, sizeof disk);
    for (int stop = 0; stop < 9; stop++) {
        memcpy(disk, original, sizeof disk);
        CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
        TrackOrderJob job;
        CHECK(track_order_job_begin(&job, &order, track_order_source(&order),
                                    (TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
        StoreResult result = STORE_WORKING;
        for (int step = 0; step < stop && result == STORE_WORKING; step++) {
            result = track_order_job_step(&job);
        }
        TrackOrder recovered;
        CHECK_EQUAL(STORE_READY, track_order_open(&recovered, configuration()));
        check_tracks(&recovered, result == STORE_DONE ? MOVED : TRACKS, 4);
        track_order_job_abort(&job);
        CHECK(!order.busy);
    }
}

static void torn_writes_keep_a_valid_copy_and_report_completed_commits_truthfully(void)
{
    static uint8_t original[TRACK_ORDER_FILE_BYTES];
    static const uint16_t TRACKS[] = {7, 3, 7, 9};
    static const uint16_t MOVED[] = {9, 7, 3, 7};
    static const uint32_t CUTS[] = {0, 1, 4, 27, 28, 511, 512};
    make_order();
    memcpy(original, disk, sizeof disk);
    for (int failed = 0; failed < 3; failed++) {
        for (unsigned cut = 0; cut < sizeof CUTS / sizeof CUTS[0]; cut++) {
            memcpy(disk, original, sizeof disk);
            CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
            fail_write_at = writes + failed;
            torn_bytes = CUTS[cut];
            StoreResult result = apply((TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0});
            CHECK(result == STORE_DONE || result == STORE_FAILED);
            TrackOrder recovered;
            CHECK_EQUAL(STORE_READY, track_order_open(&recovered, configuration()));
            check_tracks(&recovered, result == STORE_DONE ? MOVED : TRACKS, 4);
            CHECK(!order.busy);
        }
    }
}

static void a_failed_commit_readback_requires_reload_before_another_edit(void)
{
    static const uint16_t TRACKS[] = {7, 3, 7, 9};
    static const uint16_t MOVED[] = {9, 7, 3, 7};
    make_order();
    commit_written = false;
    fail_commit_read = true;
    CHECK_EQUAL(STORE_UNCERTAIN, apply((TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    check_tracks(&order, TRACKS, 4);
    CHECK(order.needs_reload);
    TrackOrderJob job;
    CHECK(!track_order_job_begin(&job, &order, track_order_source(&order),
                                 (TrackOrderEdit){TRACK_ORDER_CLEAR, 0, 0, 0}));
    fail_commit_read = false;
    CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    check_tracks(&order, MOVED, 4);
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_CLEAR, 0, 0, 0}));
}

static void silent_payload_corruption_is_detected_before_publication(void)
{
    static const uint16_t TRACKS[] = {7, 3, 7, 9};
    make_order();
    corrupt_payload = true;
    CHECK_EQUAL(STORE_FAILED, apply((TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    check_tracks(&order, TRACKS, 4);
    corrupt_payload = false;
    CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    check_tracks(&order, TRACKS, 4);
}

static void source_failures_and_invalid_track_ids_do_not_publish_partial_data(void)
{
    static uint16_t TRACKS[] = {7, 3, 7, 9};
    make_order();
    refuse_source = true;
    TrackOrderJob job;
    TrackOrderSource source = {read_tracks, TRACKS, 4};
    CHECK(track_order_job_begin(&job, &order, source, (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
    CHECK_EQUAL(STORE_FAILED, finish(&job));
    refuse_source = false;
    static uint16_t BAD[] = {1, TRACK_LIMIT};
    source = (TrackOrderSource){read_tracks, BAD, 2};
    CHECK(track_order_job_begin(&job, &order, source, (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
    CHECK_EQUAL(STORE_FAILED, finish(&job));
    CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    check_tracks(&order, TRACKS, 4);
}

static void a_second_job_cannot_overwrite_an_active_transaction(void)
{
    make_order();
    TrackOrderJob first;
    TrackOrderJob second;
    CHECK(track_order_job_begin(&first, &order, track_order_source(&order),
                                (TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    int before = writes;
    CHECK(!track_order_job_begin(&second, &order, track_order_source(&order),
                                 (TrackOrderEdit){TRACK_ORDER_CLEAR, 0, 0, 0}));
    CHECK_EQUAL(before, writes);
    track_order_job_abort(&first);
    CHECK_EQUAL(STORE_FAILED, track_order_job_step(&first));
    CHECK_EQUAL(before, writes);
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
}

static void a_damaged_latest_bank_falls_back_to_the_previous_commit(void)
{
    static const uint16_t TRACKS[] = {7, 3, 7, 9};
    make_order();
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    disk[(uint32_t)order.bank * TRACK_ORDER_BANK_BYTES + STORE_BLOCK_BYTES] ^= 1;
    CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    check_tracks(&order, TRACKS, 4);
    disk[(uint32_t)order.bank * TRACK_ORDER_BANK_BYTES + CHECK_FIELD] ^= 1;
    CHECK_EQUAL(STORE_EMPTY, track_order_open(&order, configuration()));
    CHECK(!order.valid);
}

static void another_library_and_invalid_storage_are_not_opened_as_valid_orders(void)
{
    make_order();
    TrackOrderConfig config = configuration();
    config.fingerprint++;
    CHECK_EQUAL(STORE_EMPTY, track_order_open(&order, config));
    config = configuration();
    config.file.size--;
    CHECK_EQUAL(STORE_UNREADABLE, track_order_open(&order, config));
    config = configuration();
    config.track_limit = UINT16_MAX + 1u;
    CHECK_EQUAL(STORE_UNREADABLE, track_order_open(&order, config));
    config = configuration();
    config.file.source.read = NULL;
    CHECK_EQUAL(STORE_UNREADABLE, track_order_open(&order, config));
    config = configuration();
    config.file.write = NULL;
    CHECK_EQUAL(STORE_READY, track_order_open(&order, config));
    TrackOrderJob job;
    CHECK(!track_order_job_begin(&job, &order, track_order_source(&order),
                                 (TrackOrderEdit){TRACK_ORDER_CLEAR, 0, 0, 0}));
}

static void read_errors_are_distinct_from_an_empty_order(void)
{
    make_order();
    fail_reads = true;
    CHECK_EQUAL(STORE_UNREADABLE, track_order_open(&order, configuration()));
    CHECK(order.needs_reload);
    CHECK(!order.valid);
}

static bool generate_tracks(void *context, uint16_t first, uint16_t count, uint16_t *tracks)
{
    (void)context;
    for (uint32_t index = 0; index < count; index++) {
        tracks[index] = (uint16_t)(first + index);
    }
    return true;
}

static void a_full_library_order_is_streamed_with_bounded_transfers(void)
{
    reset();
    TrackOrderConfig config = configuration();
    config.track_limit = TRACK_ORDER_CAPACITY;
    CHECK_EQUAL(STORE_EMPTY, track_order_open(&order, config));
    TrackOrderJob job;
    TrackOrderSource source = {generate_tracks, NULL, TRACK_ORDER_CAPACITY};
    CHECK(track_order_job_begin(&job, &order, source, (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
    CHECK_EQUAL(STORE_DONE, finish(&job));
    CHECK_EQUAL(TRACK_ORDER_CAPACITY, order.count);
    CHECK_EQUAL(514, last_steps);
    CHECK_EQUAL(258, writes);
    payload_reads[0] = 0;
    CHECK_EQUAL(STORE_READY, track_order_open(&order, config));
    CHECK_EQUAL(256, payload_reads[0]);
    int before = writes;
    CHECK(!track_order_job_begin(&job, &order, track_order_source(&order),
                                 (TrackOrderEdit){TRACK_ORDER_INSERT, 0, 0, 1}));
    CHECK_EQUAL(before, writes);
    CHECK_EQUAL(STORE_DONE,
                apply((TrackOrderEdit){TRACK_ORDER_MOVE, TRACK_ORDER_CAPACITY - 1, 0, 0}));
    CHECK_EQUAL(STORE_READY, track_order_open(&order, config));
    uint16_t first = 0;
    uint16_t last = 0;
    CHECK(track_order_read(&order, 0, 1, &first));
    CHECK(track_order_read(&order, TRACK_ORDER_CAPACITY - 1, 1, &last));
    CHECK_EQUAL(TRACK_ORDER_CAPACITY - 1, first);
    CHECK_EQUAL(TRACK_ORDER_CAPACITY - 2, last);
    CHECK(max_transfer <= 512);
}

static void put32(uint32_t offset, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        disk[offset + (uint32_t)index] = (uint8_t)(value >> (8 * index));
    }
}

static void generation_wrap_keeps_the_newest_valid_commit(void)
{
    make_order();
    uint32_t offset = (uint32_t)order.bank * TRACK_ORDER_BANK_BYTES;
    put32(offset + GENERATION_FIELD, UINT32_MAX);
    put32(offset + CHECK_FIELD, hash_update(HASH_START, disk + offset, CHECK_FIELD));
    CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    CHECK_EQUAL(UINT32_MAX, order.generation);
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    CHECK_EQUAL(0, order.generation);
    CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    CHECK_EQUAL(0, order.generation);
    static const uint16_t MOVED[] = {9, 7, 3, 7};
    check_tracks(&order, MOVED, 4);
}

static void many_edits_across_block_boundaries_match_a_simple_array_model(void)
{
    static uint16_t expected[2049];
    static uint16_t actual[2049];
    uint16_t count = 1025;
    for (uint16_t index = 0; index < count; index++) {
        expected[index] = (uint16_t)(index % TRACK_LIMIT);
    }
    reset();
    TrackOrderSource source = {read_tracks, expected, count};
    TrackOrderJob job;
    CHECK(track_order_job_begin(&job, &order, source, (TrackOrderEdit){TRACK_ORDER_COPY, 0, 0, 0}));
    CHECK_EQUAL(STORE_DONE, finish(&job));
    Random random = random_seeded(123);
    for (int iteration = 0; iteration < 128; iteration++) {
        uint16_t from = (uint16_t)random_below(&random, count);
        uint16_t to = (uint16_t)random_below(&random, count);
        uint16_t track = (uint16_t)random_below(&random, TRACK_LIMIT);
        switch (random_below(&random, 3)) {
        case 0: {
            uint16_t moved = expected[from];
            if (from < to) {
                memmove(expected + from, expected + from + 1,
                        (size_t)(to - from) * sizeof *expected);
            } else {
                memmove(expected + to + 1, expected + to, (size_t)(from - to) * sizeof *expected);
            }
            expected[to] = moved;
            CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_MOVE, from, to, 0}));
            break;
        }
        case 1:
            memmove(expected + from, expected + from + 1,
                    (size_t)(count - from - 1) * sizeof *expected);
            count--;
            CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_REMOVE, from, 0, 0}));
            break;
        case 2:
            memmove(expected + from + 1, expected + from,
                    (size_t)(count - from) * sizeof *expected);
            expected[from] = track;
            count++;
            CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_INSERT, from, 0, track}));
            break;
        }
        CHECK_EQUAL(count, order.count);
        CHECK(track_order_read(&order, 0, count, actual));
        CHECK(memcmp(expected, actual, (size_t)count * sizeof *expected) == 0);
        CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    }
    CHECK(max_transfer <= 512);
}

static void a_zero_initialized_job_is_idle_and_can_be_cancelled_safely(void)
{
    TrackOrderJob job = {0};
    CHECK_EQUAL(STORE_IDLE, track_order_job_step(&job));
    track_order_job_abort(&job);
    CHECK_EQUAL(STORE_IDLE, track_order_job_step(&job));
}

static void a_valid_future_format_is_not_overwritten_as_an_empty_file(void)
{
    make_order();
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    uint32_t offset = (uint32_t)order.bank * TRACK_ORDER_BANK_BYTES;
    disk[offset + VERSION_FIELD] = 2;
    put32(offset + CHECK_FIELD, hash_update(HASH_START, disk + offset, CHECK_FIELD));
    int before = writes;
    CHECK_EQUAL(STORE_UNSUPPORTED, track_order_open(&order, configuration()));
    CHECK(order.needs_reload);
    TrackOrderJob job;
    CHECK(!track_order_job_begin(&job, &order, (TrackOrderSource){0},
                                 (TrackOrderEdit){TRACK_ORDER_CLEAR, 0, 0, 0}));
    CHECK_EQUAL(before, writes);
}

static void opening_verifies_only_the_payload_of_the_bank_it_selects(void)
{
    static const uint16_t MOVED[] = {9, 7, 3, 7};
    make_order();
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    uint8_t newest = order.bank;
    payload_reads[0] = 0;
    payload_reads[1] = 0;
    CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    CHECK_EQUAL(newest, order.bank);
    CHECK_EQUAL(1, payload_reads[newest]);
    CHECK_EQUAL(0, payload_reads[1 - newest]);
    check_tracks(&order, MOVED, 4);
    payload_reads[0] = 0;
    payload_reads[1] = 0;
    CHECK_EQUAL(STORE_READY, track_order_open_at(&order, configuration(), order.generation - 1));
    CHECK_EQUAL(1 - newest, order.bank);
    CHECK_EQUAL(0, payload_reads[newest]);
}

static void space_left_over_from_another_file_reads_as_no_order(void)
{
    static const uint16_t TRACKS[] = {7, 3, 7, 9};
    reset();
    memset(disk + TRACK_ORDER_BANK_BYTES, 0xa5, TRACK_ORDER_BANK_BYTES);
    CHECK_EQUAL(STORE_EMPTY, track_order_open(&order, configuration()));
    CHECK(!order.needs_reload);
    make_order_in_place();
    CHECK_EQUAL(STORE_READY, track_order_open(&order, configuration()));
    check_tracks(&order, TRACKS, 4);
}

static void progress_runs_from_nothing_to_complete(void)
{
    make_order();
    TrackOrderJob job;
    CHECK(track_order_job_begin(&job, &order, track_order_source(&order),
                                (TrackOrderEdit){TRACK_ORDER_MOVE, 3, 0, 0}));
    uint8_t before = track_order_job_progress(&job);
    CHECK_EQUAL(0, before);
    while (track_order_job_step(&job) == STORE_WORKING) {
        uint8_t now = track_order_job_progress(&job);
        CHECK(now >= before && now <= 100);
        before = now;
    }
    CHECK_EQUAL(100, track_order_job_progress(&job));
}

static void an_exact_generation_ignores_a_newer_unpublished_bank(void)
{
    make_order();
    uint32_t generation = order.generation;
    CHECK_EQUAL(STORE_DONE, apply((TrackOrderEdit){TRACK_ORDER_REMOVE, 1, 0, 0}));
    TrackOrder reopened;
    CHECK_EQUAL(STORE_READY, track_order_open_at(&reopened, configuration(), generation));
    check_tracks(&reopened, (uint16_t[]){7, 3, 7, 9}, 4);
    CHECK_EQUAL(STORE_EMPTY, track_order_open_at(&reopened, configuration(), generation + 2));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(an_exact_generation_ignores_a_newer_unpublished_bank),
        TEST_CASE(opening_verifies_only_the_payload_of_the_bank_it_selects),
        TEST_CASE(space_left_over_from_another_file_reads_as_no_order),
        TEST_CASE(progress_runs_from_nothing_to_complete),
        TEST_CASE(a_committed_order_survives_reopening),
        TEST_CASE(a_zero_initialized_job_is_idle_and_can_be_cancelled_safely),
        TEST_CASE(a_valid_future_format_is_not_overwritten_as_an_empty_file),
        TEST_CASE(moves_removals_and_insertions_preserve_duplicate_entries),
        TEST_CASE(empty_and_cleared_orders_are_valid_persistent_lists),
        TEST_CASE(invalid_edits_and_ranges_never_touch_storage),
        TEST_CASE(abort_and_each_precommit_power_cut_preserve_the_previous_order),
        TEST_CASE(torn_writes_keep_a_valid_copy_and_report_completed_commits_truthfully),
        TEST_CASE(a_failed_commit_readback_requires_reload_before_another_edit),
        TEST_CASE(silent_payload_corruption_is_detected_before_publication),
        TEST_CASE(source_failures_and_invalid_track_ids_do_not_publish_partial_data),
        TEST_CASE(a_second_job_cannot_overwrite_an_active_transaction),
        TEST_CASE(a_damaged_latest_bank_falls_back_to_the_previous_commit),
        TEST_CASE(another_library_and_invalid_storage_are_not_opened_as_valid_orders),
        TEST_CASE(read_errors_are_distinct_from_an_empty_order),
        TEST_CASE(a_full_library_order_is_streamed_with_bounded_transfers),
        TEST_CASE(generation_wrap_keeps_the_newest_valid_commit),
        TEST_CASE(many_edits_across_block_boundaries_match_a_simple_array_model),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
