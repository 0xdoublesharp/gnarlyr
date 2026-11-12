#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    DEBUG_SCRIPT_BYTES = 640,
    DEBUG_SCRIPT_LINES = 40,
    DEBUG_SCRIPT_OPS = 48,
    DEBUG_SCRIPT_MAX_MS = 60000,
    DEBUG_SCRIPT_NAME_BYTES = 12,
    DEBUG_SCRIPT_MEASURES = 4,
    DEBUG_SCRIPT_SCREENSHOTS = 4,
    DEBUG_SCRIPT_WORDS = 16,
    DEBUG_SCRIPT_PROBES = 16,
    DEBUG_SCRIPT_SOAKS = 2,
    DEBUG_SCRIPT_RTC_READS = 4,
    DEBUG_RTC_REGISTERS = 16,
    DEBUG_SOAK_MAX_MINUTES = 180,
    DEBUG_SOAK_LOW_MILLIVOLTS = 3600,
    DEBUG_SOAK_LOW_MS = 60000,
    DEBUG_SCRIPT_LIMIT_MS = 300000,
    DEBUG_SCRIPT_END_MS = 2000,
    DEBUG_SCRIPT_EVENT_STATE = 0x100
};

typedef enum DebugScriptState {
    DEBUG_SCRIPT_NONE,
    DEBUG_SCRIPT_PENDING,
    DEBUG_SCRIPT_RUNNING,
    DEBUG_SCRIPT_FINISHED,
    DEBUG_SCRIPT_TIMED_OUT,
    DEBUG_SCRIPT_ABORTED,
    DEBUG_SCRIPT_REFUSED_TOO_LONG,
    DEBUG_SCRIPT_REFUSED_RESTART_PENDING,
    DEBUG_SCRIPT_REFUSED_NO_BUILD,
    DEBUG_SCRIPT_REFUSED_BUILD,
    DEBUG_SCRIPT_STATE_COUNT
} DebugScriptState;

typedef enum DebugLineStatus {
    DEBUG_LINE_NOT_RUN,
    DEBUG_LINE_OK,
    DEBUG_LINE_UNKNOWN,
    DEBUG_LINE_BAD_ARGUMENT,
    DEBUG_LINE_FAILED,
    DEBUG_LINE_REFUSED,
    DEBUG_LINE_CUT_SHORT,
    DEBUG_LINE_STATUS_COUNT
} DebugLineStatus;

typedef enum DebugOpKind {
    DEBUG_OP_KEY,
    DEBUG_OP_WAIT,
    DEBUG_OP_MEASURE,
    DEBUG_OP_PROFILE,
    DEBUG_OP_SET,
    DEBUG_OP_SCREENSHOT,
    DEBUG_OP_USB_SENSE,
    DEBUG_OP_RECONNECT,
    DEBUG_OP_PEEK,
    DEBUG_OP_POKE,
    DEBUG_OP_PIN,
    DEBUG_OP_SUPPLY,
    DEBUG_OP_SCREEN,
    DEBUG_OP_SOAK,
    DEBUG_OP_RTC,
    DEBUG_OP_STANDBY
} DebugOpKind;

typedef enum DebugPinAction {
    DEBUG_PIN_READ,
    DEBUG_PIN_LOW,
    DEBUG_PIN_HIGH,
    DEBUG_PIN_INPUT,
    DEBUG_PIN_FUNCTION
} DebugPinAction;

enum {
    DEBUG_PIN_PORT_SHIFT = 8,
    DEBUG_PIN_FUNCTION_SHIFT = 8,
    DEBUG_PIN_COUNT = 32,
    DEBUG_PIN_FUNCTIONS = 4,
    DEBUG_PIN_STATE_OUTPUT = 0x10,
    DEBUG_PIN_STATE_FUNCTION_SHIFT = 8
};

typedef struct DebugOp {
    uint8_t kind;
    uint8_t line;
    uint8_t first_length;
    uint8_t second_length;
    uint16_t first;
    uint16_t second;
} DebugOp;

typedef struct DebugMeasure {
    uint32_t line;
    uint32_t duration_ms;
    uint32_t frames;
    uint32_t frame_us;
    uint32_t frame_worst_us;
    uint32_t update_us;
    uint32_t draw_us;
    uint32_t send_us;
    uint32_t audio_running;
    uint32_t buffered_ms;
    uint32_t underruns;
    uint32_t output_underruns;
    uint32_t blocks;
    uint32_t handoff_us;
    uint32_t handoff_max_us;
    uint32_t read_us;
    uint32_t read_max_us;
    uint32_t audio_busy_us;
    uint32_t audio_elapsed_us;
    uint32_t clock_us;
    uint32_t idle_us;
    uint32_t core1_cycles;
    uint32_t core1_idle_cycles;
} DebugMeasure;

