#include "settings/headphone_profile.h"

#include <stddef.h>

#include "core/hash.h"
#include "core/utf8.h"

enum {
    VERSION = 1,
    HEADER_BYTES = 16,
    RECORD_BYTES = 32,
    CHECKSUM_FIELD = HEADPHONE_PROFILES_FILE_SIZE - 4,
    TRIM_FIELD = HEADPHONE_PROFILE_NAME_BYTES,
    CEILING_FIELD = TRIM_FIELD + 1,
    BALANCE_FIELD = CEILING_FIELD + 1,
    RECORD_RESERVED = BALANCE_FIELD + 1,
    HALF_DB_GAIN_Q16 = 61870,
    GAIN_SHIFT = 16,
    OUTPUT_BITS = 24,
    SAMPLE_SHIFT = 8,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608,
    PERCENT = 100
};

static const uint8_t MAGIC[] = {'D', 'S', 'H', 'P'};

HeadphoneProfile headphone_profile_defaults(void)
{
    return (HeadphoneProfile){.volume_ceiling = SETTINGS_VOLUME_MAX};
}

HeadphoneProfiles headphone_profiles_defaults(void)
{
    return (HeadphoneProfiles){.current = HEADPHONE_PROFILE_NONE};
}

static bool valid_levels(const HeadphoneProfile *profile)
{
    return profile != NULL && profile->trim_half_db <= HEADPHONE_PROFILE_TRIM_MAX_HALF_DB &&
           profile->volume_ceiling <= SETTINGS_VOLUME_MAX &&
           profile->balance >= -HEADPHONE_PROFILE_BALANCE_MAX &&
           profile->balance <= HEADPHONE_PROFILE_BALANCE_MAX;
}

static bool valid_name(const char *name)
{
    size_t length = 0;
    while (length < HEADPHONE_PROFILE_NAME_BYTES && name[length] != '\0') {
        length++;
    }
    if (length == 0 || length == HEADPHONE_PROFILE_NAME_BYTES) {
        return false;
    }
    const char *cursor = name;
    while (cursor < name + length) {
        const char *before = cursor;
        uint32_t codepoint = utf8_next(&cursor);
        if ((codepoint == UTF8_REPLACEMENT_CHARACTER && cursor == before + 1) || codepoint < 0x20 ||
            (codepoint >= 0x7F && codepoint <= 0x9F) || codepoint == 0x2028 ||
            codepoint == 0x2029) {
            return false;
        }
    }
    return cursor == name + length;
}

bool headphone_profiles_valid(const HeadphoneProfiles *profiles)
{
    if (profiles == NULL || profiles->count > HEADPHONE_PROFILE_LIMIT ||
        (profiles->current != HEADPHONE_PROFILE_NONE && profiles->current >= profiles->count)) {
        return false;
    }
    for (int slot = 0; slot < profiles->count; slot++) {
        const HeadphoneProfile *profile = &profiles->slots[slot];
        if (!valid_levels(profile) || !valid_name(profile->name)) {
            return false;
        }
    }
    return true;
}

bool headphone_profiles_active(const HeadphoneProfiles *profiles, HeadphoneProfile *profile)
{
    if (profile == NULL || !headphone_profiles_valid(profiles)) {
        return false;
    }
    *profile = profiles->current == HEADPHONE_PROFILE_NONE ? headphone_profile_defaults()
                                                           : profiles->slots[profiles->current];
    return true;
}

bool headphone_profiles_encode(const HeadphoneProfiles *profiles, uint8_t *bytes, uint32_t capacity)
{
    if (bytes == NULL || capacity < HEADPHONE_PROFILES_FILE_SIZE ||
        !headphone_profiles_valid(profiles)) {
        return false;
    }
    for (uint32_t index = 0; index < HEADPHONE_PROFILES_FILE_SIZE; index++) {
        bytes[index] = 0;
    }
    for (size_t index = 0; index < sizeof MAGIC; index++) {
        bytes[index] = MAGIC[index];
    }
    bytes[4] = VERSION;
    bytes[6] = profiles->count;
    bytes[7] = profiles->current;
    for (int slot = 0; slot < profiles->count; slot++) {
        uint8_t *record = bytes + HEADER_BYTES + slot * RECORD_BYTES;
        const HeadphoneProfile *profile = &profiles->slots[slot];
        for (int index = 0; profile->name[index] != '\0'; index++) {
            record[index] = (uint8_t)profile->name[index];
        }
        record[TRIM_FIELD] = profile->trim_half_db;
        record[CEILING_FIELD] = profile->volume_ceiling;
        record[BALANCE_FIELD] = (uint8_t)profile->balance;
    }
    hash_store(bytes + CHECKSUM_FIELD, hash_update(HASH_START, bytes, CHECKSUM_FIELD));
    return true;
}

