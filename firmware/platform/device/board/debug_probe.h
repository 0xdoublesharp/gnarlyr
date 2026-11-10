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
void board_debug_usb_starting(bool dac);
void board_debug_usb_ended(void);
const DebugReportAudio *board_debug_audio(void);
void debug_report_write(const UsbShared *shared);

#else

static inline void board_profile_sample(uint32_t pc)
{
    (void)pc;
}

static inline void board_profile_wait(void)
{
    __asm__ volatile("wfi" ::: "memory");
}

static inline void board_debug_usb_starting(bool dac)
{
    (void)dac;
}

static inline void board_debug_usb_ended(void)
{
}

static inline void debug_report_write(const UsbShared *shared)
{
    (void)shared;
}

#endif
