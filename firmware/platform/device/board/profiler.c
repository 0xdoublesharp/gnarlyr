#include <stdbool.h>
#include <stdint.h>

#include "core/seam.h"
#include "debug_probe.h"
#include "hal/system.h"

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

static PcProfile profile;
static bool held;

static uint32_t value_of(const uint8_t *linker_symbol)
{
    return (uint32_t)(uintptr_t)linker_symbol;
}

static PcProfile *prepared(void)
{
    if (profile.bucket_count == 0) {
        profile.buckets = device_profile_buckets;
        profile.bucket_shift = value_of(device_profile_bucket_shift);
        profile.code =
            (PcRange){value_of(device_profile_code_base), value_of(device_profile_code_bytes)};
        profile.idle = (PcRange){(uint32_t)(uintptr_t)board_profile_wait & ~1u, WAIT_BYTES};
        profile.overlay = (PcRange){value_of(device_profile_overlay_base),
                                    value_of(device_profile_overlay_bytes)};
        profile.bucket_count = value_of(device_profile_bucket_count);
    }
    return &profile;
}

void board_profile_sample(uint32_t pc)
{
    if (!held) {
        pc_profile_sample(&profile, pc);
    }
}

void board_profile_hold(bool hold)
{
    held = hold;
}

CORE_SEAM void board_profile_wait(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

const PcProfile *board_profile(void)
{
    return prepared();
}

HalProfileState hal_system_profile_state(void)
{
    if (profile.full) {
        return HAL_PROFILE_FULL;
    }
    return profile.enabled ? HAL_PROFILE_ON : HAL_PROFILE_OFF;
}

void hal_system_set_profiling(bool on)
{
    pc_profile_enable(prepared(), on);
}

void hal_system_reset_profile(void)
{
    pc_profile_reset(prepared());
}
