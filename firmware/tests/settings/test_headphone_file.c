#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "settings/headphone_file.h"

static uint8_t disk[HEADPHONE_FILE_BYTES];
static uint32_t cut;
static bool no_read;
static bool unreadable_after_write;
static bool false_write_result;
static unsigned writes;

static bool read_disk(void *context, uint32_t offset, void *bytes, uint32_t length)
{
    (void)context;
    CHECK(offset <= sizeof disk && length <= sizeof disk - offset);
    CHECK(length <= STORE_BLOCK_BYTES);
    if (no_read) {
        return false;
    }
    memcpy(bytes, disk + offset, length);
    return true;
}

static bool write_disk(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    (void)context;
    CHECK(offset <= sizeof disk && length <= sizeof disk - offset);
    CHECK(length <= STORE_BLOCK_BYTES);
    writes++;
    memcpy(disk + offset, bytes, cut < length ? cut : length);
    no_read = unreadable_after_write;
    return cut >= length && !false_write_result;
}

static StoreFile file(void)
{
    return (StoreFile){.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
}

static HeadphoneProfiles sample(void)
{
    HeadphoneProfiles profiles = headphone_profiles_defaults();
    profiles.count = 4;
    profiles.current = 2;
    for (int index = 0; index < profiles.count; index++) {
        profiles.slots[index] = headphone_profile_defaults();
        memcpy(profiles.slots[index].name, "Example", sizeof "Example");
        profiles.slots[index].trim_half_db = (uint8_t)(index * 3);
        profiles.slots[index].volume_ceiling = (uint8_t)(60 - index * 5);
    }
    return profiles;
}

static HeadphoneFile reset(void)
{
    memset(disk, 0, sizeof disk);
    cut = STORE_BLOCK_BYTES;
    no_read = false;
    unreadable_after_write = false;
    false_write_result = false;
    writes = 0;
    HeadphoneFile owner;
    HeadphoneProfiles profiles = sample();
    CHECK_EQUAL(STORE_EMPTY, headphone_file_open(&owner, file(), &profiles));
    CHECK_EQUAL(2, profiles.current);
    return owner;
}

static void selection_and_all_four_profiles_survive_restart(void)
{
    HeadphoneFile owner = reset();
    HeadphoneProfiles profiles = sample();
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
    CHECK_EQUAL(1, owner.generation);
    profiles.current = 1;
    profiles.slots[1].balance = -57;
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
    CHECK_EQUAL(1, owner.bank);
    HeadphoneProfiles restored;
    CHECK_EQUAL(STORE_READY, headphone_file_open(&owner, file(), &restored));
    CHECK_EQUAL(1, restored.current);
    CHECK_EQUAL(4, restored.count);
    CHECK_EQUAL(-57, restored.slots[1].balance);
    CHECK_EQUAL(45, restored.slots[3].volume_ceiling);
}

static void every_torn_write_reopens_a_whole_previous_or_new_profile(void)
{
    HeadphoneFile owner = reset();
    HeadphoneProfiles profiles = sample();
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
    profiles.current = 0;
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
    uint8_t before[HEADPHONE_FILE_BYTES];
    memcpy(before, disk, sizeof before);
    for (uint32_t at = 0; at <= STORE_BLOCK_BYTES; at++) {
        memcpy(disk, before, sizeof disk);
        HeadphoneProfiles restored;
        CHECK_EQUAL(STORE_READY, headphone_file_open(&owner, file(), &restored));
        profiles.current = 3;
        profiles.slots[3].trim_half_db = 24;
        cut = at;
        StoreResult result = headphone_file_save(&owner, &profiles);
        CHECK_EQUAL(STORE_READY, headphone_file_open(&owner, file(), &restored));
        CHECK_EQUAL(result == STORE_DONE ? 3 : 0, restored.current);
        CHECK_EQUAL(result == STORE_DONE ? 24 : 9, restored.slots[3].trim_half_db);
        CHECK(memcmp(before + STORE_BLOCK_BYTES, disk + STORE_BLOCK_BYTES, STORE_BLOCK_BYTES) == 0);
    }
}

static void uncertain_publication_blocks_all_writes_until_reopen(void)
{
    HeadphoneFile owner = reset();
    HeadphoneProfiles profiles = sample();
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
    profiles.current = 1;
    unreadable_after_write = true;
    CHECK_EQUAL(STORE_UNCERTAIN, headphone_file_save(&owner, &profiles));
    CHECK(owner.needs_reload);
    unsigned before = writes;
    no_read = false;
    unreadable_after_write = false;
    CHECK_EQUAL(STORE_FAILED, headphone_file_save(&owner, &profiles));
    CHECK_EQUAL(before, writes);
    CHECK_EQUAL(STORE_READY, headphone_file_open(&owner, file(), &profiles));
    CHECK_EQUAL(1, profiles.current);
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
}

static void damaged_newest_falls_back_and_future_formats_are_preserved(void)
{
    HeadphoneFile owner = reset();
    HeadphoneProfiles profiles = sample();
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
    profiles.current = 1;
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
    disk[STORE_BLOCK_BYTES + STORE_FIELD_BYTES + 50] ^= 1;
    CHECK_EQUAL(STORE_READY, headphone_file_open(&owner, file(), &profiles));
    CHECK_EQUAL(2, profiles.current);
    disk[STORE_BLOCK_BYTES + 4] = 2;
    hash_store(disk + STORE_BLOCK_BYTES + 24,
               hash_update(HASH_START, disk + STORE_BLOCK_BYTES, 24));
    CHECK_EQUAL(STORE_UNSUPPORTED, headphone_file_open(&owner, file(), &profiles));
    unsigned before = writes;
    CHECK_EQUAL(STORE_FAILED, headphone_file_save(&owner, &profiles));
    CHECK_EQUAL(before, writes);
}

static void short_and_unreadable_files_and_invalid_models_never_write(void)
{
    HeadphoneFile owner = reset();
    HeadphoneProfiles profiles = sample();
    StoreFile short_file = file();
    short_file.size--;
    CHECK_EQUAL(STORE_UNREADABLE, headphone_file_open(&owner, short_file, &profiles));
    CHECK_EQUAL(STORE_FAILED, headphone_file_save(&owner, &profiles));
    no_read = true;
    CHECK_EQUAL(STORE_UNREADABLE, headphone_file_open(&owner, file(), &profiles));
    CHECK_EQUAL(STORE_FAILED, headphone_file_save(&owner, &profiles));
    no_read = false;
    CHECK_EQUAL(STORE_EMPTY, headphone_file_open(&owner, file(), &profiles));
    profiles.slots[0].trim_half_db = 25;
    CHECK_EQUAL(STORE_FAILED, headphone_file_save(&owner, &profiles));
    CHECK_EQUAL(0, writes);
}

static void generation_wrap_and_false_write_results_use_readback(void)
{
    HeadphoneFile owner = reset();
    HeadphoneProfiles profiles = sample();
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
    store_put32(disk + 8, UINT32_MAX);
    hash_store(disk + 24, hash_update(HASH_START, disk, 24));
    CHECK_EQUAL(STORE_READY, headphone_file_open(&owner, file(), &profiles));
    CHECK_EQUAL(UINT32_MAX, owner.generation);
    profiles.current = 3;
    false_write_result = true;
    CHECK_EQUAL(STORE_DONE, headphone_file_save(&owner, &profiles));
    CHECK_EQUAL(0, owner.generation);
    CHECK_EQUAL(STORE_READY, headphone_file_open(&owner, file(), &profiles));
    CHECK_EQUAL(3, profiles.current);
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(selection_and_all_four_profiles_survive_restart),
        TEST_CASE(every_torn_write_reopens_a_whole_previous_or_new_profile),
        TEST_CASE(uncertain_publication_blocks_all_writes_until_reopen),
        TEST_CASE(damaged_newest_falls_back_and_future_formats_are_preserved),
        TEST_CASE(short_and_unreadable_files_and_invalid_models_never_write),
        TEST_CASE(generation_wrap_and_false_write_results_use_readback),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
