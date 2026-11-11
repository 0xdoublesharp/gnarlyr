#include "app/linked_modules.h"
#include "app/rescan_module.h"
#include "hal/system.h"

enum {
    BOOT_CLOCK_MHZ = 24
};

static uint32_t clock_mhz = BOOT_CLOCK_MHZ;
static bool profiling;

const char *hal_system_version(void)
{
    return "simulator";
}

HalBootClocks hal_system_boot_clocks(void)
{
    return (HalBootClocks){0};
}

HalUsbCore hal_system_usb_core(void)
{
    return (HalUsbCore){.captured = false};
}

HalUsbLast hal_system_usb_last(void)
{
    return (HalUsbLast){.end = HAL_USB_END_NONE};
}

void hal_system_set_dac_volume(uint8_t level)
{
    (void)level;
}

void hal_system_request_usb_mode(HalUsbMode mode)
{
    (void)mode;
}

void hal_system_set_usb_mode(HalUsbMode mode)
{
    (void)mode;
}

uint32_t hal_system_clock_mhz(void)
{
    return clock_mhz;
}

bool hal_system_set_clock_mhz(uint32_t mhz)
{
    clock_mhz = mhz;
    return true;
}

void hal_system_coast(bool coast)
{
    (void)coast;
}

bool hal_system_restart_pending(void)
{
    return false;
}

const void *hal_system_load_rescan_code(void)
{
    return &RESCAN_MODULE_ENTRY;
}

const void *hal_system_load_module(uint32_t module)
{
    return app_linked_module(module);
}

const void *hal_system_load_debug_code(void)
{
    return app_linked_debug_code();
}

HalHighRamTest hal_system_high_ram_test(void)
{
    return (HalHighRamTest){.status = HAL_HIGH_RAM_UNAVAILABLE};
}

void *hal_system_borrow_scratch(uint32_t *size)
{
    static uint8_t scratch[384 * 1024];
    *size = sizeof scratch;
    return scratch;
}

void *hal_system_borrow_listing(uint32_t *size)
{
    static uint8_t listing[96 * 1024];
    *size = sizeof listing;
    return listing;
}

void hal_system_return_scratch(void)
{
}

void hal_system_stall_test(void)
{
}

void hal_system_keep_alive(void)
{
}

uint32_t hal_system_idle_microseconds(void)
{
    return 0;
}

HalLoopCycles hal_system_loop_cycles(void)
{
    return (HalLoopCycles){0};
}

HalCore1Test hal_system_core1_test(void)
{
    return (HalCore1Test){.status = HAL_CORE1_UNAVAILABLE};
}

HalCore1Accel hal_system_core1_accel_test(void)
{
    return (HalCore1Accel){.status = HAL_CORE1_UNAVAILABLE};
}

HalStartInfo hal_system_start_info(void)
{
    return (HalStartInfo){.warm = false, .starts = 1};
}

void hal_system_watchdog_test(void)
{
}

HalProfileState hal_system_profile_state(void)
{
    return profiling ? HAL_PROFILE_ON : HAL_PROFILE_OFF;
}

void hal_system_set_profiling(bool on)
{
    profiling = on;
}

void hal_system_reset_profile(void)
{
}

HalUsbSense hal_system_usb_sense(void)
{
    return (HalUsbSense){.kept_on = false};
}

void hal_system_keep_usb_on(bool kept_on)
{
    (void)kept_on;
}

void hal_system_usb_reconnect(void)
{
}

void hal_system_usb_connect(bool on)
{
    (void)on;
}

uint8_t hal_system_power_levels(void)
{
    return 0;
}

uint32_t hal_system_power_samples(void)
{
    return 0;
}

void hal_system_hold_usb_supply_off(bool held)
{
    (void)held;
}

HalProbe hal_system_probe(HalProbeKind kind, uint32_t first, uint32_t second)
{
    (void)kind;
    (void)first;
    (void)second;
    return (HalProbe){.allowed = false};
}

HalRtcRead hal_system_rtc_read(void)
{
    return (HalRtcRead){.status = HAL_RTC_NO_ACK};
}

void hal_system_save_report(uint32_t reason)
{
    (void)reason;
}

void hal_system_script_mark(uint32_t line)
{
    (void)line;
}
