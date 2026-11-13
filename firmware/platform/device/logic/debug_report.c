#include "device/debug_report.h"

#include <stddef.h>

#include "core/key.h"
#include "core/key_script.h"
#include "core/string_builder.h"
#include "device/battery_gauge.h"
#include "device/rtc_registers.h"

enum {
    HEX_DIGITS = 8,
    PERCENT = 100,
    PERMILLE = 1000,
    WRAP_FREE_UPTIME_MS = 4294967,
    KEY_MASK = 0xFF,
    GESTURE_SHIFT = 8,
    TRACK_SHIFT = 8,
    STATE_MASK = 0xFF,
    SOAK_GROUP = 3,
    SOAK_NAME_CAPACITY = 24,
    MINUTES_PER_HOUR = 60,
    MILLISECONDS_PER_MINUTE = 60000
};

_Static_assert((int)DEBUG_SOAK_CABLE == (int)HAL_PROBE_CABLE &&
                   (int)DEBUG_SOAK_CHARGE_KNOWN == (int)HAL_PROBE_CHARGE_KNOWN &&
                   (int)DEBUG_SOAK_CHARGE_PIN == (int)HAL_PROBE_CHARGE_PIN &&
                   (int)DEBUG_SOAK_SUPPLY_OFF == (int)HAL_PROBE_SUPPLY_OFF,
               "a soak keeps the board's levels as the board gives them");

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
    [DEBUG_EVENT_USB_CONNECT] = "usb_connect",
    [DEBUG_EVENT_SCAN] = "scan",
    [DEBUG_EVENT_COVER] = "cover",
};
static const char *const SCAN_PHASES[DEBUG_SCAN_PHASE_COUNT] = {
    [DEBUG_SCAN_START] = "start",
    [DEBUG_SCAN_LIBRARY] = "library",
    [DEBUG_SCAN_COVERS] = "covers",
    [DEBUG_SCAN_COVERS_DONE] = "covers_done",
    [DEBUG_SCAN_COVERS_STOPPED] = "covers_stopped",
    [DEBUG_SCAN_UP_TO_DATE] = "up_to_date",
    [DEBUG_SCAN_EMMC_OK] = "emmc_ok",
    [DEBUG_SCAN_EMMC_FAILED] = "emmc_failed",
    [DEBUG_SCAN_EMMC_FLAG_LOST] = "emmc_flag_lost",
};
static const char *const SCRIPT_STATES[DEBUG_SCRIPT_STATE_COUNT] = {
    [DEBUG_SCRIPT_NONE] = "none",
    [DEBUG_SCRIPT_PENDING] = "pending",
    [DEBUG_SCRIPT_RUNNING] = "running",
    [DEBUG_SCRIPT_FINISHED] = "finished",
    [DEBUG_SCRIPT_TIMED_OUT] = "timed_out",
    [DEBUG_SCRIPT_ABORTED] = "aborted",
    [DEBUG_SCRIPT_REFUSED_TOO_LONG] = "refused_too_long",
    [DEBUG_SCRIPT_REFUSED_RESTART_PENDING] = "refused_restart_pending",
    [DEBUG_SCRIPT_REFUSED_NO_BUILD] = "refused_no_build",
    [DEBUG_SCRIPT_REFUSED_BUILD] = "refused_build",
};
static const char *const LINE_STATUSES[DEBUG_LINE_STATUS_COUNT] = {
    [DEBUG_LINE_NOT_RUN] = "not_run",    [DEBUG_LINE_OK] = "ok",
    [DEBUG_LINE_UNKNOWN] = "unknown",    [DEBUG_LINE_BAD_ARGUMENT] = "bad_argument",
    [DEBUG_LINE_FAILED] = "failed",      [DEBUG_LINE_REFUSED] = "refused",
    [DEBUG_LINE_CUT_SHORT] = "cut_short"};
