#include <stdbool.h>
#include <stdint.h>

#include "core/seam.h"
#include "debug_probe.h"
#include "hal/system.h"

PcProfile board_pc_profile;
static bool held;

void board_profile_sample(uint32_t pc)
{
    if (!held) {
        pc_profile_sample(&board_pc_profile, pc);
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

HalProfileState hal_system_profile_state(void)
{
    if (board_pc_profile.full) {
        return HAL_PROFILE_FULL;
    }
    return board_pc_profile.enabled ? HAL_PROFILE_ON : HAL_PROFILE_OFF;
}
