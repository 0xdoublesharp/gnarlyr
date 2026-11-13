#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/debug_report.h"
#include "device/fat.h"
#include "device/pc_profile.h"
#include "usb_session.h"

#define DEBUG_RESTART_PATH "/" FIRMWARE_DATA_DIRECTORY "/debug/restart.req"
#define DEBUG_RESTART_REQUEST "RESTART AFTER INSTALL"
#define DEBUG_RESTART_DONE "CONNECT AFTER RESTART"

#ifdef ECHO_DEBUG_LOOP

void board_profile_sample(uint32_t pc);
void board_profile_wait(void);
const PcProfile *board_profile(void);
void board_profile_hold(bool hold);
typedef struct BoardBatteryReading {
    bool known;
    uint16_t raw;
} BoardBatteryReading;

void board_debug_battery_reading(uint16_t raw);
BoardBatteryReading board_debug_battery_raw(void);
void board_debug_usb_powered(bool powered);
bool board_debug_usb_is_powered(void);
bool board_debug_usb_kept_on(void);
void board_debug_power_tick(uint32_t now_ms);
void board_debug_power_poll(void);
const PowerHistory *board_debug_power_history(void);
void board_debug_boot(void);
void board_debug_after_start(void);
uint32_t board_debug_interrupted_line(void);
void board_debug_read_grf(uint32_t *words);
const uint32_t *board_debug_grf_start(void);
bool board_debug_usb_supply_held_off(void);
bool board_debug_take_reconnect(void);
bool board_debug_usb_forced(void);
void board_debug_listen_stopped(void);
DebugReportLoop board_debug_loop(void);
void board_debug_connect_cancelled(void);
void board_debug_usb_starting(bool dac);
void board_debug_usb_ended(void);
const DebugReportAudio *board_debug_audio(void);
void board_debug_power_off(void);
void debug_report_write(const UsbShared *shared, uint32_t reason);
bool debug_script_take(void);
void debug_restart_after_install(FatVolume *volume, const UsbShared *shared);

#else

static inline void board_profile_sample(uint32_t pc)
{
    (void)pc;
}

static inline void board_profile_wait(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

static inline void board_debug_battery_reading(uint16_t raw)
{
    (void)raw;
}

static inline void board_debug_usb_powered(bool powered)
{
    (void)powered;
}

static inline bool board_debug_usb_kept_on(void)
{
    return false;
}

static inline void board_debug_power_tick(uint32_t now_ms)
{
    (void)now_ms;
}

static inline void board_debug_power_poll(void)
{
}

static inline void board_debug_usb_starting(bool dac)
{
    (void)dac;
    usb_session_reconnect_started();
}

static inline void board_debug_usb_ended(void)
{
    usb_session_reconnect_ended();
}

static inline void board_debug_boot(void)
{
}

static inline void board_debug_after_start(void)
{
}

static inline bool board_debug_usb_supply_held_off(void)
{
    return false;
}

static inline bool board_debug_take_reconnect(void)
{
    return usb_session_take_reconnect();
}

static inline bool board_debug_usb_forced(void)
{
    return usb_session_forced();
}

static inline void board_debug_listen_stopped(void)
{
    usb_session_reconnect_stopped();
}

static inline bool debug_script_take(void)
{
    return false;
}

static inline void debug_restart_after_install(FatVolume *volume, const UsbShared *shared)
{
    (void)volume;
    (void)shared;
}

static inline void debug_report_write(const UsbShared *shared, uint32_t reason)
{
    (void)shared;
    (void)reason;
}

static inline void board_debug_power_off(void)
{
}

#endif

#ifdef ECHO_USB_SCREEN_CAPTURES
void usb_screen_capture(AppUsbStatus status, struct UsbScene *scene, const char *name,
                        uint8_t slot);
#else
static inline void usb_screen_capture(AppUsbStatus status, struct UsbScene *scene, const char *name,
                                      uint8_t slot)
{
    (void)status;
    (void)scene;
    (void)name;
    (void)slot;
}
#endif
