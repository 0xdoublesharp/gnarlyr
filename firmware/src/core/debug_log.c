#include "core/debug_log.h"

#include <stddef.h>

DebugEvent debug_event_make(uint32_t time_ms, DebugEventKind kind, uint32_t value, const char *text)
{
    DebugEvent event = {.time_ms = time_ms, .value = value, .kind = (uint8_t)kind};
    for (int index = 0; text != NULL && index < DEBUG_LOG_TEXT_BYTES && text[index] != '\0';
         index++) {
        event.text[index] = text[index];
    }
    return event;
}

void debug_log_reset(DebugLog *log)
{
    *log = (DebugLog){0};
}

void debug_log_record(DebugLog *log, const DebugEvent *event)
{
    log->events[log->next_slot] = *event;
    log->next_slot = (log->next_slot + 1) % DEBUG_LOG_CAPACITY;
    if (log->recorded < UINT32_MAX) {
        log->recorded++;
    }
}

uint32_t debug_log_count(const DebugLog *log)
{
    return log->recorded < DEBUG_LOG_CAPACITY ? log->recorded : DEBUG_LOG_CAPACITY;
}

uint32_t debug_log_dropped(const DebugLog *log)
{
    return log->recorded - debug_log_count(log);
}

const DebugEvent *debug_log_at(const DebugLog *log, uint32_t index)
{
    uint32_t oldest = log->recorded < DEBUG_LOG_CAPACITY ? 0 : log->next_slot;
    return &log->events[(oldest + index) % DEBUG_LOG_CAPACITY];
}
