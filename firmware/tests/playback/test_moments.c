#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "fixture_library.h"
#include "playback/moments.h"

static uint8_t disk[MOMENTS_FILE_BYTES];
static uint8_t saved[MOMENTS_FILE_BYTES];
static Moments moments;
static uint32_t writes;
static uint32_t fail_write;
static uint32_t cut_bytes;
static bool unreadable;
static bool lose_publication_readback;

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    if (unreadable || offset > sizeof disk || length > sizeof disk - offset ||
        length > STORE_BLOCK_BYTES) {
        return false;
    }
    memcpy(bytes, disk + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    if (offset > sizeof disk || length > sizeof disk - offset || length > STORE_BLOCK_BYTES) {
        return false;
    }
    writes++;
    bool failed = writes == fail_write;
    uint32_t amount = failed && cut_bytes < length ? cut_bytes : length;
    memcpy(disk + offset, bytes, amount);
    if (lose_publication_readback && length == STORE_BLOCK_BYTES &&
        memcmp(bytes, "DSMO", STORE_MAGIC_BYTES) == 0) {
        unreadable = true;
    }
    return !failed;
}

static StoreFile file_io(void)
{
    return (StoreFile){.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
}

static Moment sample(uint32_t position)
{
    return (Moment){.identity = {.path = "Album/image.flac", .start_ms = 60000, .end_ms = 180000},
                    .position_ms = position,
                    .label = "Song title 0:12"};
}

static StoreResult finish(MomentsJob *job)
{
    StoreResult result = STORE_WORKING;
    for (int step = 0; step < 1000 && result == STORE_WORKING; step++) {
        result = moments_step(job);
    }
    CHECK(result != STORE_WORKING);
    return result;
}

static StoreResult add(const Moment *moment)
{
    MomentsJob job = {0};
    if (!moments_add(&job, &moments, moments.generation, moment)) {
        return STORE_FAILED;
    }
    return finish(&job);
}

static void reset(void)
{
    memset(disk, 0, sizeof disk);
    writes = 0;
    fail_write = 0;
    cut_bytes = 0;
    unreadable = false;
    lose_publication_readback = false;
    CHECK_EQUAL(STORE_EMPTY, moments_open(&moments, file_io()));
    CHECK_EQUAL(0, moments.count);
}

static void restart_preserves_identity_position_and_label_newest_first(void)
{
    reset();
    Moment first = sample(12345);
    CHECK_EQUAL(STORE_DONE, add(&first));
    Moment second = sample(45678);
    strcpy(second.label, "Song title 0:45");
    CHECK_EQUAL(STORE_DONE, add(&second));
    CHECK_EQUAL(STORE_READY, moments_open(&moments, file_io()));
    CHECK_EQUAL(2, moments.count);
    Moment restored = {0};
    CHECK(moments_read(&moments, 0, &restored));
    CHECK(mix_identity_equal(&second.identity, &restored.identity));
    CHECK_EQUAL(45678, restored.position_ms);
    CHECK(strcmp(second.label, restored.label) == 0);
    CHECK(moments_read(&moments, 1, &restored));
    CHECK_EQUAL(12345, restored.position_ms);
    CHECK(strcmp(first.label, restored.label) == 0);
    CHECK(!moments_read(&moments, 2, &restored));
}

static void deleting_a_middle_moment_keeps_the_other_positions_and_order(void)
{
    reset();
    for (uint32_t index = 1; index <= 3; index++) {
        Moment moment = sample(index * 1000);
        CHECK_EQUAL(STORE_DONE, add(&moment));
    }
    MomentsJob job = {0};
    CHECK(moments_delete(&job, &moments, moments.generation, 1));
    CHECK_EQUAL(STORE_DONE, finish(&job));
    CHECK_EQUAL(STORE_READY, moments_open(&moments, file_io()));
    CHECK_EQUAL(2, moments.count);
    Moment restored = {0};
    CHECK(moments_read(&moments, 0, &restored));
    CHECK_EQUAL(3000, restored.position_ms);
    CHECK(moments_read(&moments, 1, &restored));
    CHECK_EQUAL(1000, restored.position_ms);
    CHECK(!moments_delete(&job, &moments, moments.generation, 2));
}

static void a_full_list_refuses_an_add_without_discarding_saved_moments(void)
{
    reset();
    CHECK_EQUAL(64, MOMENTS_CAPACITY);
    for (uint32_t index = 0; index < MOMENTS_CAPACITY; index++) {
        Moment moment = sample(index);
        CHECK_EQUAL(STORE_DONE, add(&moment));
    }
    uint32_t previous_writes = writes;
    Moment extra = sample(99999);
    CHECK_EQUAL(STORE_FAILED, add(&extra));
    CHECK_EQUAL(previous_writes, writes);
    CHECK_EQUAL(STORE_READY, moments_open(&moments, file_io()));
    CHECK_EQUAL(MOMENTS_CAPACITY, moments.count);
    Moment restored = {0};
    CHECK(moments_read(&moments, MOMENTS_CAPACITY - 1, &restored));
    CHECK_EQUAL(0, restored.position_ms);
}

static void invalid_paths_boundaries_positions_and_labels_never_write(void)
{
    reset();
    Moment moment = sample(12000);
    strcpy(moment.identity.path, "Album/../image.flac");
    CHECK_EQUAL(STORE_FAILED, add(&moment));
    moment = sample(12000);
    moment.identity.end_ms = moment.identity.start_ms;
    CHECK_EQUAL(STORE_FAILED, add(&moment));
    moment = sample(120001);
    CHECK_EQUAL(STORE_FAILED, add(&moment));
    moment = sample(12000);
    moment.label[0] = '\n';
    CHECK_EQUAL(STORE_FAILED, add(&moment));
    moment = sample(12000);
    memset(moment.label, 'x', sizeof moment.label);
    CHECK_EQUAL(STORE_FAILED, add(&moment));
    CHECK_EQUAL(0, writes);
}

static void stale_and_concurrent_edits_are_refused(void)
{
    reset();
    uint32_t revision = moments.generation;
    Moment moment = sample(1000);
    CHECK_EQUAL(STORE_DONE, add(&moment));
    MomentsJob job = {0};
    CHECK(!moments_add(&job, &moments, revision, &moment));
    CHECK(!moments_delete(&job, &moments, revision, 0));
    CHECK(moments_add(&job, &moments, moments.generation, &moment));
    MomentsJob other;
    CHECK(!moments_add(&other, &moments, moments.generation, &moment));
    CHECK(!moments_delete(&other, &moments, moments.generation, 0));
    moments_abort(&job);
    CHECK_EQUAL(STORE_DONE, add(&moment));
}

static uint32_t prepare_second_save(void)
{
    reset();
    Moment first = sample(1000);
    CHECK_EQUAL(STORE_DONE, add(&first));
    memcpy(saved, disk, sizeof saved);
    uint32_t before = writes;
    Moment second = sample(2000);
    CHECK_EQUAL(STORE_DONE, add(&second));
    return writes - before;
}

static void restore_saved(void)
{
    memcpy(disk, saved, sizeof disk);
    writes = 0;
    fail_write = 0;
    unreadable = false;
    lose_publication_readback = false;
    CHECK_EQUAL(STORE_READY, moments_open(&moments, file_io()));
}

static void check_restarted_list(StoreResult result)
{
    CHECK_EQUAL(STORE_READY, moments_open(&moments, file_io()));
    CHECK_EQUAL(result == STORE_DONE ? 2 : 1, moments.count);
    Moment restored = {0};
    CHECK(moments_read(&moments, 0, &restored));
    CHECK_EQUAL(result == STORE_DONE ? 2000 : 1000, restored.position_ms);
    Moment expected = sample(1000);
    CHECK(mix_identity_equal(&expected.identity, &restored.identity));
}

static void a_torn_write_at_each_step_preserves_a_complete_list(void)
{
    uint32_t save_writes = prepare_second_save();
    for (uint32_t failure = 1; failure <= save_writes; failure++) {
        restore_saved();
        uint8_t active = moments.bank;
        fail_write = failure;
        cut_bytes = STORE_BLOCK_BYTES / 2;
        Moment second = sample(2000);
        StoreResult result = add(&second);
        CHECK(result == STORE_DONE || result == STORE_FAILED);
        CHECK(memcmp(disk + active * MOMENTS_BANK_BYTES, saved + active * MOMENTS_BANK_BYTES,
                     MOMENTS_BANK_BYTES) == 0);
        check_restarted_list(result);
    }
}

static void publication_is_decided_by_readback_at_every_byte_cut(void)
{
    uint32_t save_writes = prepare_second_save();
    for (uint32_t cut = 0; cut <= STORE_BLOCK_BYTES; cut++) {
        restore_saved();
        fail_write = save_writes;
        cut_bytes = cut;
        Moment second = sample(2000);
        StoreResult result = add(&second);
        CHECK(result == STORE_DONE || result == STORE_FAILED);
        check_restarted_list(result);
    }
}

static void uncertain_publication_requires_reopening_before_an_edit(void)
{
    reset();
    Moment first = sample(1000);
    CHECK_EQUAL(STORE_DONE, add(&first));
    lose_publication_readback = true;
    Moment second = sample(2000);
    CHECK_EQUAL(STORE_UNCERTAIN, add(&second));
    unreadable = false;
    lose_publication_readback = false;
    CHECK(moments.needs_reload);
    uint32_t before = writes;
    CHECK_EQUAL(STORE_FAILED, add(&first));
    MomentsJob job = {0};
    CHECK(!moments_delete(&job, &moments, moments.generation, 0));
    CHECK_EQUAL(before, writes);
    CHECK_EQUAL(STORE_READY, moments_open(&moments, file_io()));
    CHECK_EQUAL(2, moments.count);
    CHECK_EQUAL(STORE_DONE, add(&first));
}

static void a_damaged_newest_payload_falls_back_and_total_damage_refuses_edits(void)
{
    prepare_second_save();
    uint32_t newest = (uint32_t)moments.bank * MOMENTS_BANK_BYTES;
    disk[newest + STORE_BLOCK_BYTES] ^= 1;
    CHECK_EQUAL(STORE_READY, moments_open(&moments, file_io()));
    CHECK_EQUAL(1, moments.count);
    Moment restored = {0};
    CHECK(moments_read(&moments, 0, &restored));
    CHECK_EQUAL(1000, restored.position_ms);
    disk[(uint32_t)moments.bank * MOMENTS_BANK_BYTES + STORE_BLOCK_BYTES] ^= 1;
    CHECK_EQUAL(STORE_CORRUPT, moments_open(&moments, file_io()));
    uint32_t before = writes;
    CHECK_EQUAL(STORE_FAILED, add(&restored));
    CHECK_EQUAL(before, writes);
}

static void future_versions_and_unreadable_banks_are_preserved(void)
{
    reset();
    Moment moment = sample(1000);
    CHECK_EQUAL(STORE_DONE, add(&moment));
    uint8_t *header = disk + (uint32_t)moments.bank * MOMENTS_BANK_BYTES;
    store_put16(header + 4, 2);
    store_put32(header + 24, hash_update(HASH_START, header, 24));
    CHECK_EQUAL(STORE_UNSUPPORTED, moments_open(&moments, file_io()));
    uint32_t before = writes;
    CHECK_EQUAL(STORE_FAILED, add(&moment));
    CHECK_EQUAL(before, writes);
    unreadable = true;
    CHECK_EQUAL(STORE_UNREADABLE, moments_open(&moments, file_io()));
    unreadable = false;
    CHECK_EQUAL(STORE_FAILED, add(&moment));
    CHECK_EQUAL(before, writes);
}

static void identity_resolution_survives_rescan_and_refuses_changed_or_ambiguous_parts(void)
{
    FixtureLibrary fixture;
    Library *library = fixture_library_open(&fixture);
    Moment moment = {.position_ms = 12345, .label = "Saved song 0:12"};
    CHECK_EQUAL(STORE_READY, mix_identity_read(library, 0, &moment.identity));
    LibrarySectionInfo tracks = library->sections[LIBRARY_TRACKS];
    uint8_t temporary[64];
    CHECK(tracks.record_size <= sizeof temporary);
    memcpy(temporary, fixture.bytes + tracks.offset, tracks.record_size);
    memcpy(fixture.bytes + tracks.offset, fixture.bytes + tracks.offset + tracks.record_size,
           tracks.record_size);
    memcpy(fixture.bytes + tracks.offset + tracks.record_size, temporary, tracks.record_size);
    MomentResolve job;
    moment_resolve_begin(&job, library, &moment.identity);
    while (moment_resolve_step(&job) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_DONE, job.result);
    CHECK_EQUAL(1, job.track);
    moment.identity.start_ms++;
    moment_resolve_begin(&job, library, &moment.identity);
    while (moment_resolve_step(&job) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_FAILED, job.result);
    moment.identity.start_ms--;
    memcpy(fixture.bytes + tracks.offset, temporary, tracks.record_size);
    moment_resolve_begin(&job, library, &moment.identity);
    while (moment_resolve_step(&job) == STORE_WORKING) {
    }
    CHECK_EQUAL(STORE_FAILED, job.result);
    CHECK_EQUAL(2, job.matches);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(restart_preserves_identity_position_and_label_newest_first),
        TEST_CASE(identity_resolution_survives_rescan_and_refuses_changed_or_ambiguous_parts),
        TEST_CASE(deleting_a_middle_moment_keeps_the_other_positions_and_order),
        TEST_CASE(a_full_list_refuses_an_add_without_discarding_saved_moments),
        TEST_CASE(invalid_paths_boundaries_positions_and_labels_never_write),
        TEST_CASE(stale_and_concurrent_edits_are_refused),
        TEST_CASE(a_torn_write_at_each_step_preserves_a_complete_list),
        TEST_CASE(publication_is_decided_by_readback_at_every_byte_cut),
        TEST_CASE(uncertain_publication_requires_reopening_before_an_edit),
        TEST_CASE(a_damaged_newest_payload_falls_back_and_total_damage_refuses_edits),
        TEST_CASE(future_versions_and_unreadable_banks_are_preserved),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
