#include <stdint.h>
#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "settings/headphone_profile.h"

enum {
    HEADER_BYTES = 16,
    RECORD_BYTES = 32,
    CHECKSUM_FIELD = HEADPHONE_PROFILES_FILE_SIZE - 4,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608
};

static HeadphoneProfiles named_profiles(void)
{
    HeadphoneProfiles profiles = headphone_profiles_defaults();
    profiles.count = HEADPHONE_PROFILE_LIMIT;
    profiles.current = 2;
    for (int index = 0; index < HEADPHONE_PROFILE_LIMIT; index++) {
        profiles.slots[index] = headphone_profile_defaults();
        memcpy(profiles.slots[index].name, "Headphones 1", 13);
        profiles.slots[index].name[11] = (char)('1' + index);
        profiles.slots[index].trim_half_db = (uint8_t)(index * 4);
        profiles.slots[index].volume_ceiling = (uint8_t)(60 - index * 5);
        profiles.slots[index].balance = (int8_t)(index * 20 - 30);
    }
    memcpy(profiles.slots[2].name, "Écoute 🎧", sizeof "Écoute 🎧");
    return profiles;
}

static void checksum(uint8_t *bytes)
{
    hash_store(bytes + CHECKSUM_FIELD, hash_update(HASH_START, bytes, CHECKSUM_FIELD));
}

static void rejects_unchanged(const uint8_t *bytes, uint32_t length)
{
    HeadphoneProfiles before = named_profiles();
    HeadphoneProfiles after = before;
    CHECK(!headphone_profiles_decode(bytes, length, &after));
    CHECK(memcmp(&before, &after, sizeof before) == 0);
}

static void defaults_have_no_selection_and_unity_audio(void)
{
    HeadphoneProfiles profiles = headphone_profiles_defaults();
    HeadphoneProfile active = {0};
    CHECK_EQUAL(0, profiles.count);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, profiles.current);
    CHECK(headphone_profiles_valid(&profiles));
    CHECK(headphone_profiles_active(&profiles, &active));
    CHECK_EQUAL(0, active.trim_half_db);
    CHECK_EQUAL(0, active.balance);
    CHECK_EQUAL(SETTINGS_VOLUME_MAX, active.volume_ceiling);
    HeadphoneProfileGains gains = headphone_profile_gains(&active, REPLAY_GAIN_UNITY);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, gains.left);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, gains.right);
    CHECK_EQUAL(53, headphone_profile_limit_volume(&active, 53));
}

static void four_utf8_profiles_round_trip_to_exact_bytes(void)
{
    HeadphoneProfiles profiles = named_profiles();
    uint8_t bytes[HEADPHONE_PROFILES_FILE_SIZE + 1];
    memset(bytes, 0xA5, sizeof bytes);
    CHECK(headphone_profiles_encode(&profiles, bytes, sizeof bytes));
    CHECK_EQUAL(0xA5, bytes[HEADPHONE_PROFILES_FILE_SIZE]);
    CHECK(memcmp(bytes, "DSHP\1\0\4\2", 8) == 0);
    CHECK_EQUAL(8, bytes[HEADER_BYTES + 2 * RECORD_BYTES + 24]);
    CHECK_EQUAL(50, bytes[HEADER_BYTES + 2 * RECORD_BYTES + 25]);
    CHECK_EQUAL(10, bytes[HEADER_BYTES + 2 * RECORD_BYTES + 26]);
    CHECK_EQUAL(hash_update(HASH_START, bytes, CHECKSUM_FIELD), hash_load(bytes + CHECKSUM_FIELD));
    HeadphoneProfiles decoded = headphone_profiles_defaults();
    CHECK(headphone_profiles_decode(bytes, HEADPHONE_PROFILES_FILE_SIZE, &decoded));
    CHECK_EQUAL(profiles.count, decoded.count);
    CHECK_EQUAL(profiles.current, decoded.current);
    for (int index = 0; index < HEADPHONE_PROFILE_LIMIT; index++) {
        CHECK(strcmp(profiles.slots[index].name, decoded.slots[index].name) == 0);
        CHECK_EQUAL(profiles.slots[index].trim_half_db, decoded.slots[index].trim_half_db);
        CHECK_EQUAL(profiles.slots[index].volume_ceiling, decoded.slots[index].volume_ceiling);
        CHECK_EQUAL(profiles.slots[index].balance, decoded.slots[index].balance);
    }
    uint8_t again[HEADPHONE_PROFILES_FILE_SIZE];
    CHECK(headphone_profiles_encode(&decoded, again, sizeof again));
    CHECK(memcmp(bytes, again, sizeof again) == 0);
}

