#include "device/debug_report.h"

#include <stddef.h>

#include "core/key.h"
#include "core/key_script.h"
#include "core/string_builder.h"
#include "device/battery_gauge.h"

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
    [DEBUG_EVENT_POWER] = "power",
};
static const char *const REASON_NAMES[DEBUG_REPORT_REASON_COUNT] = {
    [DEBUG_REPORT_NONE] = "none",
    [DEBUG_REPORT_USB_SESSION] = "usb_session",
    [DEBUG_REPORT_SHUTDOWN] = "shutdown",
    [DEBUG_REPORT_AUTO_OFF] = "auto_off",
    [DEBUG_REPORT_SLEEP_TIMER] = "sleep_timer",
    [DEBUG_REPORT_LOW_BATTERY] = "low_battery",
    [DEBUG_REPORT_UNPLUGGED] = "unplugged",
    [DEBUG_REPORT_RESTART] = "restart",
};
static const char SCHEMA_KEY[] = "debug_report";
static const char SEQUENCE_KEY[] = "report.sequence";
static const char COLD_KEY[] = "report.cold_start";
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

bool debug_report_cold_start(const HalStartInfo *start, uint32_t uptime_ms)
{
    return !start->warm && uptime_ms < DEBUG_REPORT_COLD_MS;
}

