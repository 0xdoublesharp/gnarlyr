#include "headphone_store.h"

HeadphoneAudio headphone_store_parameters(const HeadphoneProfiles *profiles)
{
    if (profiles->current == HEADPHONE_PROFILE_NONE) {
        return headphone_audio_defaults();
    }
    const HeadphoneProfile *profile = &profiles->slots[profiles->current];
    uint32_t trim = headphone_profile_trim(profile->trim_half_db);
    uint32_t left = (uint32_t)(100 - (profile->balance > 0 ? profile->balance : 0));
    uint32_t right = (uint32_t)(100 + (profile->balance < 0 ? profile->balance : 0));
    return (HeadphoneAudio){(trim * left + 50) / 100, (trim * right + 50) / 100,
                            profile->volume_ceiling};
}
