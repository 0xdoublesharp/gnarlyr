#include "device/debug_report.h"

#include <stddef.h>

#include "core/key.h"
#include "core/key_script.h"
#include "core/string_builder.h"

enum {
    HEX_DIGITS = 8,
    PERCENT = 100,
    WRAP_FREE_UPTIME_MS = 4294967,
    KEY_MASK = 0xFF,
    GESTURE_SHIFT = 8,
    TRACK_SHIFT = 8,
    STATE_MASK = 0xFF
};

typedef struct Writer {
    StringBuilder text;
    size_t line_start;
    bool truncated;
} Writer;

static const char UNKNOWN[] = "unknown";
static const char *const EVENT_NAMES[DEBUG_EVENT_KIND_COUNT] = {
    [DEBUG_EVENT_KEY] = "key",
    [DEBUG_EVENT_SCREEN] = "screen",
    [DEBUG_EVENT_PLAYER] = "player",
    [DEBUG_EVENT_UNDERRUN] = "underrun",
    [DEBUG_EVENT_OUTPUT_UNDERRUN] = "output_underrun",
    [DEBUG_EVENT_REFUSAL] = "refusal",
    [DEBUG_EVENT_STORAGE] = "storage",
    [DEBUG_EVENT_USB_START] = "usb_start",
    [DEBUG_EVENT_USB_END] = "usb_end",
    [DEBUG_EVENT_SCRIPT] = "script",
};
static const char *const GESTURE_NAMES[] = {"press", "long", "repeat", "double", "triple"};
static const char *const PLAYER_NAMES[] = {"idle", "playing", "paused"};
static const char *const AUDIO_NAMES[] = {"stopped", "playing", "paused"};
static const char *const USB_END_NAMES[] = {"none", "ejected", "unplugged", "no_host"};
static const char *const STEP_NAMES[HAL_START_STEP_COUNT] = {
    "none",        "backlight_on", "backlight_off", "clock_switch",
    "core1_start", "core1_stop",   "card_write",    "watchdog_test"};
static const char *const PART_NAMES[DEBUG_FRAME_PARTS] = {
    "frame.part.covers_us",   "frame.part.reflections_us", "frame.part.text_us",
    "frame.part.backdrop_us", "frame.part.hash_us",        "frame.part.audio_us"};

#define NAME_IN(names, index)                                                                      \
    ((size_t)(index) < sizeof(names) / sizeof(names)[0] ? (names)[index] : UNKNOWN)

static void begin(Writer *writer, const char *key)
{
    writer->line_start = writer->text.length;
    string_builder_append(&writer->text, key);
    string_builder_append(&writer->text, ": ");
}

static void finish(Writer *writer)
{
    string_builder_append_char(&writer->text, '\n');
    if (!writer->text.overflowed) {
        return;
    }
    writer->text.length = writer->line_start;
    writer->text.buffer[writer->line_start] = '\0';
    writer->text.capacity = writer->line_start + 1;
    writer->truncated = true;
}

static void put_text(Writer *writer, const char *key, const char *value)
{
    begin(writer, key);
    string_builder_append(&writer->text, value);
    finish(writer);
}

static void put_unsigned(Writer *writer, const char *key, uint32_t value)
{
    begin(writer, key);
    string_builder_append_unsigned(&writer->text, value);
    finish(writer);
}

static void put_known(Writer *writer, const char *key, bool known, uint32_t value)
{
    if (known) {
        put_unsigned(writer, key, value);
    } else {
        put_text(writer, key, UNKNOWN);
    }
}

static void append_hex(StringBuilder *text, uint32_t value, int digits)
{
    static const char DIGITS[] = "0123456789abcdef";
    for (int digit = digits - 1; digit >= 0; digit--) {
        string_builder_append_char(text, DIGITS[(value >> (4 * digit)) & 0xFu]);
    }
}

static int hex_digits(uint32_t value)
{
    int digits = 1;
    while (digits < HEX_DIGITS && (value >> (4 * digits)) != 0) {
        digits++;
    }
    return digits;
}

static void put_address(Writer *writer, const char *key, bool known, uint32_t value)
{
    begin(writer, key);
    if (known) {
        string_builder_append(&writer->text, "0x");
        append_hex(&writer->text, value, HEX_DIGITS);
    } else {
        string_builder_append(&writer->text, UNKNOWN);
    }
    finish(writer);
}

static uint32_t share_percent(uint32_t part, uint32_t whole)
{
    return part / (whole / PERCENT);
}

static void put_share(Writer *writer, const char *key, uint32_t part, uint32_t whole)
{
    bool known = whole >= PERCENT && part <= whole;
    put_known(writer, key, known, known ? share_percent(part, whole) : 0);
}

static void put_build(Writer *writer, const char *version)
{
    const char *tag = version;
    begin(writer, "firmware.version");
    for (; *tag != '\0' && *tag != '-'; tag++) {
        string_builder_append_char(&writer->text, *tag);
    }
    finish(writer);
    put_text(writer, "firmware.build", *tag == '-' && tag[1] != '\0' ? tag + 1 : UNKNOWN);
}

