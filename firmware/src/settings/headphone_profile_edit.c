#include "settings/headphone_profile.h"

#include <stddef.h>

#include "core/hash.h"
#include "headphone_format.h"

enum {
    VERSION = 1,
    HEADER_BYTES = 16,
    RECORD_BYTES = 32,
    CHECKSUM_FIELD = HEADPHONE_PROFILES_FILE_SIZE - 4,
    TRIM_FIELD = HEADPHONE_PROFILE_NAME_BYTES,
    CEILING_FIELD = TRIM_FIELD + 1,
    BALANCE_FIELD = CEILING_FIELD + 1,
    CROSSFEED_FIELD = BALANCE_FIELD + 1,
    PERCENT = 100
};

static const uint8_t MAGIC[] = {'D', 'S', 'H', 'P'};

bool headphone_profiles_valid(const HeadphoneProfiles *profiles)
{
    if (profiles == NULL || profiles->count > HEADPHONE_PROFILE_LIMIT ||
        (profiles->current != HEADPHONE_PROFILE_NONE && profiles->current >= profiles->count)) {
        return false;
    }
    for (int slot = 0; slot < profiles->count; slot++) {
        const HeadphoneProfile *profile = &profiles->slots[slot];
        if (!headphone_profile_valid(profile)) {
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
        record[CROSSFEED_FIELD] = profile->crossfeed;
    }
    hash_store(bytes + CHECKSUM_FIELD, hash_update(HASH_START, bytes, CHECKSUM_FIELD));
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
    if (profile == NULL || profile->trim_half_db > HEADPHONE_PROFILE_TRIM_MAX_HALF_DB ||
        profile->volume_ceiling > SETTINGS_VOLUME_MAX ||
        profile->balance < -HEADPHONE_PROFILE_BALANCE_MAX ||
        profile->balance > HEADPHONE_PROFILE_BALANCE_MAX) {
        return (HeadphoneProfileGains){0};
    }
    uint32_t trim = headphone_profile_trim(profile->trim_half_db);
    uint32_t left = (uint32_t)(PERCENT - (profile->balance > 0 ? profile->balance : 0));
    uint32_t right = (uint32_t)(PERCENT + (profile->balance < 0 ? profile->balance : 0));
    return (HeadphoneProfileGains){.left = compose_gain(input_gain, trim, left),
                                   .right = compose_gain(input_gain, trim, right)};
}
