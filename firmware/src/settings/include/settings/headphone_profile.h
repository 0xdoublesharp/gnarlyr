#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/headphone_audio.h"
#include "settings/settings.h"

enum {
    HEADPHONE_PROFILE_LIMIT = 4,
    HEADPHONE_PROFILE_NONE = HEADPHONE_PROFILE_LIMIT,
    HEADPHONE_PROFILE_NAME_BYTES = 24,
    HEADPHONE_PROFILE_TRIM_MAX_HALF_DB = 24,
    HEADPHONE_PROFILE_BALANCE_MAX = 100,
    HEADPHONE_PROFILES_FILE_SIZE = 148
};

typedef struct HeadphoneProfile {
    char name[HEADPHONE_PROFILE_NAME_BYTES];
    uint8_t trim_half_db;
    uint8_t volume_ceiling;
    int8_t balance;
} HeadphoneProfile;

typedef struct HeadphoneProfiles {
    uint8_t count;
    uint8_t current;
    HeadphoneProfile slots[HEADPHONE_PROFILE_LIMIT];
} HeadphoneProfiles;

typedef struct HeadphoneProfileGains {
    uint32_t left;
    uint32_t right;
} HeadphoneProfileGains;

HeadphoneProfile headphone_profile_defaults(void);
HeadphoneProfiles headphone_profiles_defaults(void);
bool headphone_profiles_valid(const HeadphoneProfiles *profiles);
bool headphone_profiles_active(const HeadphoneProfiles *profiles, HeadphoneProfile *profile);
bool headphone_profiles_encode(const HeadphoneProfiles *profiles, uint8_t *bytes,
                               uint32_t capacity);
bool headphone_profiles_decode(const uint8_t *bytes, uint32_t length, HeadphoneProfiles *profiles);
HeadphoneProfileGains headphone_profile_gains(const HeadphoneProfile *profile, uint32_t input_gain);
uint8_t headphone_profile_limit_volume(const HeadphoneProfile *profile, uint8_t requested);
uint32_t headphone_profile_trim(uint8_t steps);
