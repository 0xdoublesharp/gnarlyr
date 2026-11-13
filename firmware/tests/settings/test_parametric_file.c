#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "settings/parametric_file.h"

static uint8_t disk[PARAMETRIC_FILE_BYTES];
static uint32_t cut;
static bool no_read;
static bool unreadable_after_write;
static unsigned unreadable_at_write;
static bool false_write_result;
static unsigned writes;
static unsigned fail_write;

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
    uint32_t copied = writes == fail_write && cut < length ? cut : length;
    memcpy(disk + offset, bytes, copied);
    no_read = unreadable_after_write || writes == unreadable_at_write;
    return copied == length && !false_write_result;
}

static StoreFile file(void)
{
    return (StoreFile){.source = {read_disk, NULL}, .write = write_disk, .size = sizeof disk};
}

static ParametricBank sample(void)
{
    ParametricEq eq = {
        .count = 1, .preamp = -62, .filters = {{105, 70, -23, PARAMETRIC_PEAK, true}}};
    ParametricBank bank = {.custom = eq, .used = 1};
    bank.slots[0] = eq;
    return bank;
}

static void bank_round_trips_and_a_torn_write_keeps_the_previous_copy(void)
{
    for (unsigned step = 1; step <= 4; step++) {
        for (uint32_t boundary = 0; boundary < STORE_BLOCK_BYTES; boundary++) {
            memset(disk, 0, sizeof disk);
            cut = STORE_BLOCK_BYTES;
            no_read = false;
            writes = 0;
            fail_write = 0;
            ParametricBank bank = sample();
            ParametricFile owner;
            CHECK_EQUAL(STORE_EMPTY, parametric_file_open(&owner, file(), &bank));
            CHECK_EQUAL(STORE_DONE, parametric_file_save(&owner, &bank));
            bank.custom.preamp = -100;
            writes = 0;
            fail_write = step;
            cut = boundary;
            StoreResult result = parametric_file_save(&owner, &bank);
            CHECK(result == STORE_DONE || result == STORE_FAILED);
            CHECK_EQUAL(STORE_READY, parametric_file_open(&owner, file(), &bank));
            CHECK_EQUAL(result == STORE_DONE ? -100 : -62, bank.custom.preamp);
            CHECK_EQUAL(1, bank.used);
            CHECK_EQUAL(105, bank.slots[0].filters[0].hz);
        }
    }
}

static void corrupt_newest_payload_falls_back_and_uncertain_readback_blocks_saves(void)
{
    memset(disk, 0, sizeof disk);
    fail_write = 0;
    no_read = false;
    ParametricBank bank = sample();
    ParametricFile owner;
    CHECK_EQUAL(STORE_EMPTY, parametric_file_open(&owner, file(), &bank));
    CHECK_EQUAL(STORE_DONE, parametric_file_save(&owner, &bank));
    bank.custom.preamp = -100;
    CHECK_EQUAL(STORE_DONE, parametric_file_save(&owner, &bank));
    disk[PARAMETRIC_BANK_BYTES + STORE_BLOCK_BYTES + 8] ^= 1;
    CHECK_EQUAL(STORE_READY, parametric_file_open(&owner, file(), &bank));
    CHECK_EQUAL(-62, bank.custom.preamp);
    writes = 0;
    unreadable_at_write = 4;
    CHECK_EQUAL(STORE_UNCERTAIN, parametric_file_save(&owner, &bank));
    CHECK(owner.needs_reload);
    CHECK_EQUAL(STORE_FAILED, parametric_file_save(&owner, &bank));
    unreadable_at_write = 0;
    no_read = false;
}

static void canonical_encoding_rejects_invalid_filters_and_padding(void)
{
    ParametricBank bank = sample();
    uint8_t bytes[PARAMETRIC_PAYLOAD_BYTES];
    CHECK(parametric_bank_encode(&bank, bytes));
    ParametricBank restored;
    CHECK(parametric_bank_decode(bytes, &restored));
    CHECK(parametric_same(&bank.custom, &restored.custom));
    bytes[5] = 1;
    CHECK(!parametric_bank_decode(bytes, &restored));
    bank.custom.filters[0].q = 29;
    CHECK(!parametric_bank_encode(&bank, bytes));
}

static void shaping_positions_survive_custom_and_slots_and_the_spare_bytes_stay_zero(void)
{
    ParametricBank bank = {0};
    bank.shaping[0] = (SoundShaping){.positions = {2, -3, 0, 1}, .active = true};
    ParametricEq base = {0};
    CHECK(sound_shaping_build(&bank.custom, &base, &bank.shaping[0]));
    bank.used = 128;
    bank.slots[7] = bank.custom;
    bank.shaping[8] = bank.shaping[0];
    uint8_t bytes[PARAMETRIC_PAYLOAD_BYTES];
    ParametricBank restored;
    CHECK(parametric_bank_encode(&bank, bytes));
    CHECK(parametric_bank_decode(bytes, &restored));
    CHECK(restored.shaping[0].active);
    CHECK_EQUAL(2, restored.shaping[0].positions[0]);
    CHECK_EQUAL(-3, restored.shaping[8].positions[1]);
    CHECK_EQUAL(0, bytes[1]);
    bytes[1] = 1;
    CHECK(!parametric_bank_decode(bytes, &restored));
    bytes[1] = 0;
    bytes[PARAMETRIC_SHAPING_OFFSET] = 6;
    CHECK(!parametric_bank_decode(bytes, &restored));
}

static void a_bank_without_the_shaping_records_is_corrupt(void)
{
    memset(disk, 0, sizeof disk);
    no_read = false;
    fail_write = 0;
    false_write_result = false;
    ParametricBank bank = sample();
    uint8_t bytes[PARAMETRIC_PAYLOAD_BYTES];
    CHECK(parametric_bank_encode(&bank, bytes));
    memcpy(disk + STORE_BLOCK_BYTES, bytes, PARAMETRIC_SHAPING_OFFSET);
    StoreKind kind = {.magic = {'P', 'E', 'Q', 'F'}, .version = 1};
    StoreHeader header = {.generation = 42,
                          .bytes = PARAMETRIC_SHAPING_OFFSET,
                          .hash = hash_update(HASH_START, bytes, PARAMETRIC_SHAPING_OFFSET)};
    store_encode(&kind, &header, disk);
    ParametricFile owner;
    CHECK(parametric_file_open(&owner, file(), &bank) != STORE_READY);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_bank_without_the_shaping_records_is_corrupt),
        TEST_CASE(bank_round_trips_and_a_torn_write_keeps_the_previous_copy),
        TEST_CASE(canonical_encoding_rejects_invalid_filters_and_padding),
        TEST_CASE(shaping_positions_survive_custom_and_slots_and_the_spare_bytes_stay_zero),
        TEST_CASE(corrupt_newest_payload_falls_back_and_uncertain_readback_blocks_saves),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
