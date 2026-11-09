#include <string.h>

#include "check.h"
#include "playback/mix_catalog.h"

static uint8_t disk[MIX_CATALOG_BYTES];
static MixCatalog catalog;
static bool fail_entry_reads;
static int writes;
static int fail_write;
static bool acknowledge_failure;
static bool fail_commit_read;

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    if ((fail_commit_read && writes == 7 && length == MIX_HEADER_BYTES) ||
        (fail_entry_reads && length == MIX_ENTRY_BYTES) || offset > sizeof disk ||
        length > sizeof disk - offset) {
        return false;
    }
    memcpy(bytes, disk + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    if (offset > sizeof disk || length > sizeof disk - offset) {
        return false;
    }
    writes++;
    if (writes == fail_write && !acknowledge_failure) {
        memcpy(disk + offset, bytes, length / 2);
        return false;
    }
    memcpy(disk + offset, bytes, length);
    return writes != fail_write;
}

static MixIO io(void)
{
    return (MixIO){.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
}

static void a_named_mixtape_survives_a_restart(void)
{
    memset(disk, 0, sizeof disk);
    CHECK_EQUAL(MIX_EMPTY, mix_catalog_open(&catalog, io()));
    MixPatch patch = {.slot = 7,
                      .entry = {.name = "Side A",
                                .generation = 11,
                                .bytes = 1024,
                                .count = 8,
                                .file_bank = 1,
                                .active = true}};
    MixCatalogJob job = {0};
    CHECK(mix_catalog_begin(&job, &catalog, 0, patch));
    for (int step = 0; step < 32 && job.result == MIX_WORKING; step++) {
        mix_catalog_step(&job);
    }
    CHECK_EQUAL(MIX_DONE, job.result);
    CHECK_EQUAL(MIX_READY, mix_catalog_open(&catalog, io()));
    MixEntry entry;
    CHECK(mix_catalog_read(&catalog, 7, &entry));
    CHECK(strcmp(entry.name, "Side A") == 0);
    CHECK_EQUAL(11, entry.generation);
    CHECK_EQUAL(8, entry.count);
    CHECK_EQUAL(1, entry.file_bank);
}

static void entry_read_failures_are_not_reported_as_empty_or_corrupt_data(void)
{
    a_named_mixtape_survives_a_restart();
    fail_entry_reads = true;
    CHECK_EQUAL(MIX_UNREADABLE, mix_catalog_open(&catalog, io()));
    CHECK(catalog.needs_reload);
    fail_entry_reads = false;
}

static MixPatch renamed(void)
{
    return (MixPatch){
        .slot = 7,
        .entry = {.name = "Side B", .generation = 12, .bytes = 256, .count = 2, .active = true}};
}

static void finish_job(MixCatalogJob *job)
{
    for (int step = 0; step < 32 && job->result == MIX_WORKING; step++) {
        mix_catalog_step(job);
    }
}

static void a_torn_write_retains_a_complete_old_or_new_catalog(void)
{
    for (int failure = 1; failure <= 7; failure++) {
        fail_write = 0;
        a_named_mixtape_survives_a_restart();
        writes = 0;
        fail_write = failure;
        MixCatalogJob job;
        CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
        finish_job(&job);
        CHECK_EQUAL(failure == 7 ? MIX_DONE : MIX_FAILED, job.result);
        CHECK_EQUAL(MIX_READY, mix_catalog_open(&catalog, io()));
        MixEntry entry;
        CHECK(mix_catalog_read(&catalog, 7, &entry));
        CHECK(strcmp(entry.name, failure == 7 ? "Side B" : "Side A") == 0);
    }
    fail_write = 0;
}

static void readback_resolves_a_reported_header_write_failure(void)
{
    a_named_mixtape_survives_a_restart();
    writes = 0;
    fail_write = 7;
    acknowledge_failure = true;
    MixCatalogJob job;
    CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    finish_job(&job);
    CHECK_EQUAL(MIX_DONE, job.result);
    acknowledge_failure = false;
    fail_write = 0;
}

static void an_unreadable_commit_blocks_further_edits_until_reopened(void)
{
    a_named_mixtape_survives_a_restart();
    writes = 0;
    fail_commit_read = true;
    MixCatalogJob job;
    CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    finish_job(&job);
    CHECK_EQUAL(MIX_UNCERTAIN, job.result);
    CHECK(catalog.needs_reload);
    CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    fail_commit_read = false;
    CHECK_EQUAL(MIX_READY, mix_catalog_open(&catalog, io()));
    MixEntry entry;
    CHECK(mix_catalog_read(&catalog, 7, &entry));
    CHECK(strcmp(entry.name, "Side B") == 0);
}

static void cancellation_keeps_the_published_name_and_rejects_stale_edits(void)
{
    for (int cutoff = 0; cutoff < 12; cutoff++) {
        a_named_mixtape_survives_a_restart();
        MixCatalogJob job;
        CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation - 1, renamed()));
        CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
        CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
        for (int step = 0; step < cutoff; step++) {
            CHECK_EQUAL(MIX_WORKING, mix_catalog_step(&job));
        }
        mix_catalog_abort(&job);
        CHECK(!catalog.busy);
        CHECK_EQUAL(MIX_READY, mix_catalog_open(&catalog, io()));
        MixEntry entry;
        CHECK(mix_catalog_read(&catalog, 7, &entry));
        CHECK(strcmp(entry.name, "Side A") == 0);
    }
}