typedef struct DebugProbe {
    uint8_t line;
    uint8_t levels;
    uint32_t time_ms;
    uint32_t before;
    uint32_t after;
} DebugProbe;

typedef enum DebugRtcAction {
    DEBUG_RTC_READ,
    DEBUG_RTC_SET
} DebugRtcAction;

typedef struct DebugRtcRead {
    uint8_t line;
    uint8_t status;
    uint8_t action;
    uint32_t time_ms;
    uint32_t written_s;
    uint8_t registers[DEBUG_RTC_REGISTERS];
} DebugRtcRead;

typedef enum DebugSoakEnd {
    DEBUG_SOAK_RUNNING,
    DEBUG_SOAK_DONE,
    DEBUG_SOAK_KEY,
    DEBUG_SOAK_LOW_BATTERY,
    DEBUG_SOAK_STOPPED,
    DEBUG_SOAK_END_COUNT
} DebugSoakEnd;

enum {
    DEBUG_SOAK_CABLE = 1,
    DEBUG_SOAK_CHARGE_KNOWN = 2,
    DEBUG_SOAK_CHARGE_PIN = 4,
    DEBUG_SOAK_SUPPLY_OFF = 8,
    DEBUG_SOAK_AUDIO = 16,
    DEBUG_SOAK_SCREEN = 32
};

typedef struct DebugSoakEdge {
    uint32_t sample;
    uint8_t levels;
    uint8_t clock_mhz;
} DebugSoakEdge;

typedef struct DebugSoak {
    uint8_t line;
    uint8_t end;
    uint32_t started_ms;
    uint32_t ended_ms;
    uint32_t idle_ms;
    DebugSoakEdge start;
    DebugSoakEdge finish;
} DebugSoak;

typedef struct DebugScriptRun {
    char text[DEBUG_SCRIPT_BYTES];
    uint16_t length;
    uint8_t state;
    uint8_t measure_count;
    uint8_t screenshot_count;
    uint8_t op_count;
    uint8_t word_count;
    uint8_t probe_count;
    uint8_t soak_count;
    uint8_t rtc_count;
    uint8_t status[DEBUG_SCRIPT_LINES];
    uint8_t screenshot_lines[DEBUG_SCRIPT_SCREENSHOTS];
    uint32_t started_ms;
    uint32_t finished_ms;
    uint32_t soak_ms;
    DebugMeasure measures[DEBUG_SCRIPT_MEASURES];
    DebugOp ops[DEBUG_SCRIPT_OPS];
    uint32_t words[DEBUG_SCRIPT_WORDS];
    DebugProbe probes[DEBUG_SCRIPT_PROBES];
    DebugSoak soaks[DEBUG_SCRIPT_SOAKS];
    DebugRtcRead rtc[DEBUG_SCRIPT_RTC_READS];
} DebugScriptRun;

typedef struct DebugScriptHost {
    const char *version;
    bool restart_pending;
} DebugScriptHost;

typedef enum DebugCommandKind {
    DEBUG_COMMAND_BLANK,
    DEBUG_COMMAND_BUILD,
    DEBUG_COMMAND_KEYS,
    DEBUG_COMMAND_WAIT,
    DEBUG_COMMAND_MEASURE,
    DEBUG_COMMAND_PROFILE,
    DEBUG_COMMAND_SET,
    DEBUG_COMMAND_SCREENSHOT,
    DEBUG_COMMAND_USB_SENSE,
    DEBUG_COMMAND_RECONNECT,
    DEBUG_COMMAND_PEEK,
    DEBUG_COMMAND_POKE,
    DEBUG_COMMAND_GPIO,
    DEBUG_COMMAND_SUPPLY,
    DEBUG_COMMAND_SCREEN,
    DEBUG_COMMAND_SOAK,
    DEBUG_COMMAND_RTC,
    DEBUG_COMMAND_STANDBY,
    DEBUG_COMMAND_UNKNOWN,
    DEBUG_COMMAND_BAD_ARGUMENT
} DebugCommandKind;

typedef enum DebugProfileAction {
    DEBUG_PROFILE_OFF,
    DEBUG_PROFILE_ON,
    DEBUG_PROFILE_RESET
} DebugProfileAction;

typedef struct DebugSpan {
    const char *text;
    size_t length;
} DebugSpan;

typedef struct DebugCommand {
    DebugCommandKind kind;
    uint32_t number;
    uint32_t second;
    DebugSpan argument;
    DebugSpan value;
} DebugCommand;

DebugSpan debug_script_next_line(const char *text, size_t length, size_t *offset);
uint32_t debug_script_line_count(const char *text, size_t length);
DebugCommand debug_script_parse(DebugSpan line);
bool debug_span_matches(DebugSpan span, const char *text);
void debug_script_load(DebugScriptRun *run, uint32_t length, DebugScriptHost host);