static void every_truncation_and_corruption_preserves_old_state(void)
{
    HeadphoneProfiles profiles = named_profiles();
    uint8_t bytes[HEADPHONE_PROFILES_FILE_SIZE + 1] = {0};
    CHECK(headphone_profiles_encode(&profiles, bytes, sizeof bytes));
    for (uint32_t length = 0; length < HEADPHONE_PROFILES_FILE_SIZE; length++) {
        rejects_unchanged(bytes, length);
    }
    rejects_unchanged(bytes, sizeof bytes);
    for (int offset = 0; offset < HEADPHONE_PROFILES_FILE_SIZE; offset++) {
        bytes[offset] ^= 1;
        rejects_unchanged(bytes, HEADPHONE_PROFILES_FILE_SIZE);
        bytes[offset] ^= 1;
    }
}

static void semantic_errors_are_rejected_even_with_a_valid_checksum(void)
{
    HeadphoneProfiles profiles = named_profiles();
    uint8_t valid[HEADPHONE_PROFILES_FILE_SIZE] = {0};
    CHECK(headphone_profiles_encode(&profiles, valid, sizeof valid));
    static const struct {
        uint8_t offset;
        uint8_t value;
    } changes[] = {{0, 'X'},
                   {4, 2},
                   {5, 1},
                   {6, 5},
                   {7, 5},
                   {8, 1},
                   {15, 1},
                   {HEADER_BYTES, 0},
                   {HEADER_BYTES + 24, 49},
                   {HEADER_BYTES + 25, 61},
                   {HEADER_BYTES + 26, 101},
                   {HEADER_BYTES + 26, 155},
                   {HEADER_BYTES + 27, 1},
                   {HEADER_BYTES + 31, 1}};
    for (size_t index = 0; index < sizeof changes / sizeof changes[0]; index++) {
        uint8_t bytes[HEADPHONE_PROFILES_FILE_SIZE];
        memcpy(bytes, valid, sizeof bytes);
        bytes[changes[index].offset] = changes[index].value;
        checksum(bytes);
        rejects_unchanged(bytes, sizeof bytes);
    }
}

static void trimmed_and_balanced_gains_compose_with_replay_gain(void)
{
    HeadphoneProfile profile = headphone_profile_defaults();
    HeadphoneProfileGains unity = headphone_profile_gains(&profile, 100000);
    CHECK_EQUAL(100000, unity.left);
    CHECK_EQUAL(100000, unity.right);
    profile.trim_half_db = 12;
    HeadphoneProfileGains trim = headphone_profile_gains(&profile, REPLAY_GAIN_UNITY);
    CHECK(trim.left > 32700 && trim.left < 33000);
    CHECK_EQUAL(trim.left, trim.right);
    HeadphoneProfileGains replay = headphone_profile_gains(&profile, 2 * REPLAY_GAIN_UNITY);
    CHECK_EQUAL(trim.left * 2, replay.left);
    CHECK_EQUAL(trim.right * 2, replay.right);
    profile.trim_half_db = 0;
    profile.balance = 50;
    HeadphoneProfileGains right = headphone_profile_gains(&profile, REPLAY_GAIN_UNITY);
    CHECK_EQUAL(REPLAY_GAIN_UNITY / 2, right.left);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, right.right);
    profile.balance = -100;
    HeadphoneProfileGains left = headphone_profile_gains(&profile, REPLAY_GAIN_UNITY);
    CHECK_EQUAL(REPLAY_GAIN_UNITY, left.left);
    CHECK_EQUAL(0, left.right);
    profile.balance = 100;
    right = headphone_profile_gains(&profile, UINT32_MAX);
    CHECK_EQUAL(0, right.left);
    CHECK_EQUAL(UINT32_MAX, right.right);
}