static void a_damaged_latest_payload_falls_back_to_the_previous_catalog(void)
{
    a_named_mixtape_survives_a_restart();
    MixCatalogJob job;
    CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    finish_job(&job);
    CHECK_EQUAL(MIX_DONE, job.result);
    disk[catalog.bank * MIX_BANK_BYTES + MIX_HEADER_BYTES] ^= 1;
    CHECK_EQUAL(MIX_READY, mix_catalog_open(&catalog, io()));
    MixEntry entry;
    CHECK(mix_catalog_read(&catalog, 7, &entry));
    CHECK(strcmp(entry.name, "Side A") == 0);
}

static void empty_mixtapes_and_entries_crossing_sector_boundaries_are_preserved(void)
{
    memset(disk, 0, sizeof disk);
    CHECK_EQUAL(MIX_EMPTY, mix_catalog_open(&catalog, io()));
    for (uint16_t slot = 0; slot < MIX_CATALOG_SLOTS; slot++) {
        MixPatch patch = {.slot = slot,
                          .entry = {.name = "Empty", .generation = slot, .active = true}};
        MixCatalogJob job;
        CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, patch));
        finish_job(&job);
        CHECK_EQUAL(MIX_DONE, job.result);
    }
    CHECK_EQUAL(MIX_READY, mix_catalog_open(&catalog, io()));
    for (uint16_t slot = 0; slot < MIX_CATALOG_SLOTS; slot++) {
        MixEntry entry;
        CHECK(mix_catalog_read(&catalog, slot, &entry));
        CHECK_EQUAL(slot, entry.generation);
        CHECK(entry.active);
        CHECK_EQUAL(0, entry.count);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_named_mixtape_survives_a_restart),
        TEST_CASE(entry_read_failures_are_not_reported_as_empty_or_corrupt_data),
        TEST_CASE(a_torn_write_retains_a_complete_old_or_new_catalog),
        TEST_CASE(readback_resolves_a_reported_header_write_failure),
        TEST_CASE(an_unreadable_commit_blocks_further_edits_until_reopened),
        TEST_CASE(cancellation_keeps_the_published_name_and_rejects_stale_edits),
        TEST_CASE(a_damaged_latest_payload_falls_back_to_the_previous_catalog),
        TEST_CASE(empty_mixtapes_and_entries_crossing_sector_boundaries_are_preserved)};
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