static void put_system(Writer *writer, const DebugReportInput *input)
{
    put_unsigned(writer, "debug_report", DEBUG_REPORT_SCHEMA);
    put_text(writer, "firmware.name", input->name);
    put_build(writer, input->version);
    put_unsigned(writer, "uptime_ms", input->uptime_ms);
    put_unsigned(writer, "clock_mhz", input->clock_mhz);
    put_unsigned(writer, "core0.clock_us", input->clock_us);
    put_unsigned(writer, "core0.idle_us", input->idle_us);
    bool unwrapped = input->uptime_ms < WRAP_FREE_UPTIME_MS;
    put_share(writer, "core0.busy_percent", input->clock_us - input->idle_us,
              unwrapped ? input->clock_us : 0);
}

static void put_battery(Writer *writer, HalBattery battery)
{
    put_known(writer, "battery.millivolts", battery.known, battery.millivolts);
    put_known(writer, "battery.percent", battery.known, battery.percent);
    put_known(writer, "battery.charging", battery.known, battery.charging);
    put_unsigned(writer, "battery.usb_power", battery.usb_power);
}

static void put_start(Writer *writer, const HalStartInfo *start)
{
    bool warm = start->warm;
    put_unsigned(writer, "start.count", start->starts);
    put_unsigned(writer, "start.warm", warm);
    put_known(writer, "start.previous_uptime_ms", warm, start->previous_uptime_ms);
    put_known(writer, "start.previous_power_off", warm, start->previous_power_off);
    put_text(writer, "start.last_step", warm ? NAME_IN(STEP_NAMES, start->last_step) : UNKNOWN);
    put_known(writer, "start.last_step_finished", warm, start->last_step_finished);
    put_known(writer, "start.last_step_ms", warm, start->last_step_ms);
    put_known(writer, "start.last_millivolts", warm && start->last_millivolts != 0,
              start->last_millivolts);
    put_address(writer, "fault.cfsr", warm, start->fault_status);
    put_address(writer, "fault.hfsr", warm, start->hard_fault_status);
    put_address(writer, "fault.pc", warm, start->fault_pc);
}

static void put_usb(Writer *writer, HalUsbLast usb)
{
    bool known = usb.end != HAL_USB_END_NONE;
    put_text(writer, "usb.last_end", NAME_IN(USB_END_NAMES, usb.end));
    put_known(writer, "usb.left_mounted", known, usb.left_mounted);
    put_known(writer, "usb.internal_sectors_written", known, usb.internal_written);
    put_known(writer, "usb.card_sectors_written", known, usb.card_written);
    put_known(writer, "usb.writes_after_eject", known, usb.writes_after_eject);
}

static void put_average(Writer *writer, const char *key, uint32_t total, uint32_t count)
{
    put_known(writer, key, count != 0, count != 0 ? total / count : 0);
}

static void put_audio(Writer *writer, const DebugReportAudio *audio)
{
    const HalAudioStats *stats = &audio->stats;
    bool running = audio->running;
    bool decoded = stats->blocks != 0;
    put_text(writer, "audio.state", NAME_IN(AUDIO_NAMES, audio->state));
    put_unsigned(writer, "audio.running", running);
    put_known(writer, "audio.buffered_ms", running, stats->buffered_ms);
    put_known(writer, "audio.underruns", running, stats->underruns);
    put_unsigned(writer, "audio.output_underruns", stats->output_underruns);
    put_unsigned(writer, "audio.blocks", stats->blocks);
    put_average(writer, "audio.handoff_avg_us", stats->decode_total_us, stats->blocks);
    put_known(writer, "audio.handoff_max_us", decoded, stats->decode_max_us);
    put_average(writer, "audio.read_avg_us", stats->read_total_us, stats->blocks);
    put_known(writer, "audio.read_max_us", decoded, stats->read_max_us);
    put_unsigned(writer, "audio.busy_us", stats->busy_us);
    put_known(writer, "audio.elapsed_us", running, stats->elapsed_us);
    put_share(writer, "audio.busy_percent", stats->busy_us, running ? stats->elapsed_us : 0);
}

static void put_frames(Writer *writer, const DebugFrames *frames)
{
    bool drawn = frames->count != 0;
    put_unsigned(writer, "frame.count", frames->count);
    put_known(writer, "frame.total_avg_us", drawn, frames->total_us);
    put_known(writer, "frame.total_worst_us", drawn, frames->worst_us);
    put_known(writer, "frame.update_avg_us", drawn, frames->update_us);
    put_known(writer, "frame.draw_avg_us", drawn, frames->draw_us);
    put_known(writer, "frame.send_avg_us", drawn, frames->send_us);
    for (int part = 0; part < DEBUG_FRAME_PARTS; part++) {
        put_known(writer, PART_NAMES[part], drawn, frames->part_us[part]);
    }
}

static void append_event_text(StringBuilder *text, const DebugEvent *event)
{
    if (event->text[0] == '\0') {
        return;
    }
    string_builder_append_char(text, ' ');
    for (int index = 0; index < DEBUG_LOG_TEXT_BYTES && event->text[index] != '\0'; index++) {
        char character = event->text[index];
        string_builder_append_char(text, character >= ' ' && character <= '~' ? character : '?');
    }
}