static void volume_ceiling_is_independent_of_requested_mode_volume(void)
{
    HeadphoneProfile profile = headphone_profile_defaults();
    profile.volume_ceiling = 37;
    CHECK_EQUAL(0, headphone_profile_limit_volume(&profile, 0));
    CHECK_EQUAL(13, headphone_profile_limit_volume(&profile, 13));
    CHECK_EQUAL(37, headphone_profile_limit_volume(&profile, 37));
    CHECK_EQUAL(37, headphone_profile_limit_volume(&profile, 60));
    CHECK_EQUAL(37, headphone_profile_limit_volume(&profile, 255));
    profile.volume_ceiling = 0;
    CHECK_EQUAL(0, headphone_profile_limit_volume(&profile, 60));
}

static void pcm_unity_preserves_bits_and_rounding_matches_existing_engine(void)
{
    static const int32_t samples[] = {SAMPLE_MIN, SAMPLE_MIN + 1, -65536,    -3, -2, -1, 0, 1, 2, 3,
                                      65536,      SAMPLE_MAX - 1, SAMPLE_MAX};
    for (size_t index = 0; index < sizeof samples / sizeof samples[0]; index++) {
        CHECK_EQUAL(samples[index],
                    headphone_profile_sample(samples[index], 24, REPLAY_GAIN_UNITY));
        uint32_t word = (uint32_t)samples[index] << 8;
        CHECK_EQUAL(word, headphone_profile_word(word, REPLAY_GAIN_UNITY));
    }
    CHECK_EQUAL(256, headphone_profile_sample(1, 16, REPLAY_GAIN_UNITY));
    CHECK_EQUAL(-8388608, headphone_profile_sample(-32768, 16, REPLAY_GAIN_UNITY));
    CHECK_EQUAL(0, headphone_profile_sample(-1, 24, REPLAY_GAIN_UNITY / 2));
    CHECK_EQUAL(-1, headphone_profile_sample(-3, 24, REPLAY_GAIN_UNITY / 2));
    CHECK_EQUAL(2, headphone_profile_sample(3, 24, REPLAY_GAIN_UNITY / 2));
    CHECK_EQUAL(0, headphone_profile_sample(INT32_MIN, 32, 0));
    CHECK_EQUAL(SAMPLE_MAX, headphone_profile_sample(INT32_MAX, 32, UINT32_MAX));
    CHECK_EQUAL(SAMPLE_MIN, headphone_profile_sample(INT32_MIN, 32, UINT32_MAX));
    CHECK_EQUAL(SAMPLE_MAX, headphone_profile_sample(SAMPLE_MAX, 24, 2 * REPLAY_GAIN_UNITY));
    CHECK_EQUAL(SAMPLE_MIN, headphone_profile_sample(SAMPLE_MIN, 24, 2 * REPLAY_GAIN_UNITY));
}

static void none_and_unused_slots_have_a_canonical_encoding(void)
{
    HeadphoneProfiles profiles = headphone_profiles_defaults();
    uint8_t bytes[HEADPHONE_PROFILES_FILE_SIZE];
    CHECK(headphone_profiles_encode(&profiles, bytes, sizeof bytes));
    for (int offset = HEADER_BYTES; offset < CHECKSUM_FIELD; offset++) {
        CHECK_EQUAL(0, bytes[offset]);
    }
    CHECK_EQUAL(4, bytes[7]);
    HeadphoneProfiles decoded = named_profiles();
    CHECK(headphone_profiles_decode(bytes, sizeof bytes, &decoded));
    CHECK_EQUAL(0, decoded.count);
    CHECK_EQUAL(HEADPHONE_PROFILE_NONE, decoded.current);
    profiles = named_profiles();
    profiles.count = 1;
    profiles.current = HEADPHONE_PROFILE_NONE;
    CHECK(headphone_profiles_encode(&profiles, bytes, sizeof bytes));
    HeadphoneProfile active = {0};
    CHECK(headphone_profiles_active(&profiles, &active));
    CHECK_EQUAL(0, active.trim_half_db);
    CHECK_EQUAL(SETTINGS_VOLUME_MAX, active.volume_ceiling);
    for (int offset = HEADER_BYTES + RECORD_BYTES; offset < CHECKSUM_FIELD; offset++) {
        CHECK_EQUAL(0, bytes[offset]);
    }
    bytes[HEADER_BYTES + RECORD_BYTES] = 1;
    checksum(bytes);
    rejects_unchanged(bytes, sizeof bytes);
    profiles.count = 0;
    profiles.current = 0;
    CHECK(!headphone_profiles_valid(&profiles));
}