static void put_system(Writer *writer, const DebugReportInput *input)
{
    put_unsigned(writer, SCHEMA_KEY, DEBUG_REPORT_SCHEMA);
    put_unsigned(writer, SEQUENCE_KEY, input->sequence);
    put_text(writer, "report.reason", NAME_IN(REASON_NAMES, input->reason));
    put_unsigned(writer, COLD_KEY, debug_report_cold_start(&input->start, input->uptime_ms));
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

static const char *reading_source(bool from_gauge, bool known)
{
    if (!known) {
        return UNKNOWN;
    }
    return from_gauge ? "gauge" : "start_up_sample";
}

static void put_battery(Writer *writer, const DebugReportInput *input)
{
    HalBattery battery = input->battery;
    bool sampled = !battery.known && input->power.adc_known;
    uint32_t millivolts = sampled ? battery_millivolts(input->power.adc_raw) : battery.millivolts;
    uint32_t percent = sampled ? battery_percent_for(millivolts) : battery.percent;
    put_text(writer, "battery.source", reading_source(battery.known, battery.known || sampled));
    put_known(writer, "battery.millivolts", battery.known || sampled, millivolts);
    put_known(writer, "battery.percent", battery.known || sampled, percent);
    put_unsigned(writer, "battery.charging", battery.charging);
    put_unsigned(writer, "battery.usb_power", battery.usb_power);
}

static void put_power(Writer *writer, const DebugReportInput *input)
{
    const DebugReportPower *power = &input->power;
    bool gauged = power->gauge_readings != 0;
    bool charging = input->battery.charging;
    put_unsigned(writer, "power.vbus", power->vbus);
    put_unsigned(writer, "power.usb_powered", power->usb_powered);
    put_known(writer, "power.charge_pin", power->pin_known, power->pin_high);
    put_text(writer, "power.adc_source", reading_source(power->adc_from_gauge, power->adc_known));
    put_known(writer, "power.adc_raw", power->adc_known, power->adc_raw);
    put_known(writer, "power.adc_millivolts", power->adc_known, battery_millivolts(power->adc_raw));
    put_unsigned(writer, "power.gauge_readings", power->gauge_readings);
    put_known(writer, "power.gauge_millivolts", gauged, power->gauge_millivolts);
    put_known(writer, "power.percent_plain", gauged, power->percent_plain);
    put_known(writer, "power.percent_gauge", gauged, power->percent_gauge);
    put_known(writer, "power.percent_shown", power->shown_known, power->percent_shown);
    put_known(writer, "power.offset_applied", gauged, charging);
    put_known(writer, "power.capped", gauged,
              charging && power->percent_gauge >= BATTERY_CHARGING_MAX_PERCENT);
    put_known(writer, "power.held", gauged && power->shown_known,
              power->percent_shown != power->percent_gauge);
}

static void append_flag(StringBuilder *text, uint8_t flags, uint8_t flag)
{
    string_builder_append_char(text, (flags & flag) != 0 ? '1' : '0');
}

static void append_sample(StringBuilder *text, PowerSample sample)
{
    string_builder_append_unsigned(text, sample.minute);
    string_builder_append_char(text, ' ');
    string_builder_append_unsigned(text, sample.millivolts);
    string_builder_append_char(text, ' ');
    string_builder_append_unsigned(text, sample.percent);
    string_builder_append_char(text, ' ');
    append_flag(text, sample.flags, POWER_SAMPLE_VBUS);
    append_flag(text, sample.flags, POWER_SAMPLE_CHARGE_PIN);
    append_flag(text, sample.flags, POWER_SAMPLE_PLAYING);
    append_flag(text, sample.flags, POWER_SAMPLE_USB_POWERED);
    append_flag(text, sample.flags, POWER_SAMPLE_USB_KEPT_ON);
}

static void put_samples(Writer *writer, const PowerHistory *history, uint32_t first, uint32_t end)
{
    begin(writer, "power.sample");
    for (uint32_t index = first; index < end; index++) {
        if (index != first) {
            string_builder_append(&writer->text, "; ");
        }
        append_sample(&writer->text, power_history_at(history, index));
    }
    finish(writer);
}

static void put_history(Writer *writer, const PowerHistory *history)
{
    if (history == NULL) {
        put_text(writer, "power.samples", UNKNOWN);
        return;
    }
    uint32_t count = power_history_count(history);
    put_unsigned(writer, "power.samples", history->recorded);
    put_unsigned(writer, "power.sample_interval_s", POWER_HISTORY_INTERVAL_S);
    for (uint32_t first = 0; first < count; first += DEBUG_REPORT_ROW_SAMPLES) {
        uint32_t end = first + DEBUG_REPORT_ROW_SAMPLES;
        put_samples(writer, history, first, end < count ? end : count);
    }
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

static void put_usb(Writer *writer, HalUsbLast usb, bool kept_on)
{
    bool known = usb.end != HAL_USB_END_NONE;
    put_text(writer, "usb.sense_mode", kept_on ? "kept_on" : "normal");
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
    } else if (event->kind == DEBUG_EVENT_POWER) {
        append_named_pair(text, (value & POWER_SAMPLE_VBUS) != 0 ? "1" : "0",
                          (value & POWER_SAMPLE_CHARGE_PIN) != 0 ? 1 : 0);
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
    uint32_t end = first + DEBUG_REPORT_ROW_BUCKETS;
    end = end < profile->bucket_count ? end : profile->bucket_count;
    while (end > first && profile->buckets[end - 1] == 0) {
        end--;
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
        if (profile->buckets[index] == 0) {
            index++;
            continue;
        }
        uint32_t end = row_end(profile, index);
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
    put_battery(&writer, input);
    put_power(&writer, input);
    put_start(&writer, &input->start);
    put_usb(&writer, input->usb, input->power.usb_kept_on);
    put_audio(&writer, &input->audio);
    put_frames(&writer, &input->frames);
    put_events(&writer, input->log);
    put_history(&writer, input->power.history);
    put_profile(&writer, input->profile);
    uint32_t length = put_tail(&writer, capacity);
    for (uint32_t index = length; index < capacity; index++) {
        text[index] = '\n';
    }
    return length;
}

static const char *value_of(const char *head, uint32_t length, const char *key)
{
    for (uint32_t start = 0; start < length; start++) {
        uint32_t index = 0;
        while (key[index] != '\0' && start + index < length && head[start + index] == key[index]) {
            index++;
        }
        bool at_line = start == 0 || head[start - 1] == '\n';
        if (at_line && key[index] == '\0' && start + index + 2 <= length &&
            head[start + index] == ':' && head[start + index + 1] == ' ') {
            return head + start + index + 2;
        }
    }
    return NULL;
}

static bool number_at(const char *text, const char *end, uint32_t *number)
{
    uint32_t value = 0;
    const char *cursor = text;
    while (cursor != NULL && cursor < end && *cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (uint32_t)(*cursor - '0');
        cursor++;
    }
    *number = value;
    return cursor != NULL && cursor > text && cursor < end && *cursor == '\n';
}

DebugReportSlot debug_report_read_head(const char *head, uint32_t length)
{
    uint32_t schema = 0;
    uint32_t cold = 0;
    DebugReportSlot slot = {.valid = false};
    const char *end = head + length;
    slot.valid = number_at(value_of(head, length, SCHEMA_KEY), end, &schema) &&
                 schema == DEBUG_REPORT_SCHEMA &&
                 number_at(value_of(head, length, SEQUENCE_KEY), end, &slot.sequence) &&
                 number_at(value_of(head, length, COLD_KEY), end, &cold);
    slot.cold_start = cold != 0;
    return slot;
}

static int newest_slot(const DebugReportSlot *slots)
{
    int newest = -1;
    for (int slot = 0; slot < DEBUG_REPORT_SLOTS; slot++) {
        if (slots[slot].valid && (newest < 0 || slots[slot].sequence > slots[newest].sequence)) {
            newest = slot;
        }
    }
    return newest;
}

static uint32_t slot_to_replace(const DebugReportSlot *slots)
{
    uint32_t oldest = 0;
    for (uint32_t slot = 0; slot < DEBUG_REPORT_SLOTS; slot++) {
        if (!slots[slot].valid) {
            return slot;
        }
        if (slots[slot].sequence < slots[oldest].sequence) {
            oldest = slot;
        }
    }
    return oldest;
}

DebugReportPlace debug_report_place(const DebugReportSlot *slots, bool cold_start)
{
    int newest = newest_slot(slots);
    if (newest < 0) {
        return (DebugReportPlace){.slot = 0, .sequence = 1};
    }
    bool replace_newest = cold_start && slots[newest].cold_start;
    return (DebugReportPlace){.slot = replace_newest ? (uint32_t)newest : slot_to_replace(slots),
                              .sequence = slots[newest].sequence + 1};
}
