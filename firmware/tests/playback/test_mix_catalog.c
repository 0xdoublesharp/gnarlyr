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
    if ((fail_commit_read && writes == 7 && length == STORE_BLOCK_BYTES) ||
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

static StoreFile io(void)
{
    return (StoreFile){.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
}

static void a_named_mixtape_survives_a_restart(void)
{
    memset(disk, 0, sizeof disk);
    CHECK_EQUAL(STORE_EMPTY, mix_catalog_open(&catalog, io()));
    MixPatch patch = {.slot = 7,
                      .entry = {.name = "Side A",
                                .generation = 11,
                                .bytes = 1024,
                                .count = 8,
                                .file_bank = 1,
                                .active = true}};
    MixCatalogJob job = {0};
    CHECK(mix_catalog_begin(&job, &catalog, 0, patch));
    for (int step = 0; step < 32 && job.write.result == STORE_WORKING; step++) {
        mix_catalog_step(&job);
    }
    CHECK_EQUAL(STORE_DONE, job.write.result);
    CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, io()));
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
    CHECK_EQUAL(STORE_UNREADABLE, mix_catalog_open(&catalog, io()));
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
    for (int step = 0; step < 32 && job->write.result == STORE_WORKING; step++) {
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
        CHECK_EQUAL(failure == 7 ? STORE_DONE : STORE_FAILED, job.write.result);
        CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, io()));
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
    CHECK_EQUAL(STORE_DONE, job.write.result);
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
    CHECK_EQUAL(STORE_UNCERTAIN, job.write.result);
    CHECK(catalog.needs_reload);
    CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    fail_commit_read = false;
    CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, io()));
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
            CHECK_EQUAL(STORE_WORKING, mix_catalog_step(&job));
        }
        mix_catalog_abort(&job);
        CHECK(!catalog.busy);
        CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, io()));
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
    CHECK_EQUAL(STORE_DONE, job.write.result);
    disk[catalog.bank * MIX_BANK_BYTES + STORE_BLOCK_BYTES] ^= 1;
    CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, io()));
    MixEntry entry;
    CHECK(mix_catalog_read(&catalog, 7, &entry));
    CHECK(strcmp(entry.name, "Side A") == 0);
}

static void empty_mixtapes_and_entries_crossing_sector_boundaries_are_preserved(void)
{
    memset(disk, 0, sizeof disk);
    CHECK_EQUAL(STORE_EMPTY, mix_catalog_open(&catalog, io()));
    for (uint16_t slot = 0; slot < MIX_CATALOG_SLOTS; slot++) {
        MixPatch patch = {.slot = slot,
                          .entry = {.name = "Empty", .generation = slot, .active = true}};
        MixCatalogJob job;
        CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, patch));
        finish_job(&job);
        CHECK_EQUAL(STORE_DONE, job.write.result);
    }
    CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, io()));
    for (uint16_t slot = 0; slot < MIX_CATALOG_SLOTS; slot++) {
        MixEntry entry;
        CHECK(mix_catalog_read(&catalog, slot, &entry));
        CHECK_EQUAL(slot, entry.generation);
        CHECK(entry.active);
        CHECK_EQUAL(0, entry.count);
    }
}

static void a_new_file_with_old_card_contents_past_4_kb_is_an_empty_catalogue(void)
{
    memset(disk, 0, sizeof disk);
    memset(disk + 4096, 0xa5, sizeof disk - 4096);
    CHECK_EQUAL(STORE_EMPTY, mix_catalog_open(&catalog, io()));
    CHECK(!catalog.needs_reload);
    MixCatalogJob job;
    CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    finish_job(&job);
    CHECK_EQUAL(STORE_DONE, job.write.result);
    CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    finish_job(&job);
    CHECK_EQUAL(STORE_DONE, job.write.result);
    CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, io()));
    CHECK_EQUAL(2, catalog.generation);
    MixEntry entry;
    CHECK(mix_catalog_read(&catalog, 7, &entry));
    CHECK(strcmp(entry.name, "Side B") == 0);
    CHECK(mix_catalog_read(&catalog, 8, &entry));
    CHECK(!entry.active);
}

static void a_catalogue_damaged_in_both_banks_refuses_edits(void)
{
    a_named_mixtape_survives_a_restart();
    MixCatalogJob job;
    CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    finish_job(&job);
    CHECK_EQUAL(STORE_DONE, job.write.result);
    disk[STORE_BLOCK_BYTES] ^= 1;
    disk[MIX_BANK_BYTES + STORE_BLOCK_BYTES] ^= 1;
    int before = writes;
    CHECK_EQUAL(STORE_CORRUPT, mix_catalog_open(&catalog, io()));
    CHECK(catalog.needs_reload);
    MixEntry entry;
    CHECK(!mix_catalog_read(&catalog, 7, &entry));
    CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    CHECK_EQUAL(before, writes);
}

static void the_previous_catalogue_stays_readable_after_an_edit(void)
{
    a_named_mixtape_survives_a_restart();
    MixCatalogJob job;
    CHECK(mix_catalog_begin(&job, &catalog, catalog.generation, renamed()));
    finish_job(&job);
    CHECK_EQUAL(STORE_DONE, job.write.result);
    MixCatalog older = catalog;
    CHECK_EQUAL(STORE_READY, mix_catalog_previous(&older));
    MixEntry entry;
    CHECK(mix_catalog_read(&older, 7, &entry));
    CHECK(strcmp(entry.name, "Side A") == 0);
    CHECK_EQUAL(STORE_EMPTY, mix_catalog_previous(&older));
    CHECK(mix_catalog_read(&catalog, 7, &entry));
    CHECK(strcmp(entry.name, "Side B") == 0);
}

static void invalid_names_and_sizes_never_reach_storage(void)
{
    a_named_mixtape_survives_a_restart();
    int before = writes;
    MixCatalogJob job;
    MixPatch patch = renamed();
    patch.entry.name[0] = 0;
    CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation, patch));
    patch = renamed();
    patch.entry.name[1] = 7;
    CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation, patch));
    patch = renamed();
    memset(patch.entry.name, 'x', MIX_NAME_BYTES);
    CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation, patch));
    patch = renamed();
    patch.entry.bytes = 21;
    CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation, patch));
    patch = renamed();
    patch.slot = MIX_CATALOG_SLOTS;
    CHECK(!mix_catalog_begin(&job, &catalog, catalog.generation, patch));
    CHECK_EQUAL(before, writes);
    CHECK(memcmp(disk + catalog.bank * MIX_BANK_BYTES, "DSMC", 4) == 0);
    CHECK(strcmp(MIX_CATALOG_PATH + strlen(MIX_CATALOG_PATH) - 10, "/mixes.bin") == 0);
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
        TEST_CASE(empty_mixtapes_and_entries_crossing_sector_boundaries_are_preserved),
        TEST_CASE(a_new_file_with_old_card_contents_past_4_kb_is_an_empty_catalogue),
        TEST_CASE(a_catalogue_damaged_in_both_banks_refuses_edits),
        TEST_CASE(the_previous_catalogue_stays_readable_after_an_edit),
        TEST_CASE(invalid_names_and_sizes_never_reach_storage),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
