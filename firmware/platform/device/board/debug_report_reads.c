#include "board.h"
#include "dac.h"
#include "debug_probe.h"
#include "hal/system.h"

HalStartInfo hal_system_start_info(void)
{
    const StartInfo *info = &board_start_info;
    return (HalStartInfo){.warm = info->warm,
                          .starts = info->starts,
                          .previous_uptime_ms = info->previous_uptime_ms,
                          .previous_power_off = info->previous_power_off,
                          .fault_status = info->fault_status,
                          .hard_fault_status = info->hard_fault_status,
                          .fault_pc = info->fault_address,
                          .last_step = info->last_step,
                          .last_step_finished = info->last_step_finished,
                          .last_step_ms = info->last_step_ms,
                          .last_millivolts = info->last_millivolts};
}

BatteryDisplay board_power_shown(void)
{
    return board_power_display;
}

DacDiagnostics dac_path_diagnostics(void)
{
    DacDiagnostics result = board_dac_diagnostics;
    result.up = dac_path_is_up();
    return result;
}

BoardBatteryReading board_debug_battery_raw(void)
{
    return board_debug_battery;
}
