#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/debug_log.h"
#include "core/debug_script.h"
#include "device/pc_profile.h"
#include "device/power_history.h"
#include "hal/audio.h"
#include "hal/power.h"
#include "hal/system.h"

enum {
    DEBUG_REPORT_SCHEMA = 1,
    DEBUG_REPORT_BYTES = 16384,
    DEBUG_REPORT_TAIL_BYTES = 48,
    DEBUG_REPORT_ROW_BUCKETS = 32,
    DEBUG_REPORT_ROW_SAMPLES = 4,
    DEBUG_REPORT_SLOTS = 3,
    DEBUG_REPORT_HEAD_BYTES = 128,
    DEBUG_REPORT_COLD_MS = 10000
};

typedef struct DebugReportAudio {
    bool running;
    HalAudioState state;
    HalAudioStats stats;
} DebugReportAudio;

typedef struct DebugReportSlot {
    bool valid;
    bool cold_start;
    uint32_t sequence;
} DebugReportSlot;

typedef struct DebugReportPlace {
    uint32_t slot;
    uint32_t sequence;
} DebugReportPlace;

typedef struct DebugReportPower {
    bool vbus;
    bool usb_powered;
    bool usb_kept_on;
    bool pin_known;
    bool pin_high;
    bool adc_known;
    bool adc_from_gauge;
    uint16_t adc_raw;
    uint32_t gauge_readings;
    uint32_t gauge_millivolts;
    uint8_t percent_plain;
    uint8_t percent_gauge;
    bool shown_known;
    uint8_t percent_shown;
    const PowerHistory *history;
} DebugReportPower;

typedef enum DebugLoopConnect {
    DEBUG_LOOP_CONNECT_NONE,
    DEBUG_LOOP_CONNECT_WAITING,
    DEBUG_LOOP_CONNECT_ANSWERED,
    DEBUG_LOOP_CONNECT_NO_HOST
} DebugLoopConnect;

typedef struct DebugReportLoop {
    bool restarted;
    bool cable_at_connect;
    uint8_t last_connect;
    uint32_t attempts;
    uint32_t answered;
    uint32_t unanswered;
} DebugReportLoop;

typedef struct DebugReportInput {
    uint32_t sequence;
    DebugReportReason reason;
    const char *name;
    const char *version;
    uint32_t uptime_ms;
    uint32_t clock_mhz;
    uint32_t clock_us;
    uint32_t idle_us;
    HalBattery battery;
    DebugReportPower power;
    HalStartInfo start;
    HalUsbLast usb;
    DebugReportAudio audio;
    DebugFrames frames;
    DebugReportLoop loop;
    const DebugScriptRun *script;
    uint32_t interrupted_line;
    const DebugLog *log;
    const PcProfile *profile;
} DebugReportInput;

uint32_t debug_report_format(const DebugReportInput *input, char *text, uint32_t capacity);
bool debug_report_cold_start(const HalStartInfo *start, uint32_t uptime_ms);
DebugReportSlot debug_report_read_head(const char *head, uint32_t length);
DebugReportPlace debug_report_place(const DebugReportSlot *slots, bool cold_start);