static const char *const SOAK_ENDS[DEBUG_SOAK_END_COUNT] = {[DEBUG_SOAK_RUNNING] = "running",
                                                            [DEBUG_SOAK_DONE] = "done",
                                                            [DEBUG_SOAK_KEY] = "key",
                                                            [DEBUG_SOAK_LOW_BATTERY] =
                                                                "low_battery",
                                                            [DEBUG_SOAK_STOPPED] = "stopped"};
static const char *const REASON_NAMES[DEBUG_REPORT_REASON_COUNT] = {
    [DEBUG_REPORT_NONE] = "none",
    [DEBUG_REPORT_USB_SESSION] = "usb_session",
    [DEBUG_REPORT_SHUTDOWN] = "shutdown",
    [DEBUG_REPORT_AUTO_OFF] = "auto_off",
    [DEBUG_REPORT_SLEEP_TIMER] = "sleep_timer",
    [DEBUG_REPORT_LOW_BATTERY] = "low_battery",
    [DEBUG_REPORT_UNPLUGGED] = "unplugged",
    [DEBUG_REPORT_RESTART] = "restart",
    [DEBUG_REPORT_NO_HOST] = "no_host",
};
static const char *const RTC_STATUSES[HAL_RTC_STATUS_COUNT] = {[HAL_RTC_OK] = "ok",
                                                               [HAL_RTC_NO_ACK] = "no_ack",
                                                               [HAL_RTC_TIMEOUT] = "timeout",
                                                               [HAL_RTC_NO_TIME] = "no_time",
                                                               [HAL_RTC_MISMATCH] = "mismatch"};
static const char *const RTC_ACTIONS[] = {[DEBUG_RTC_READ] = "read", [DEBUG_RTC_SET] = "set"};
static const char *const CONNECT_NAMES[] = {"none", "waiting", "answered", "no_host", "cancelled"};
static const char SCHEMA_KEY[] = "debug_report";
static const char SEQUENCE_KEY[] = "report.sequence";
static const char COLD_KEY[] = "report.cold_start";
static const char *const GESTURE_NAMES[] = {"press", "long", "repeat", "double", "triple"};
static const char *const PLAYER_NAMES[] = {"idle", "playing", "paused", "standby"};
_Static_assert(sizeof PLAYER_NAMES / sizeof PLAYER_NAMES[0] == DEBUG_PLAYER_STANDBY + 1,
               "the report names the standby state the event log records");
static const char *const AUDIO_NAMES[] = {"stopped", "playing", "paused"};
static const char *const USB_END_NAMES[] = {"none", "ejected", "unplugged", "no_host"};
static const char *const SPEED_FALLBACK_NAMES[] = {"none", "not_configured", "few_commands"};
static const char *const STEP_NAMES[HAL_START_STEP_COUNT] = {
    "none",       "backlight_on", "backlight_off", "clock_switch", "core1_start",
    "core1_stop", "card_write",   "watchdog_test", "loader"};
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