static void append_named_pair(StringBuilder *text, const char *name, uint32_t number)
{
    string_builder_append(text, name);
    string_builder_append_char(text, ' ');
    string_builder_append_unsigned(text, number);
}

static void append_event_value(StringBuilder *text, const DebugEvent *event)
{
    uint32_t value = event->value;
    if (event->kind == DEBUG_EVENT_KEY) {
        string_builder_append(text, key_script_key_name((Key)(value & KEY_MASK)));
        string_builder_append_char(text, ' ');
        string_builder_append(text, NAME_IN(GESTURE_NAMES, value >> GESTURE_SHIFT));
    } else if (event->kind == DEBUG_EVENT_PLAYER) {
        append_named_pair(text, NAME_IN(PLAYER_NAMES, value & STATE_MASK), value >> TRACK_SHIFT);
    } else if (event->kind == DEBUG_EVENT_USB_END) {
        string_builder_append(text, NAME_IN(USB_END_NAMES, value));
    } else {
        string_builder_append_unsigned(text, value);
    }
}

static void put_event(Writer *writer, const DebugEvent *event)
{
    begin(writer, "event");
    string_builder_append_unsigned(&writer->text, event->time_ms);
    string_builder_append_char(&writer->text, ' ');
    string_builder_append(&writer->text, NAME_IN(EVENT_NAMES, event->kind));
    string_builder_append_char(&writer->text, ' ');
    append_event_value(&writer->text, event);
    append_event_text(&writer->text, event);
    finish(writer);
}

static void put_events(Writer *writer, const DebugLog *log)
{
    if (log == NULL) {
        put_text(writer, "events.recorded", UNKNOWN);
        return;
    }
    put_unsigned(writer, "events.recorded", log->recorded);
    put_unsigned(writer, "events.dropped", debug_log_dropped(log));
    for (uint32_t index = 0; index < debug_log_count(log); index++) {
        put_event(writer, debug_log_at(log, index));
    }
}

static uint32_t row_end(const PcProfile *profile, uint32_t first)
{
    uint32_t end = first;
    while (end < profile->bucket_count && end - first < DEBUG_REPORT_ROW_BUCKETS &&
           profile->buckets[end] != 0) {
        end++;
    }
    return end;
}

static void put_row(Writer *writer, const PcProfile *profile, uint32_t first, uint32_t end)
{
    begin(writer, "profile.row");
    append_hex(&writer->text, first, hex_digits(first));
    for (uint32_t index = first; index < end; index++) {
        string_builder_append_char(&writer->text, ' ');
        append_hex(&writer->text, profile->buckets[index], hex_digits(profile->buckets[index]));
    }
    finish(writer);
}

static void put_rows(Writer *writer, const PcProfile *profile)
{
    uint32_t index = 0;
    while (index < profile->bucket_count && !writer->truncated) {
        uint32_t end = row_end(profile, index);
        if (end == index) {
            index++;
            continue;
        }
        put_row(writer, profile, index, end);
        index = end;
    }
}

static const char *profile_state(const PcProfile *profile)
{
    if (profile->full) {
        return "full";
    }
    return profile->enabled ? "on" : "off";
}

static void put_profile(Writer *writer, const PcProfile *profile)
{
    if (profile == NULL) {
        put_text(writer, "profile.state", UNKNOWN);
        return;
    }
    put_text(writer, "profile.state", profile_state(profile));
    put_unsigned(writer, "profile.samples", profile->samples);
    put_unsigned(writer, "profile.idle", profile->idle_samples);
    put_unsigned(writer, "profile.overlay", profile->overlay_samples);
    put_unsigned(writer, "profile.other", profile->other_samples);
    put_address(writer, "profile.code_base", true, profile->code.base);
    put_unsigned(writer, "profile.bucket_bytes", 1u << profile->bucket_shift);
    put_unsigned(writer, "profile.buckets", profile->bucket_count);
    put_rows(writer, profile);
}

static uint32_t put_tail(Writer *writer, uint32_t capacity)
{
    uint32_t body = (uint32_t)writer->text.length;
    writer->text.capacity = capacity;
    writer->text.overflowed = false;
    put_unsigned(writer, "truncated", writer->truncated);
    put_unsigned(writer, "end", body);
    return (uint32_t)writer->text.length;
}

uint32_t debug_report_format(const DebugReportInput *input, char *text, uint32_t capacity)
{
    if (capacity <= DEBUG_REPORT_TAIL_BYTES) {
        return 0;
    }
    Writer writer = {.text = string_builder_make(text, capacity - DEBUG_REPORT_TAIL_BYTES)};
    put_system(&writer, input);
    put_battery(&writer, input->battery);
    put_start(&writer, &input->start);
    put_usb(&writer, input->usb);
    put_audio(&writer, &input->audio);
    put_frames(&writer, &input->frames);
    put_events(&writer, input->log);
    put_profile(&writer, input->profile);
    uint32_t length = put_tail(&writer, capacity);
    for (uint32_t index = length; index < capacity; index++) {
        text[index] = '\n';
    }
    return length;
}
