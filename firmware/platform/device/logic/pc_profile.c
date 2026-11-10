#include "device/pc_profile.h"

static bool inside(PcRange range, uint32_t address)
{
    return address - range.base < range.bytes;
}

static void count_code(PcProfile *profile, uint32_t address)
{
    uint16_t *bucket = &profile->buckets[(address - profile->code.base) >> profile->bucket_shift];
    *bucket = (uint16_t)(*bucket + 1);
    if (*bucket == PC_PROFILE_BUCKET_FULL) {
        profile->enabled = false;
        profile->full = true;
    }
}

void pc_profile_sample(PcProfile *profile, uint32_t pc)
{
    if (!profile->enabled) {
        return;
    }
    uint32_t address = pc & ~1u;
    profile->samples++;
    if (inside(profile->idle, address)) {
        profile->idle_samples++;
    } else if (inside(profile->code, address)) {
        count_code(profile, address);
    } else if (inside(profile->overlay, address)) {
        profile->overlay_samples++;
    } else {
        profile->other_samples++;
    }
}

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
