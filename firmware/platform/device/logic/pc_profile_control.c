#include "device/pc_profile.h"

void pc_profile_enable(PcProfile *profile, bool enabled)
{
    profile->enabled = enabled && !profile->full;
}

void pc_profile_reset(PcProfile *profile)
{
    bool enabled = profile->enabled || profile->full;
    profile->enabled = false;
    for (uint32_t index = 0; index < profile->bucket_count; index++) {
        profile->buckets[index] = 0;
    }
    profile->samples = 0;
    profile->idle_samples = 0;
    profile->overlay_samples = 0;
    profile->other_samples = 0;
    profile->full = false;
    profile->enabled = enabled;
}
