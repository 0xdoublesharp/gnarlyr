#include <stdint.h>

#include "check.h"
#include "core/hash.h"
#include "settings/equalizer_slots.h"

static void fresh_slots_are_empty(void)
{
    EqualizerSlots slots = {0};
    for (int slot = 0; slot < EQUALIZER_SLOT_COUNT; slot++) {
        CHECK(!equalizer_slots_used(&slots, slot));
    }
    CHECK(!equalizer_slots_used(&slots, -1));
    CHECK(!equalizer_slots_used(&slots, EQUALIZER_SLOT_COUNT));
}

static void a_saved_slot_is_used_and_keeps_its_gains(void)
{
    static const int8_t gains[SETTINGS_EQUALIZER_BANDS] = {-20, 20, 1, -1, 0, 3, 4, 5, -6, 7};
    EqualizerSlots slots = {0};
    equalizer_slots_put(&slots, 2, gains);
    CHECK(equalizer_slots_used(&slots, 2));
    CHECK(!equalizer_slots_used(&slots, 1));
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        CHECK_EQUAL(gains[band], slots.gains[2][band]);
    }
}

static void slots_survive_an_encode_and_decode(void)
{
    static const int8_t first[SETTINGS_EQUALIZER_BANDS] = {-20, 20, 1, -1, 0, 3, 4, 5, -6, 7};
    static const int8_t last[SETTINGS_EQUALIZER_BANDS] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    EqualizerSlots slots = {0};
    EqualizerSlots decoded;
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    equalizer_slots_put(&slots, 0, first);
    equalizer_slots_put(&slots, EQUALIZER_SLOT_COUNT - 1, last);
    equalizer_slots_encode(&slots, bytes);
    CHECK(equalizer_slots_decode(bytes, sizeof bytes, &decoded));
    CHECK(equalizer_slots_used(&decoded, 0));
    CHECK(!equalizer_slots_used(&decoded, 1));
    CHECK(equalizer_slots_used(&decoded, EQUALIZER_SLOT_COUNT - 1));
    for (int band = 0; band < SETTINGS_EQUALIZER_BANDS; band++) {
        CHECK_EQUAL(first[band], decoded.gains[0][band]);
        CHECK_EQUAL(last[band], decoded.gains[EQUALIZER_SLOT_COUNT - 1][band]);
    }
}

static void a_flat_saved_slot_is_still_used(void)
{
    static const int8_t flat[SETTINGS_EQUALIZER_BANDS] = {0};
    EqualizerSlots slots = {0};
    EqualizerSlots decoded;
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    equalizer_slots_put(&slots, 4, flat);
    equalizer_slots_encode(&slots, bytes);
    CHECK(equalizer_slots_decode(bytes, sizeof bytes, &decoded));
    CHECK(equalizer_slots_used(&decoded, 4));
}

static void a_corrupted_or_short_file_decodes_as_empty(void)
{
    static const int8_t gains[SETTINGS_EQUALIZER_BANDS] = {1, 2, 3};
    EqualizerSlots slots = {0};
    EqualizerSlots decoded = {.used = 0xFF};
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    equalizer_slots_put(&slots, 1, gains);
    equalizer_slots_encode(&slots, bytes);
    CHECK(!equalizer_slots_decode(bytes, sizeof bytes - 1, &decoded));
    CHECK_EQUAL(0, decoded.used);
    bytes[10] ^= 0x01;
    decoded.used = 0xFF;
    CHECK(!equalizer_slots_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(0, decoded.used);
}

static void a_zeroed_file_decodes_as_empty(void)
{
    uint8_t bytes[4096] = {0};
    EqualizerSlots decoded = {.used = 0xFF};
    CHECK(!equalizer_slots_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(0, decoded.used);
}

static void a_gain_beyond_ten_decibels_is_rejected(void)
{
    static const int8_t gains[SETTINGS_EQUALIZER_BANDS] = {1, 2, 3};
    EqualizerSlots slots = {0};
    EqualizerSlots decoded;
    uint8_t bytes[EQUALIZER_SLOTS_FILE_SIZE];
    equalizer_slots_put(&slots, 0, gains);
    equalizer_slots_encode(&slots, bytes);
    bytes[5] = 41;
    hash_store(bytes + EQUALIZER_SLOTS_FILE_SIZE - 4,
               hash_update(HASH_START, bytes, EQUALIZER_SLOTS_FILE_SIZE - 4));
    CHECK(!equalizer_slots_decode(bytes, sizeof bytes, &decoded));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(fresh_slots_are_empty),
        TEST_CASE(a_saved_slot_is_used_and_keeps_its_gains),
        TEST_CASE(slots_survive_an_encode_and_decode),
        TEST_CASE(a_flat_saved_slot_is_still_used),
        TEST_CASE(a_corrupted_or_short_file_decodes_as_empty),
        TEST_CASE(a_zeroed_file_decodes_as_empty),
        TEST_CASE(a_gain_beyond_ten_decibels_is_rejected),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
