#pragma once

#include <stdint.h>

enum {
    DEBUG_LOG_CAPACITY = 48,
    DEBUG_LOG_TEXT_BYTES = 7,
    DEBUG_FRAME_PARTS = 6
};

typedef enum DebugEventKind {
    DEBUG_EVENT_KEY,
    DEBUG_EVENT_SCREEN,
    DEBUG_EVENT_PLAYER,
    DEBUG_EVENT_UNDERRUN,
    DEBUG_EVENT_OUTPUT_UNDERRUN,
    DEBUG_EVENT_REFUSAL,
    DEBUG_EVENT_STORAGE,
    DEBUG_EVENT_USB_START,
    DEBUG_EVENT_USB_END,
    DEBUG_EVENT_SCRIPT,
    DEBUG_EVENT_POWER,
    DEBUG_EVENT_KIND_COUNT
} DebugEventKind;

typedef enum DebugReportReason {
    DEBUG_REPORT_NONE,
    DEBUG_REPORT_USB_SESSION,
    DEBUG_REPORT_SHUTDOWN,
    DEBUG_REPORT_AUTO_OFF,
    DEBUG_REPORT_SLEEP_TIMER,
    DEBUG_REPORT_LOW_BATTERY,
    DEBUG_REPORT_UNPLUGGED,
    DEBUG_REPORT_RESTART,
    DEBUG_REPORT_REASON_COUNT
} DebugReportReason;

typedef struct DebugEvent {
    uint32_t time_ms;
    uint32_t value;
    uint8_t kind;
    char text[DEBUG_LOG_TEXT_BYTES];
} DebugEvent;

typedef struct DebugLog {
    DebugEvent events[DEBUG_LOG_CAPACITY];
    uint32_t recorded;
} DebugLog;

typedef struct DebugFrames {
    uint32_t count;
    uint32_t total_us;
    uint32_t worst_us;
    uint32_t update_us;
    uint32_t draw_us;
    uint32_t send_us;
    uint32_t part_us[DEBUG_FRAME_PARTS];
} DebugFrames;

DebugEvent debug_event_make(uint32_t time_ms, DebugEventKind kind, uint32_t value,
                            const char *text);
void debug_log_reset(DebugLog *log);
void debug_log_record(DebugLog *log, const DebugEvent *event);
uint32_t debug_log_count(const DebugLog *log);
uint32_t debug_log_dropped(const DebugLog *log);
const DebugEvent *debug_log_at(const DebugLog *log, uint32_t index);
