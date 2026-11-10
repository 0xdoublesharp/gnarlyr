#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/debug_report.h"
#include "device/pc_profile.h"
#include "usb_session.h"

#ifdef ECHO_DEBUG_LOOP

void board_profile_sample(uint32_t pc);
void board_profile_wait(void);
const PcProfile *board_profile(void);
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
void board_debug_usb_starting(bool dac);
void board_debug_usb_ended(void);
const DebugReportAudio *board_debug_audio(void);
void board_debug_power_off(void);
void debug_report_write(const UsbShared *shared, uint32_t reason);

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
}

static inline void board_debug_usb_ended(void)
{
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