static void utf8_names_are_bounded_printable_and_zero_padded(void)
{
    HeadphoneProfiles profiles = named_profiles();
    profiles.count = 1;
    profiles.current = 0;
    memset(profiles.slots[0].name, 'A', HEADPHONE_PROFILE_NAME_BYTES);
    profiles.slots[0].name[HEADPHONE_PROFILE_NAME_BYTES - 1] = 0;
    uint8_t valid[HEADPHONE_PROFILES_FILE_SIZE];
    CHECK(headphone_profiles_encode(&profiles, valid, sizeof valid));
    HeadphoneProfiles decoded = headphone_profiles_defaults();
    CHECK(headphone_profiles_decode(valid, sizeof valid, &decoded));
    CHECK_EQUAL(23, strlen(decoded.slots[0].name));
    static const char *const good[] = {"x", "Écoute 🎧", "名", "�"};
    for (size_t index = 0; index < sizeof good / sizeof good[0]; index++) {
        memset(profiles.slots[0].name, 0xA5, HEADPHONE_PROFILE_NAME_BYTES);
        memcpy(profiles.slots[0].name, good[index], strlen(good[index]) + 1);
        CHECK(headphone_profiles_encode(&profiles, valid, sizeof valid));
        CHECK(headphone_profiles_decode(valid, sizeof valid, &decoded));
        CHECK(strcmp(good[index], decoded.slots[0].name) == 0);
        for (size_t offset = strlen(good[index]) + 1; offset < HEADPHONE_PROFILE_NAME_BYTES;
             offset++) {
            CHECK_EQUAL(0, valid[HEADER_BYTES + offset]);
        }
    }
    static const uint8_t bad[][8] = {{0},
                                     {0x80, 0},
                                     {0xC0, 0xAF, 0},
                                     {0xE0, 0x80, 0x80, 0},
                                     {0xED, 0xA0, 0x80, 0},
                                     {0xF4, 0x90, 0x80, 0x80, 0},
                                     {0xF5, 0x80, 0x80, 0x80, 0},
                                     {0xE2, 0x82, 0},
                                     {0xF0, 0x9F, 0x8E, 0},
                                     {'a', '\n', 0},
                                     {'a', '\r', 0},
                                     {'a', '\t', 0},
                                     {'a', 0x7F, 0},
                                     {0xC2, 0x85, 0},
                                     {0xE2, 0x80, 0xA8, 0},
                                     {0xE2, 0x80, 0xA9, 0}};
    for (size_t index = 0; index < sizeof bad / sizeof bad[0]; index++) {
        uint8_t bytes[HEADPHONE_PROFILES_FILE_SIZE];
        memcpy(bytes, valid, sizeof bytes);
        memset(bytes + HEADER_BYTES, 0, HEADPHONE_PROFILE_NAME_BYTES);
        memcpy(bytes + HEADER_BYTES, bad[index], sizeof bad[index]);
        checksum(bytes);
        rejects_unchanged(bytes, sizeof bytes);
    }
    uint8_t bytes[HEADPHONE_PROFILES_FILE_SIZE];
    memcpy(bytes, valid, sizeof bytes);
    memset(bytes + HEADER_BYTES, 'A', HEADPHONE_PROFILE_NAME_BYTES);
    checksum(bytes);
    rejects_unchanged(bytes, sizeof bytes);
    bytes[HEADER_BYTES + 22] = 0xF0;
    bytes[HEADER_BYTES + 23] = 0;
    checksum(bytes);
    rejects_unchanged(bytes, sizeof bytes);
    memset(bytes + HEADER_BYTES, 0, HEADPHONE_PROFILE_NAME_BYTES);
    bytes[HEADER_BYTES] = 'A';
    bytes[HEADER_BYTES + 2] = 'B';
    checksum(bytes);
    rejects_unchanged(bytes, sizeof bytes);
}

