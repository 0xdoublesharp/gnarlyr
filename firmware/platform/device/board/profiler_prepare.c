#include <stdint.h>

#include "debug_probe.h"

enum {
    WAIT_BYTES = 4
};

extern uint16_t device_profile_buckets[];
extern const uint8_t device_profile_bucket_count[];
extern const uint8_t device_profile_bucket_shift[];
extern const uint8_t device_profile_code_base[];
extern const uint8_t device_profile_code_bytes[];
extern const uint8_t device_profile_overlay_base[];
extern const uint8_t device_profile_overlay_bytes[];
extern const uint8_t device_profile_module_base[];
extern const uint8_t device_profile_module_bytes[];

static uint32_t value_of(const uint8_t *linker_symbol)
{
    return (uint32_t)(uintptr_t)linker_symbol;
}

PcProfile *board_profile(void)
{
    PcProfile *profile = &board_pc_profile;
    if (profile->bucket_count == 0) {
        profile->buckets = device_profile_buckets;
        profile->bucket_shift = value_of(device_profile_bucket_shift);
        profile->code =
            (PcRange){value_of(device_profile_code_base), value_of(device_profile_code_bytes)};
        profile->idle = (PcRange){(uint32_t)(uintptr_t)board_profile_wait & ~1u, WAIT_BYTES};
        profile->overlay = (PcRange){value_of(device_profile_overlay_base),
                                     value_of(device_profile_overlay_bytes)};
        profile->modules =
            (PcRange){value_of(device_profile_module_base), value_of(device_profile_module_bytes)};
        profile->bucket_count = value_of(device_profile_bucket_count);
    }
    return profile;
}