static bool zero_bytes(const uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool read_profile(const uint8_t *record, HeadphoneProfile *profile)
{
    bool ended = false;
    for (int index = 0; index < HEADPHONE_PROFILE_NAME_BYTES; index++) {
        if (ended && record[index] != 0) {
            return false;
        }
        profile->name[index] = (char)record[index];
        ended = ended || record[index] == 0;
    }
    int balance = record[BALANCE_FIELD];
    balance -= balance > INT8_MAX ? 256 : 0;
    if (balance < -HEADPHONE_PROFILE_BALANCE_MAX || balance > HEADPHONE_PROFILE_BALANCE_MAX ||
        !zero_bytes(record + RECORD_RESERVED, RECORD_BYTES - RECORD_RESERVED)) {
        return false;
    }
    profile->trim_half_db = record[TRIM_FIELD];
    profile->volume_ceiling = record[CEILING_FIELD];
    profile->balance = (int8_t)balance;
    return valid_levels(profile) && valid_name(profile->name);
}

bool headphone_profiles_decode(const uint8_t *bytes, uint32_t length, HeadphoneProfiles *profiles)
{
    if (bytes == NULL || profiles == NULL || length != HEADPHONE_PROFILES_FILE_SIZE) {
        return false;
    }
    for (size_t index = 0; index < sizeof MAGIC; index++) {
        if (bytes[index] != MAGIC[index]) {
            return false;
        }
    }
    if (bytes[4] != VERSION || bytes[5] != 0 || !zero_bytes(bytes + 8, HEADER_BYTES - 8) ||
        hash_load(bytes + CHECKSUM_FIELD) != hash_update(HASH_START, bytes, CHECKSUM_FIELD)) {
        return false;
    }
    HeadphoneProfiles candidate = {.count = bytes[6], .current = bytes[7]};
    if (candidate.count > HEADPHONE_PROFILE_LIMIT ||
        (candidate.current != HEADPHONE_PROFILE_NONE && candidate.current >= candidate.count)) {
        return false;
    }
    for (int slot = 0; slot < HEADPHONE_PROFILE_LIMIT; slot++) {
        const uint8_t *record = bytes + HEADER_BYTES + slot * RECORD_BYTES;
        if (slot < candidate.count ? !read_profile(record, &candidate.slots[slot])
                                   : !zero_bytes(record, RECORD_BYTES)) {
            return false;
        }
    }
    *profiles = candidate;
    return true;
}

static uint32_t compose_gain(uint32_t input, uint32_t trim, uint32_t percent)
{
    uint64_t product = (uint64_t)input * trim * percent;
    uint32_t divisor = REPLAY_GAIN_UNITY * PERCENT;
    return (uint32_t)((product + divisor / 2) / divisor);
}

HeadphoneProfileGains headphone_profile_gains(const HeadphoneProfile *profile, uint32_t input_gain)
{
    if (!valid_levels(profile)) {
        return (HeadphoneProfileGains){0};
    }
    uint32_t trim = REPLAY_GAIN_UNITY;
    for (int step = 0; step < profile->trim_half_db; step++) {
        trim =
            (uint32_t)(((uint64_t)trim * HALF_DB_GAIN_Q16 + REPLAY_GAIN_UNITY / 2) >> GAIN_SHIFT);
    }
    uint32_t left = (uint32_t)(PERCENT - (profile->balance > 0 ? profile->balance : 0));
    uint32_t right = (uint32_t)(PERCENT + (profile->balance < 0 ? profile->balance : 0));
    return (HeadphoneProfileGains){.left = compose_gain(input_gain, trim, left),
                                   .right = compose_gain(input_gain, trim, right)};
}

uint8_t headphone_profile_limit_volume(const HeadphoneProfile *profile, uint8_t requested)
{
    return valid_levels(profile)
               ? (requested < profile->volume_ceiling ? requested : profile->volume_ceiling)
               : 0;
}

static int64_t floor_shift(int64_t value, int shift)
{
    return value >= 0 ? value >> shift : -((-value - 1) >> shift) - 1;
}

int32_t headphone_profile_sample(int32_t sample, uint8_t bits, uint32_t gain)
{
    if (bits == 0 || bits > 32) {
        return 0;
    }
    int64_t value = (int64_t)sample * gain;
    int shift = GAIN_SHIFT + bits - OUTPUT_BITS;
    if (shift > 0) {
        value = floor_shift(value + (INT64_C(1) << (shift - 1)), shift);
    } else if (shift < 0) {
        int64_t factor = INT64_C(1) << -shift;
        if (value > SAMPLE_MAX / factor || value < SAMPLE_MIN / factor) {
            return value < 0 ? SAMPLE_MIN : SAMPLE_MAX;
        }
        value *= factor;
    }
    return value > SAMPLE_MAX ? SAMPLE_MAX : value < SAMPLE_MIN ? SAMPLE_MIN : (int32_t)value;
}

uint32_t headphone_profile_word(uint32_t word, uint32_t gain)
{
    if (gain == REPLAY_GAIN_UNITY) {
        return word;
    }
    int32_t sample = (int32_t)(word >> SAMPLE_SHIFT);
    if ((word & UINT32_C(0x80000000)) != 0) {
        sample -= 1 << OUTPUT_BITS;
    }
    return (uint32_t)headphone_profile_sample(sample, OUTPUT_BITS, gain) << SAMPLE_SHIFT;
}