static void put_standby(Writer *writer, const Standby *standby)
{
    put_unsigned(writer, "standby.entries", standby->entries);
    put_unsigned(writer, "standby.wakes", standby->wakes);
    if (standby->entries == 0) {
        return;
    }
    put_known(writer, "standby.wake_latency_ms", standby->wakes != 0 && !standby->waking,
              standby->latency_ms);
    put_unsigned(writer, "standby.entry_ms", standby->entry_ms);
    put_unsigned(writer, "standby.wake_ms", standby->wake_ms);
    put_known(writer, "standby.entry_mv", standby->entry_mv != 0, standby->entry_mv);
    put_known(writer, "standby.wake_mv", standby->wake_mv != 0, standby->wake_mv);
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
    put_standby(writer, &input->standby);
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

static void put_setup(StringBuilder *text, const HalUsbSetup *setup)
{
    static const char OUTCOMES[] = "?asr";
    string_builder_append_char(text, ' ');
    append_hex(text, setup->request_type, 2);
    string_builder_append_char(text, '.');
    append_hex(text, setup->request, 2);
    string_builder_append_char(text, '.');
    append_hex(text, setup->value, 4);
    string_builder_append_char(text, '/');
    string_builder_append_unsigned(text, setup->length);
    string_builder_append_char(text, OUTCOMES[setup->outcome < 4 ? setup->outcome : 0]);
}

static void put_setup_trace(StringBuilder *text, const HalUsbSetupTrace *trace)
{
    uint32_t count = trace->seen < HAL_USB_SETUP_TRACE ? trace->seen : HAL_USB_SETUP_TRACE;
    string_builder_append(text, " setups=");
    string_builder_append_unsigned(text, trace->seen);
    for (uint32_t index = trace->seen - count; index < trace->seen; index++) {
        put_setup(text, &trace->setups[index % HAL_USB_SETUP_TRACE]);
    }
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
    if (usb.speed_fallback != 0) {
        begin(writer, "usb.speed_fallback");
        string_builder_append(&writer->text, NAME_IN(SPEED_FALLBACK_NAMES, usb.speed_fallback));
        string_builder_append(&writer->text, " commands=");
        string_builder_append_unsigned(&writer->text, usb.fallback_commands);
        put_setup_trace(&writer->text, &usb.fallback_setups);
        finish(writer);
    }
}

static void put_usb_words(Writer *writer, const DebugReportUsbWords *words)
{
    static const char *const START_KEYS[DEBUG_REPORT_GRF_WORDS] = {
        "usb.grf_e8_start", "usb.grf_84_start", "usb.grf_b4_start", "usb.grf_b8_start",
        "usb.grf_98_start"};
    static const char *const KEYS[DEBUG_REPORT_GRF_WORDS] = {
        "usb.grf_e8", "usb.grf_84", "usb.grf_b4", "usb.grf_b8", "usb.grf_98"};
    for (uint32_t index = 0; index < DEBUG_REPORT_GRF_WORDS; index++) {
        put_address(writer, START_KEYS[index], words->grf_start != NULL,
                    words->grf_start != NULL ? words->grf_start[index] : 0);
    }
    for (uint32_t index = 0; index < DEBUG_REPORT_GRF_WORDS; index++) {
        put_address(writer, KEYS[index], true, words->grf[index]);
    }
    put_address(writer, "usb.gotgctl", words->core_known, words->gotgctl);
    put_known(writer, "usb.b_session", words->core_known,
              (words->gotgctl & DEBUG_REPORT_B_SESSION_VALID) != 0);
}

static const char *boot_kind(const DebugReportInput *input)
{
    const HalStartInfo *start = &input->start;
    if (input->loop.restarted) {
        return "loop_restart";
    }
    if (!start->warm) {
        return "cold";
    }
    if ((start->fault_status | start->hard_fault_status | start->fault_pc) != 0) {
        return "fault";
    }
    return start->previous_power_off ? "restart" : "watchdog_or_reset";
}

static void put_loop(Writer *writer, const DebugReportInput *input)
{
    const DebugReportLoop *loop = &input->loop;
    put_text(writer, "loop.boot", boot_kind(input));
    put_unsigned(writer, "loop.connect_attempts", loop->attempts);
    put_unsigned(writer, "loop.connect_answered", loop->answered);
    put_unsigned(writer, "loop.connect_unanswered", loop->unanswered);
    put_text(writer, "loop.last_connect", NAME_IN(CONNECT_NAMES, loop->last_connect));
    put_known(writer, "loop.cable_bit_at_connect", loop->attempts != 0, loop->cable_at_connect);
}

static void append_card_field(StringBuilder *text, const char *name, uint32_t value)
{
    string_builder_append(text, name);
    string_builder_append_unsigned(text, value);
}

static void append_card_command(StringBuilder *text, const char *name, uint8_t index, uint32_t word)
{
    append_card_field(text, name, index);
    string_builder_append_char(text, '=');
    append_hex(text, word, HEX_DIGITS);
}

static void put_card(Writer *writer, const CardStartLog *card)
{
    bool clean = card->ready && card->failures == 0 && card->attempts <= 1;
    if (card->attempts == 0 || clean) {
        return;
    }
    begin(writer, "card.start");
    StringBuilder *text = &writer->text;
    append_card_field(text, "raw", card->raw_now);
    append_card_field(text, " present", card->present_now);
    append_card_field(text, " ejected", card->ejected_now);
    append_card_field(text, " ready", card->ready);
    append_card_field(text, " detected", card->detected);
    append_card_field(text, " powered", card->powered_before);
    append_card_field(text, " tries", card->attempts);
    append_card_field(text, " ms", card->init_ms);
    append_card_command(text, " last", card->last_command, card->last_response);
    append_card_command(text, " failed", card->failed_command, card->failed_interrupts);
    append_card_field(text, " errors", card->failures);
    append_card_field(text, " timeouts", card->timeouts);
    finish(writer);
}

static void put_average(Writer *writer, const char *key, uint32_t total, uint32_t count)
{
    put_known(writer, key, count != 0, count != 0 ? total / count : 0);
}

static void put_bluetooth(Writer *writer, const HalBluetoothLink *link)
{
    if (link == NULL) {
        return;
    }
    const HalBluetoothTimeline *time = &link->timeline;
    put_unsigned(writer, "bluetooth.module_on_ms", time->module_on_ms);
    put_unsigned(writer, "bluetooth.init_done_ms", time->init_done_ms);
    put_unsigned(writer, "bluetooth.connect_ms", time->connect_ms);
    put_unsigned(writer, "bluetooth.connected_ms", time->connected_ms);
    put_unsigned(writer, "bluetooth.stream_ms", time->stream_ms);
    put_unsigned(writer, "bluetooth.first_audio_ms", time->first_audio_ms);
    begin(writer, "bluetooth.reports");
    for (uint32_t index = 0; index < link->event_count; index++) {
        string_builder_append_char(&writer->text, link->events[index][0]);
        string_builder_append_char(&writer->text, link->events[index][1]);
        string_builder_append_char(&writer->text, ' ');
    }
    finish(writer);
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

static void append_plain(StringBuilder *text, DebugSpan span)
{
    for (size_t index = 0; index < span.length; index++) {
        char character = span.text[index];
        string_builder_append_char(text, character >= ' ' && character <= '~' ? character : '?');
    }
}

static void put_script_line(Writer *writer, const DebugScriptRun *script, uint32_t line,
                            DebugSpan span)
{
    begin(writer, "script.line");
    string_builder_append_unsigned(&writer->text, line);
    string_builder_append_char(&writer->text, ' ');
    string_builder_append(&writer->text, NAME_IN(LINE_STATUSES, script->status[line - 1]));
    string_builder_append_char(&writer->text, ' ');
    append_plain(&writer->text, span);
    finish(writer);
}

static void put_script_lines(Writer *writer, const DebugScriptRun *script)
{
    size_t offset = 0;
    for (uint32_t line = 1; line <= DEBUG_SCRIPT_LINES && offset < script->length; line++) {
        DebugSpan span = debug_script_next_line(script->text, script->length, &offset);
        if (debug_script_parse(span).kind != DEBUG_COMMAND_BLANK) {
            put_script_line(writer, script, line, span);
        }
    }
}

static void append_field(StringBuilder *text, const char *name, bool known, uint32_t value)
{
    string_builder_append_char(text, ' ');
    string_builder_append(text, name);
    string_builder_append_char(text, '=');
    if (known) {
        string_builder_append_unsigned(text, value);
    } else {
        string_builder_append(text, UNKNOWN);
    }
}

static void append_average(StringBuilder *text, const char *name, uint32_t total, uint32_t count)
{
    append_field(text, name, count != 0, count != 0 ? total / count : 0);
}

static void append_share(StringBuilder *text, const char *name, uint32_t part, uint32_t whole)
{
    bool known = whole >= PERCENT && part <= whole;
    append_field(text, name, known, known ? share_percent(part, whole) : 0);
}

static void append_permille(StringBuilder *text, const char *name, uint32_t part, uint32_t whole)
{
    bool known = whole >= PERMILLE && part <= whole;
    append_field(text, name, known, known ? part / (whole / PERMILLE) : 0);
}

static void append_measured_frames(StringBuilder *text, const DebugMeasure *measure)
{
    append_field(text, "frames", true, measure->frames);
    append_average(text, "frame_avg_us", measure->frame_us, measure->frames);
    append_field(text, "frame_worst_us", measure->frames != 0, measure->frame_worst_us);
    append_average(text, "update_avg_us", measure->update_us, measure->frames);
    append_average(text, "draw_avg_us", measure->draw_us, measure->frames);
    append_average(text, "send_avg_us", measure->send_us, measure->frames);
}

static void append_measured_audio(StringBuilder *text, const DebugMeasure *measure)
{
    bool running = measure->audio_running != 0;
    bool decoded = measure->blocks != 0;
    append_field(text, "audio", true, measure->audio_running);
    append_field(text, "buffered_ms", running, measure->buffered_ms);
    append_field(text, "underruns", running, measure->underruns);
    append_field(text, "output_underruns", true, measure->output_underruns);
    append_field(text, "blocks", true, measure->blocks);
    append_average(text, "handoff_avg_us", measure->handoff_us, measure->blocks);
    append_field(text, "handoff_max_us", decoded, measure->handoff_max_us);
    append_average(text, "read_avg_us", measure->read_us, measure->blocks);
    append_field(text, "read_max_us", decoded, measure->read_max_us);
    append_share(text, "audio_busy_percent", measure->audio_busy_us,
                 running ? measure->audio_elapsed_us : 0);
}

static void put_measure(Writer *writer, const DebugMeasure *measure)
{
    begin(writer, "script.measure");
    string_builder_append(&writer->text, "line=");
    string_builder_append_unsigned(&writer->text, measure->line);
    append_field(&writer->text, "ms", true, measure->duration_ms);
    append_measured_frames(&writer->text, measure);
    append_measured_audio(&writer->text, measure);
    append_share(&writer->text, "core0_busy_percent", measure->clock_us - measure->idle_us,
                 measure->clock_us);
    uint32_t core1_busy = measure->core1_cycles - measure->core1_idle_cycles;
    append_permille(&writer->text, "core1_busy_permille", core1_busy, measure->core1_cycles);
    finish(writer);
}

static void append_word(StringBuilder *text, const char *name, uint32_t value)
{
    string_builder_append_char(text, ' ');
    string_builder_append(text, name);
    string_builder_append(text, "=0x");
    append_hex(text, value, HEX_DIGITS);
}

static void put_probe(Writer *writer, const DebugProbe *probe)
{
    begin(writer, "script.probe");
    string_builder_append(&writer->text, "line=");
    string_builder_append_unsigned(&writer->text, probe->line);
    append_field(&writer->text, "ms", true, probe->time_ms);
    append_word(&writer->text, "before", probe->before);
    append_word(&writer->text, "after", probe->after);
    append_field(&writer->text, "cable", true, (probe->levels & HAL_PROBE_CABLE) != 0);
    append_field(&writer->text, "charge_pin", (probe->levels & HAL_PROBE_CHARGE_KNOWN) != 0,
                 (probe->levels & HAL_PROBE_CHARGE_PIN) != 0);
    finish(writer);
}

static void append_two_digits(StringBuilder *text, char separator, uint32_t value)
{
    string_builder_append_char(text, separator);
    string_builder_append_char(text, (char)('0' + value / 10 % 10));
    string_builder_append_char(text, (char)('0' + value % 10));
}

static void append_rtc_time(StringBuilder *text, const RtcTime *time)
{
    string_builder_append(text, " time=");
    string_builder_append_unsigned(text, time->year);
    append_two_digits(text, '-', time->month);
    append_two_digits(text, '-', time->day);
    append_two_digits(text, 'T', time->hour);
    append_two_digits(text, ':', time->minute);
    append_two_digits(text, ':', time->second);
}

static void append_rtc_registers(StringBuilder *text, const DebugRtcRead *read)
{
    string_builder_append(text, " registers=");
    for (uint32_t index = 0; index < DEBUG_RTC_REGISTERS; index++) {
        append_hex(text, read->registers[index], 2);
    }
}

static void append_rtc_decoded(StringBuilder *text, const DebugRtcRead *read)
{
    RtcTime time;
    bool valid = rtc_registers_decode(read->registers, &time);
    append_field(text, "vl", true, time.voltage_low);
    if (valid) {
        append_rtc_time(text, &time);
    } else {
        string_builder_append(text, " time=invalid");
    }
    append_field(text, "weekday", true, time.weekday);
    append_field(text, "century", true, time.century);
}

static void append_rtc_head(StringBuilder *text, const DebugRtcRead *read)
{
    string_builder_append(text, "line=");
    string_builder_append_unsigned(text, read->line);
    append_field(text, "ms", true, read->time_ms);
    string_builder_append(text, " action=");
    string_builder_append(text, NAME_IN(RTC_ACTIONS, read->action));
    append_field(text, "written_s", read->written_s != 0, read->written_s);
    string_builder_append(text, " status=");
    string_builder_append(text, NAME_IN(RTC_STATUSES, read->status));
}

static void put_rtc(Writer *writer, const DebugRtcRead *read)
{
    begin(writer, "script.rtc");
    append_rtc_head(&writer->text, read);
    if (read->status == HAL_RTC_OK || read->status == HAL_RTC_MISMATCH) {
        append_rtc_registers(&writer->text, read);
        append_rtc_decoded(&writer->text, read);
    } else {
        string_builder_append(&writer->text,
                              " registers=unknown vl=unknown time=unknown weekday=unknown "
                              "century=unknown");
    }
    finish(writer);
}

typedef struct SoakVoltage {
    uint32_t samples;
    bool known;
    bool rate_known;
    uint32_t first_millivolts;
    uint32_t last_millivolts;
    int32_t per_hour;
} SoakVoltage;

typedef struct SampleSum {
    uint32_t millivolts;
    uint32_t minutes;
} SampleSum;

static SampleSum sum_samples(const PowerHistory *history, uint32_t first, uint32_t count)
{
    uint32_t oldest = history->recorded - power_history_count(history);
    SampleSum sum = {0, 0};
    for (uint32_t index = 0; index < count; index++) {
        PowerSample sample = power_history_at(history, first - oldest + index);
        sum.millivolts += sample.millivolts;
        sum.minutes += sample.minute;
    }
    return sum;
}

static bool soak_samples_kept(const PowerHistory *history, const DebugSoak *soak)
{
    if (history == NULL || soak->finish.sample <= soak->start.sample) {
        return false;
    }
    uint32_t oldest = history->recorded - power_history_count(history);
    return soak->start.sample >= oldest && soak->finish.sample <= history->recorded;
}

static SoakVoltage soak_voltage(const PowerHistory *history, const DebugSoak *soak)
{
    SoakVoltage voltage = {.samples = soak->finish.sample - soak->start.sample};
    if (!soak_samples_kept(history, soak)) {
        return voltage;
    }
    uint32_t group = voltage.samples < SOAK_GROUP ? voltage.samples : SOAK_GROUP;
    SampleSum first = sum_samples(history, soak->start.sample, group);
    SampleSum last = sum_samples(history, soak->finish.sample - group, group);
    voltage.known = true;
    voltage.first_millivolts = first.millivolts / group;
    voltage.last_millivolts = last.millivolts / group;
    if (last.minutes > first.minutes) {
        int32_t change = (int32_t)last.millivolts - (int32_t)first.millivolts;
        voltage.rate_known = true;
        voltage.per_hour = change * MINUTES_PER_HOUR / (int32_t)(last.minutes - first.minutes);
    }
    return voltage;
}

static void append_rate(StringBuilder *text, const SoakVoltage *voltage)
{
    string_builder_append(text, " mv_per_hour=");
    if (!voltage->rate_known) {
        string_builder_append(text, UNKNOWN);
        return;
    }
    if (voltage->per_hour < 0) {
        string_builder_append_char(text, '-');
    }
    string_builder_append_unsigned(
        text, (uint32_t)(voltage->per_hour < 0 ? -voltage->per_hour : voltage->per_hour));
}

typedef struct SoakLevel {
    const char *name;
    uint8_t flag;
    uint8_t known;
} SoakLevel;

static void append_soak_edge(StringBuilder *text, const DebugSoakEdge *edge, const char *when)
{
    static const SoakLevel LEVELS[] = {
        {"cable", DEBUG_SOAK_CABLE, 0},
        {"charge_pin", DEBUG_SOAK_CHARGE_PIN, DEBUG_SOAK_CHARGE_KNOWN},
        {"supply_off", DEBUG_SOAK_SUPPLY_OFF, 0},
        {"audio", DEBUG_SOAK_AUDIO, 0},
        {"screen", DEBUG_SOAK_SCREEN, 0},
        {"clock_mhz", 0, 0}};
    char name[SOAK_NAME_CAPACITY];
    for (size_t index = 0; index < sizeof LEVELS / sizeof LEVELS[0]; index++) {
        const SoakLevel *level = &LEVELS[index];
        StringBuilder builder = string_builder_make(name, sizeof name);
        string_builder_append(&builder, level->name);
        string_builder_append(&builder, when);
        bool known = level->known == 0 || (edge->levels & level->known) != 0;
        append_field(text, name, known,
                     level->flag == 0 ? edge->clock_mhz : (edge->levels & level->flag) != 0);
    }
}

static void put_soak(Writer *writer, const DebugSoak *soak, const PowerHistory *history)
{
    SoakVoltage voltage = soak_voltage(history, soak);
    uint32_t lasted_ms = soak->ended_ms - soak->started_ms;
    begin(writer, "script.soak");
    string_builder_append(&writer->text, "line=");
    string_builder_append_unsigned(&writer->text, soak->line);
    append_field(&writer->text, "minutes", true, lasted_ms / MILLISECONDS_PER_MINUTE);
    string_builder_append(&writer->text, " end=");
    string_builder_append(&writer->text, NAME_IN(SOAK_ENDS, soak->end));
    append_field(&writer->text, "samples", true, voltage.samples);
    append_field(&writer->text, "first_mv", voltage.known, voltage.first_millivolts);
    append_field(&writer->text, "last_mv", voltage.known, voltage.last_millivolts);
    append_rate(&writer->text, &voltage);
    append_share(&writer->text, "busy_percent", lasted_ms - soak->idle_ms, lasted_ms);
    append_soak_edge(&writer->text, &soak->start, "_start");
    append_soak_edge(&writer->text, &soak->finish, "_end");
    finish(writer);
}

static DebugSpan line_span(const DebugScriptRun *script, uint32_t wanted)
{
    size_t offset = 0;
    DebugSpan span = {script->text, 0};
    for (uint32_t line = 1; line <= wanted && offset < script->length; line++) {
        span = debug_script_next_line(script->text, script->length, &offset);
    }
    return span;
}

static void put_screenshot(Writer *writer, const DebugScriptRun *script, uint32_t slot)
{
    begin(writer, "script.screenshot");
    string_builder_append_unsigned(&writer->text, slot);
    string_builder_append_char(&writer->text, ' ');
    append_plain(&writer->text,
                 debug_script_parse(line_span(script, script->screenshot_lines[slot])).argument);
    finish(writer);
}

static void put_script_results(Writer *writer, const DebugScriptRun *script,
                               const PowerHistory *history)
{
    for (uint32_t index = 0; index < script->soak_count; index++) {
        put_soak(writer, &script->soaks[index], history);
    }
    for (uint32_t index = 0; index < script->measure_count; index++) {
        put_measure(writer, &script->measures[index]);
    }
    for (uint32_t slot = 0; slot < script->screenshot_count; slot++) {
        put_screenshot(writer, script, slot);
    }
    for (uint32_t index = 0; index < script->probe_count; index++) {
        put_probe(writer, &script->probes[index]);
    }
    for (uint32_t index = 0; index < script->rtc_count; index++) {
        put_rtc(writer, &script->rtc[index]);
    }
}

static void put_script(Writer *writer, const DebugReportInput *input)
{
    const DebugScriptRun *script = input->script;
    if (script == NULL) {
        put_text(writer, "script.state", UNKNOWN);
        return;
    }
    bool started = script->state >= DEBUG_SCRIPT_RUNNING && script->state <= DEBUG_SCRIPT_ABORTED;
    put_text(writer, "script.state", NAME_IN(SCRIPT_STATES, script->state));
    put_known(writer, "script.started_ms", started, script->started_ms);
    put_known(writer, "script.finished_ms", started && script->state != DEBUG_SCRIPT_RUNNING,
              script->finished_ms);
    put_unsigned(writer, "script.interrupted_line", input->interrupted_line);
    if (script->state == DEBUG_SCRIPT_NONE) {
        return;
    }
    put_script_lines(writer, script);
    put_script_results(writer, script, input->power.history);
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
        string_builder_append(text, NAME_IN(GESTURE_NAMES, (value >> GESTURE_SHIFT) & 0xFFu));
        string_builder_append_char(text, ' ');
        string_builder_append_unsigned(text, value >> 16);
    } else if (event->kind == DEBUG_EVENT_PLAYER) {
        append_named_pair(text, NAME_IN(PLAYER_NAMES, value & STATE_MASK), value >> TRACK_SHIFT);
    } else if (event->kind == DEBUG_EVENT_USB_END) {
        string_builder_append(text, NAME_IN(USB_END_NAMES, value));
    } else if (event->kind == DEBUG_EVENT_SCRIPT && value >= DEBUG_SCRIPT_EVENT_STATE) {
        string_builder_append(text, NAME_IN(SCRIPT_STATES, value - DEBUG_SCRIPT_EVENT_STATE));
    } else if (event->kind == DEBUG_EVENT_SCAN) {
        append_named_pair(text, NAME_IN(SCAN_PHASES, value >> DEBUG_SCAN_PHASE_SHIFT),
                          value & DEBUG_SCAN_NUMBER_MASK);
    } else if (event->kind == DEBUG_EVENT_COVER) {
        append_named_pair(text, (value & DEBUG_COVER_FULL) != 0 ? "full" : "half",
                          value & DEBUG_COVER_ALBUM_MASK);
        string_builder_append(text, (value & DEBUG_COVER_FOUND) != 0 ? " found" : " missing");
    } else if (event->kind == DEBUG_EVENT_USB_CONNECT) {
        string_builder_append(text, NAME_IN(CONNECT_NAMES, value));
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

static void put_boot(Writer *writer, const uint32_t *times)
{
    static const char *const KEYS[DEBUG_BOOT_COUNT] = {
        "boot.clock_us", "boot.display_us",   "boot.glyphs_us",      "boot.app_us",
        "boot.frame_us", "boot.extension_us", "boot.interactive_us", "boot.audio_us"};
    for (int index = 0; index < DEBUG_BOOT_COUNT; index++) {
        uint32_t value = times != NULL ? times[index] : 0;
        put_known(writer, KEYS[index], value != 0, value - 1);
    }
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
    put_boot(&writer, input->boot_us);
    put_usb(&writer, input->usb, input->power.usb_kept_on);
    put_usb_words(&writer, &input->usb_words);
    put_loop(&writer, input);
    put_card(&writer, &input->card);
    put_audio(&writer, &input->audio);
    put_bluetooth(&writer, input->bluetooth);
    put_unsigned(&writer, "dac.up", input->dac.up);
    put_known(&writer, "dac.power_left", (input->dac.valid & 1) != 0, input->dac.power[0]);
    put_known(&writer, "dac.power_right", (input->dac.valid & 2) != 0, input->dac.power[1]);
    put_unsigned(&writer, "dac.start_us", input->dac.start_us);
    put_unsigned(&writer, "dac.stop_us", input->dac.stop_us);
    put_unsigned(&writer, "dac.gain_us", input->dac.gain_us);
    put_frames(&writer, &input->frames);
    put_script(&writer, input);
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