static void invalid_models_and_small_buffers_never_publish_partial_bytes(void)
{
    HeadphoneProfiles good = named_profiles();
    for (int fault = 0; fault < 8; fault++) {
        HeadphoneProfiles invalid = good;
        switch (fault) {
        case 0:
            invalid.count = 5;
            break;
        case 1:
            invalid.current = 5;
            break;
        case 2:
            invalid.slots[1].trim_half_db = 49;
            break;
        case 3:
            invalid.slots[1].volume_ceiling = 61;
            break;
        case 4:
            invalid.slots[1].balance = 101;
            break;
        case 5:
            invalid.slots[1].balance = -101;
            break;
        case 6:
            invalid.slots[1].name[0] = 0;
            break;
        case 7:
            memset(invalid.slots[1].name, 'x', HEADPHONE_PROFILE_NAME_BYTES);
            break;
        }
        CHECK(!headphone_profiles_valid(&invalid));
        uint8_t before[HEADPHONE_PROFILES_FILE_SIZE];
        uint8_t bytes[HEADPHONE_PROFILES_FILE_SIZE];
        memset(before, 0xA5, sizeof before);
        memcpy(bytes, before, sizeof bytes);
        CHECK(!headphone_profiles_encode(&invalid, bytes, sizeof bytes));
        CHECK(memcmp(bytes, before, sizeof bytes) == 0);
        HeadphoneProfile active = good.slots[0];
        HeadphoneProfile kept = active;
        CHECK(!headphone_profiles_active(&invalid, &active));
        CHECK(memcmp(&active, &kept, sizeof active) == 0);
    }
    uint8_t bytes[HEADPHONE_PROFILES_FILE_SIZE];
    uint8_t before[HEADPHONE_PROFILES_FILE_SIZE];
    memset(before, 0xA5, sizeof before);
    for (uint32_t capacity = 0; capacity < sizeof bytes; capacity++) {
        memcpy(bytes, before, sizeof bytes);
        CHECK(!headphone_profiles_encode(&good, bytes, capacity));
        CHECK(memcmp(bytes, before, sizeof bytes) == 0);
    }
    good.count = 1;
    good.current = 1;
    CHECK(!headphone_profiles_valid(&good));
    CHECK(!headphone_profiles_valid(NULL));
    CHECK(!headphone_profiles_active(NULL, &good.slots[0]));
    CHECK(!headphone_profiles_active(&good, NULL));
    CHECK(!headphone_profiles_encode(NULL, bytes, sizeof bytes));
    CHECK(!headphone_profiles_encode(&good, NULL, sizeof bytes));
    CHECK(!headphone_profiles_decode(NULL, sizeof bytes, &good));
    CHECK(!headphone_profiles_decode(bytes, sizeof bytes, NULL));
}

static void trim_balance_and_replay_gain_remain_bounded_across_every_setting(void)
{
    HeadphoneProfile profile = headphone_profile_defaults();
    for (int balance = -100; balance <= 100; balance++) {
        uint32_t previous_left = UINT32_MAX;
        uint32_t previous_right = UINT32_MAX;
        for (int trim = 0; trim <= HEADPHONE_PROFILE_TRIM_MAX_HALF_DB; trim++) {
            profile.trim_half_db = (uint8_t)trim;
            profile.balance = (int8_t)balance;
            HeadphoneProfileGains maximum = headphone_profile_gains(&profile, UINT32_MAX);
            CHECK(maximum.left <= previous_left);
            CHECK(maximum.right <= previous_right);
            previous_left = maximum.left;
            previous_right = maximum.right;
            HeadphoneProfileGains ordinary = headphone_profile_gains(&profile, REPLAY_GAIN_UNITY);
            CHECK(ordinary.left <= REPLAY_GAIN_UNITY);
            CHECK(ordinary.right <= REPLAY_GAIN_UNITY);
            profile.balance = (int8_t)-balance;
            HeadphoneProfileGains mirror = headphone_profile_gains(&profile, REPLAY_GAIN_UNITY);
            CHECK_EQUAL(ordinary.left, mirror.right);
            CHECK_EQUAL(ordinary.right, mirror.left);
            HeadphoneProfileGains muted = headphone_profile_gains(&profile, 0);
            CHECK_EQUAL(0, muted.left);
            CHECK_EQUAL(0, muted.right);
        }
    }
    profile.balance = 0;
    profile.trim_half_db = HEADPHONE_PROFILE_TRIM_MAX_HALF_DB;
    HeadphoneProfileGains maximum_trim = headphone_profile_gains(&profile, REPLAY_GAIN_UNITY);
    CHECK(maximum_trim.left > 4100 && maximum_trim.left < 4200);
    profile.trim_half_db++;
    CHECK_EQUAL(0, headphone_profile_gains(&profile, REPLAY_GAIN_UNITY).left);
    CHECK_EQUAL(0, headphone_profile_limit_volume(&profile, 60));
    CHECK_EQUAL(0, headphone_profile_gains(NULL, REPLAY_GAIN_UNITY).right);
    CHECK_EQUAL(0, headphone_profile_limit_volume(NULL, 60));
}

