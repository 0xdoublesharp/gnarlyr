#include "settings/headphone_profile.h"

#include "headphone_format.h"

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
    CROSSFEED_FIELD = BALANCE_FIELD + 1,
    RECORD_RESERVED = CROSSFEED_FIELD + 1,
    HALF_DB_GAIN_Q16 = 61870,
    GAIN_SHIFT = 16
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
           profile->balance <= HEADPHONE_PROFILE_BALANCE_MAX &&
           profile->crossfeed <= HEADPHONE_PROFILE_CROSSFEED_MAX;
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

bool headphone_profile_valid(const HeadphoneProfile *profile)
{
    return valid_levels(profile) && valid_name(profile->name);
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
    profile->crossfeed = record[CROSSFEED_FIELD];
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

uint32_t headphone_profile_trim(uint8_t steps)
{
    uint32_t trim = REPLAY_GAIN_UNITY;
    for (int step = 0; step < steps; step++) {
        trim =
            (uint32_t)(((uint64_t)trim * HALF_DB_GAIN_Q16 + REPLAY_GAIN_UNITY / 2) >> GAIN_SHIFT);
    }
    return trim;
}

uint8_t headphone_profile_limit_volume(const HeadphoneProfile *profile, uint8_t requested)
{
    return valid_levels(profile)
               ? (requested < profile->volume_ceiling ? requested : profile->volume_ceiling)
               : 0;
}
