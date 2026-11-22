#include "debug_probe.h"
#include "hal/system.h"

void hal_system_set_profiling(bool on)
{
    pc_profile_enable(board_profile(), on);
}

void hal_system_reset_profile(void)
{
    pc_profile_reset(board_profile());
}