static void pcm_unity_covers_every_sixteen_bit_sample_and_original_word(void)
{
    for (int32_t sample = -32768; sample <= 32767; sample++) {
        CHECK_EQUAL(sample * 256, headphone_profile_sample(sample, 16, REPLAY_GAIN_UNITY));
    }
    uint32_t random = 1;
    for (int index = 0; index < 10000; index++) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        CHECK_EQUAL(random, headphone_profile_word(random, REPLAY_GAIN_UNITY));
        int32_t sample = (int32_t)(random >> 8);
        sample -= (random & UINT32_C(0x80000000)) != 0 ? 1 << 24 : 0;
        CHECK_EQUAL(sample, headphone_profile_sample(sample, 24, REPLAY_GAIN_UNITY));
        int32_t attenuated = headphone_profile_sample(sample, 24, REPLAY_GAIN_UNITY / 2);
        CHECK(attenuated >= SAMPLE_MIN && attenuated <= SAMPLE_MAX);
        CHECK_EQUAL((uint32_t)attenuated << 8,
                    headphone_profile_word(random & UINT32_C(0xFFFFFF00), REPLAY_GAIN_UNITY / 2));
    }
    CHECK_EQUAL(SAMPLE_MIN, headphone_profile_sample(INT32_MIN, 32, REPLAY_GAIN_UNITY));
    CHECK_EQUAL(SAMPLE_MAX, headphone_profile_sample(INT32_MAX, 32, REPLAY_GAIN_UNITY));
    CHECK_EQUAL(0, headphone_profile_sample(1, 0, REPLAY_GAIN_UNITY));
    CHECK_EQUAL(0, headphone_profile_sample(1, 33, REPLAY_GAIN_UNITY));
    for (uint8_t bits = 1; bits <= 32; bits++) {
        CHECK_EQUAL(SAMPLE_MIN, headphone_profile_sample(INT32_MIN, bits, UINT32_MAX));
        CHECK_EQUAL(SAMPLE_MAX, headphone_profile_sample(INT32_MAX, bits, UINT32_MAX));
    }
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(defaults_have_no_selection_and_unity_audio),
        TEST_CASE(four_utf8_profiles_round_trip_to_exact_bytes),
        TEST_CASE(every_truncation_and_corruption_preserves_old_state),
        TEST_CASE(semantic_errors_are_rejected_even_with_a_valid_checksum),
        TEST_CASE(trimmed_and_balanced_gains_compose_with_replay_gain),
        TEST_CASE(volume_ceiling_is_independent_of_requested_mode_volume),
        TEST_CASE(pcm_unity_preserves_bits_and_rounding_matches_existing_engine),
        TEST_CASE(none_and_unused_slots_have_a_canonical_encoding),
        TEST_CASE(utf8_names_are_bounded_printable_and_zero_padded),
        TEST_CASE(invalid_models_and_small_buffers_never_publish_partial_bytes),
        TEST_CASE(trim_balance_and_replay_gain_remain_bounded_across_every_setting),
        TEST_CASE(pcm_unity_covers_every_sixteen_bit_sample_and_original_word),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
