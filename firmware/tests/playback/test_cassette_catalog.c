#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "playback/mix_catalog.h"

static uint8_t disk[MIX_CATALOG_BYTES];

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    if (offset > sizeof disk || length > sizeof disk - offset) {
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
    memcpy(disk + offset, bytes, length);
    return true;
}

static StoreFile io(void)
{
    return (StoreFile){.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
}

static MixEntry sample(void)
{
    return (MixEntry){.name = "Night drive",
                      .generation = 3,
                      .bytes = 300,
                      .count = 3,
                      .active = true,
                      .side_a_count = 2,
                      .side_minutes = 30};
}

static void a_two_sided_catalogue_keeps_its_split_and_budget_after_restart(void)
{
    memset(disk, 0, sizeof disk);
    MixCatalog catalog;
    CHECK_EQUAL(STORE_EMPTY, mix_catalog_open(&catalog, io()));
    MixCatalogJob job = {0};
    CHECK(mix_catalog_begin(&job, &catalog, 0, (MixPatch){sample(), 0}));
    for (int step = 0; step < 30 && job.write.result == STORE_WORKING; step++) {
        mix_catalog_step(&job);
    }
    CHECK_EQUAL(STORE_DONE, job.write.result);
    CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, io()));
    MixEntry entry = {0};
    CHECK(mix_catalog_read(&catalog, 0, &entry));
    CHECK_EQUAL(30, entry.side_minutes);
    CHECK_EQUAL(2, entry.side_a_count);
}

static void impossible_splits_budgets_and_side_counts_are_refused(void)
{
    memset(disk, 0, sizeof disk);
    MixCatalog catalog;
    CHECK_EQUAL(STORE_EMPTY, mix_catalog_open(&catalog, io()));
    MixEntry entry = sample();
    MixCatalogJob job = {0};
    entry.side_minutes = 31;
    CHECK(!mix_catalog_begin(&job, &catalog, 0, (MixPatch){entry, 0}));
    entry = sample();
    entry.side_a_count = 4;
    CHECK(!mix_catalog_begin(&job, &catalog, 0, (MixPatch){entry, 0}));
    entry = sample();
    entry.count = 67;
    entry.bytes = 1000;
    CHECK(!mix_catalog_begin(&job, &catalog, 0, (MixPatch){entry, 0}));
}

static void an_older_single_sided_catalogue_loads_without_changing_its_bytes(void)
{
    memset(disk, 0, sizeof disk);
    MixCatalog catalog;
    CHECK_EQUAL(STORE_EMPTY, mix_catalog_open(&catalog, io()));
    MixEntry entry = sample();
    entry.side_minutes = 0;
    entry.side_a_count = 0;
    MixCatalogJob job = {0};
    CHECK(mix_catalog_begin(&job, &catalog, 0, (MixPatch){entry, 0}));
    while (mix_catalog_step(&job) == STORE_WORKING) {
    }
    store_put16(disk + 4, 1);
    store_put32(disk + 24, hash_update(HASH_START, disk, 24));
    uint8_t before[MIX_CATALOG_BYTES];
    memcpy(before, disk, sizeof before);
    CHECK_EQUAL(STORE_READY, mix_catalog_open(&catalog, io()));
    CHECK(mix_catalog_read(&catalog, 0, &entry));
    CHECK_EQUAL(0, entry.side_minutes);
    CHECK_EQUAL(3, entry.count);
    CHECK(memcmp(before, disk, sizeof disk) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_two_sided_catalogue_keeps_its_split_and_budget_after_restart),
        TEST_CASE(impossible_splits_budgets_and_side_counts_are_refused),
        TEST_CASE(an_older_single_sided_catalogue_loads_without_changing_its_bytes),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
