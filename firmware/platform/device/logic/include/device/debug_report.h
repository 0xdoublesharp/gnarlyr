#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/debug_log.h"
#include "device/pc_profile.h"
#include "hal/audio.h"
#include "hal/power.h"
#include "hal/system.h"

enum {
    DEBUG_REPORT_SCHEMA = 1,
    DEBUG_REPORT_BYTES = 16384,
    DEBUG_REPORT_TAIL_BYTES = 48,
    DEBUG_REPORT_ROW_BUCKETS = 16
};

typedef struct DebugReportAudio {
    bool running;
    HalAudioState state;
    HalAudioStats stats;
} DebugReportAudio;

typedef struct DebugReportInput {
    const char *name;
    const char *version;
    uint32_t uptime_ms;
    uint32_t clock_mhz;
    uint32_t clock_us;
    uint32_t idle_us;
    HalBattery battery;
    HalStartInfo start;
    HalUsbLast usb;
    DebugReportAudio audio;
    DebugFrames frames;
    const DebugLog *log;
    const PcProfile *profile;
} DebugReportInput;

uint32_t debug_report_format(const DebugReportInput *input, char *text, uint32_t capacity);
