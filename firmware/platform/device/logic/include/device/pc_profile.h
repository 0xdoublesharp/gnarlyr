#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    PC_PROFILE_BUCKET_FULL = 0xFFFF
};

typedef struct PcRange {
    uint32_t base;
    uint32_t bytes;
} PcRange;

typedef struct PcProfile {
    uint16_t *buckets;
    uint32_t bucket_count;
    uint32_t bucket_shift;
    PcRange code;
    PcRange idle;
    PcRange overlay;
    uint32_t samples;
    uint32_t idle_samples;
    uint32_t overlay_samples;
    uint32_t other_samples;
    bool enabled;
    bool full;
} PcProfile;

void pc_profile_sample(PcProfile *profile, uint32_t pc);
void pc_profile_enable(PcProfile *profile, bool enabled);
void pc_profile_reset(PcProfile *profile);
