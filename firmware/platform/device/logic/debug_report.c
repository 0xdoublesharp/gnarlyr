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
    U64_DECIMAL_DIGITS = 20,
    KEY_MASK = 0xFF,
    LADDER_PRESSED = 1u << 8,
    LADDER_CHORD = 1u << 9,
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
    const char *prefix;
    size_t line_start;
    bool truncated;
} Writer;

static const char UNKNOWN[] = "unknown";
enum {
    EVENT_NAMES_COUNT = DEBUG_EVENT_KIND_COUNT
};
static const char EVENT_NAMES[] = "key\0"
                                  "screen\0"
                                  "player\0"
                                  "underrun\0"
                                  "output_underrun\0"
                                  "refusal\0"
                                  "storage\0"
                                  "usb_start\0"
                                  "usb_end\0"
                                  "script\0"
                                  "power\0"
                                  "usb_connect\0"
                                  "scan\0"
                                  "cover\0"
                                  "display_fallback\0"
                                  "ladder";
enum {
    SCAN_PHASES_COUNT = DEBUG_SCAN_PHASE_COUNT
};
static const char SCAN_PHASES[] = "start\0"
                                  "library\0"
                                  "covers\0"
                                  "covers_done\0"
                                  "covers_stopped\0"
                                  "up_to_date\0"
                                  "emmc_ok\0"
                                  "emmc_failed\0"
                                  "emmc_flag_lost";
enum {
    SCRIPT_STATES_COUNT = DEBUG_SCRIPT_STATE_COUNT
};
static const char SCRIPT_STATES[] = "none\0"
                                    "pending\0"
                                    "running\0"
                                    "finished\0"
                                    "timed_out\0"
                                    "aborted\0"
                                    "refused_too_long\0"
                                    "refused_restart_pending\0"
                                    "refused_no_build\0"
                                    "refused_build";
enum {
    LINE_STATUSES_COUNT = DEBUG_LINE_STATUS_COUNT
};
static const char LINE_STATUSES[] = "not_run\0"
                                    "ok\0"
                                    "unknown\0"
                                    "bad_argument\0"
                                    "failed\0"
                                    "refused\0"
                                    "cut_short";
enum {
    SOAK_ENDS_COUNT = DEBUG_SOAK_END_COUNT
};
static const char SOAK_ENDS[] = "running\0"
                                "done\0"
                                "key\0"
                                "low_battery\0"
                                "stopped";
enum {
    REASON_NAMES_COUNT = DEBUG_REPORT_REASON_COUNT
};
static const char REASON_NAMES[] = "none\0"
                                   "usb_session\0"
                                   "shutdown\0"
                                   "auto_off\0"
                                   "sleep_timer\0"
                                   "low_battery\0"
                                   "unplugged\0"
                                   "restart\0"
                                   "no_host";
enum {
    RTC_STATUSES_COUNT = HAL_RTC_STATUS_COUNT
};
static const char RTC_STATUSES[] = "ok\0"
                                   "no_ack\0"
                                   "timeout\0"
                                   "no_time\0"
                                   "mismatch";
enum {
    RTC_ACTIONS_COUNT = 2
};
static const char RTC_ACTIONS[] = "read\0"
                                  "set";
enum {
    CONNECT_NAMES_COUNT = 5
};
static const char CONNECT_NAMES[] = "none\0"
                                    "waiting\0"
                                    "answered\0"
                                    "no_host\0"
                                    "cancelled";
static const char SCHEMA_KEY[] = "debug_report";
static const char SEQUENCE_KEY[] = "report.sequence";
static const char COLD_KEY[] = "report.cold_start";
enum {
    GESTURE_NAMES_COUNT = 3
};
static const char GESTURE_NAMES[] = "press\0"
                                    "long\0"
                                    "repeat";
enum {
    PLAYER_NAMES_COUNT = 4
};
static const char PLAYER_NAMES[] = "idle\0"
                                   "playing\0"
                                   "paused\0"
                                   "standby";
_Static_assert(PLAYER_NAMES_COUNT == DEBUG_PLAYER_STANDBY + 1,
               "the report names the standby state the event log records");
enum {
    AUDIO_NAMES_COUNT = 3
};
static const char AUDIO_NAMES[] = "stopped\0"
                                  "playing\0"
                                  "paused";
enum {
    USB_END_NAMES_COUNT = 4
};
static const char USB_END_NAMES[] = "none\0"
                                    "ejected\0"
                                    "unplugged\0"
                                    "no_host";
enum {
    SPEED_FALLBACK_NAMES_COUNT = 4
};
static const char SPEED_FALLBACK_NAMES[] = "none\0"
                                           "not_configured\0"
                                           "few_commands\0"
                                           "resets";
enum {
    STEP_NAMES_COUNT = HAL_START_STEP_COUNT
};
static const char STEP_NAMES[] = "none\0"
                                 "backlight_on\0"
                                 "backlight_off\0"
                                 "clock_switch\0"
                                 "core1_start\0"
                                 "core1_stop\0"
                                 "card_write\0"
                                 "watchdog_test\0"
                                 "loader";
static const char *const PART_NAMES[DEBUG_FRAME_PARTS] = {"part.covers_us", "part.reflections_us",
                                                          "part.text_us",   "part.backdrop_us",
                                                          "part.hash_us",   "part.audio_us"};

static const char *next_key(const char *key)
{
    while (*key++ != '\0') {
    }
    return key;
}

static const char *indexed_name(const char *names, size_t count, size_t index)
{
    if (index >= count) {
        return UNKNOWN;
    }
    while (index-- != 0) {
        names = next_key(names);
    }
    return names;
}

#define NAME_IN(names, index) indexed_name(names, names##_COUNT, index)

static void begin(Writer *writer, const char *key)
{
    writer->line_start = writer->text.length;
    if (writer->prefix != NULL) {
        string_builder_append(&writer->text, writer->prefix);
    }
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

static void append_u64(StringBuilder *text, uint64_t value)
{
    char digits[U64_DECIMAL_DIGITS];
    int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    while (count > 0) {
        string_builder_append_char(text, digits[--count]);
    }
}

static void put_u64(Writer *writer, const char *key, uint64_t value)
{
    begin(writer, key);
    append_u64(&writer->text, value);
    finish(writer);
}

static void put_known_u64(Writer *writer, const char *key, bool known, uint64_t value)
{
    if (known) {
        put_u64(writer, key, value);
    } else {
        put_text(writer, key, UNKNOWN);
    }
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

static void put_share_u64(Writer *writer, const char *key, uint64_t part, uint64_t whole)
{
    bool known = whole >= PERCENT && part <= whole;
    put_known(writer, key, known, known ? (uint32_t)(part / (whole / PERCENT)) : 0);
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
    writer->prefix = "standby.";
    put_unsigned(writer, "entries", standby->entries);
    put_unsigned(writer, "wakes", standby->wakes);
    if (standby->entries != 0) {
        put_known(writer, "wake_latency_ms", standby->wakes != 0 && !standby->waking,
                  standby->latency_ms);
        put_unsigned(writer, "entry_ms", standby->entry_ms);
        put_unsigned(writer, "wake_ms", standby->wake_ms);
        put_known(writer, "entry_mv", standby->entry_mv != 0, standby->entry_mv);
        put_known(writer, "wake_mv", standby->wake_mv != 0, standby->wake_mv);
    }
    writer->prefix = NULL;
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
    put_u64(writer, "core0.clock_us", input->clock_us);
    put_u64(writer, "core0.idle_us", input->idle_us);
    bool valid = input->clock_us >= input->idle_us;
    put_share_u64(writer, "core0.busy_percent", valid ? input->clock_us - input->idle_us : 0,
                  valid ? input->clock_us : 0);
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

static void put_known_list(Writer *writer, const char *const *keys, const void *first, size_t count,
                           bool known)
{
    const uint8_t *bytes = first;
    for (size_t index = 0; index < count; index++) {
        put_known(writer, keys[index], known, *(const uint32_t *)(const void *)bytes);
        bytes += sizeof(uint32_t);
    }
}

static void put_word_list(Writer *writer, const char *const *keys, const void *first, size_t count)
{
    put_known_list(writer, keys, first, count, true);
}

enum {
    REPORT_OFFSET_MASK = 63,
    REPORT_SIZE_SHIFT = 6,
    REPORT_HALFWORD = 1,
    REPORT_BYTE = 2
};

_Static_assert(sizeof(bool) == sizeof(uint8_t), "report descriptors read bools as bytes");

#define REPORT_OFFSET(type, member)                                                                \
    (offsetof(type, member) +                                                                      \
     0 * sizeof(char[offsetof(type, member) <= REPORT_OFFSET_MASK ? 1 : -1]))
#define REPORT_SIZE(type, member)                                                                  \
    _Generic(((type *)0)->member,                                                                  \
        bool: REPORT_BYTE,                                                                         \
        uint8_t: REPORT_BYTE,                                                                      \
        uint16_t: REPORT_HALFWORD,                                                                 \
        uint32_t: 0)
#define REPORT_NUMBER(type, member)                                                                \
    (REPORT_OFFSET(type, member) | (REPORT_SIZE(type, member) << REPORT_SIZE_SHIFT))
#define FIELD_COUNT(fields) (sizeof(fields) / sizeof((fields)[0]))

static uint32_t report_number(const void *data, uint8_t field)
{
    const uint8_t *bytes = (const uint8_t *)data + (field & REPORT_OFFSET_MASK);
    if (field >= (REPORT_BYTE << REPORT_SIZE_SHIFT)) {
        return *bytes;
    }
    if (field >= (REPORT_HALFWORD << REPORT_SIZE_SHIFT)) {
        return *(const uint16_t *)(const void *)bytes;
    }
    return *(const uint32_t *)(const void *)bytes;
}

typedef struct ReportNumbers {
    const char *keys;
    const uint8_t *offsets;
    size_t count;
} ReportNumbers;

#define REPORT_NUMBERS(keys, offsets) ((ReportNumbers){(keys), (offsets), sizeof(offsets)})

static void put_numbers(Writer *writer, ReportNumbers fields, const void *data, bool known)
{
    const char *key = fields.keys;
    for (size_t index = 0; index < fields.count; index++) {
        put_known(writer, key, known, report_number(data, fields.offsets[index]));
        key = next_key(key);
    }
}

static void put_power(Writer *writer, const DebugReportInput *input)
{
    static const char *const GAUGE_KEYS[] = {
        "gauge_millivolts", "percent_plain", "percent_gauge", "percent_shown",
        "offset_applied",   "capped",        "held",
    };
    const DebugReportPower *power = &input->power;
    bool gauged = power->gauge_readings != 0;
    bool charging = input->battery.charging;
    writer->prefix = "power.";
    static const char FLAGS_KEYS[] = "vbus\0usb_powered";
    static const uint8_t FLAGS[] = {
        REPORT_NUMBER(DebugReportPower, vbus),
        REPORT_NUMBER(DebugReportPower, usb_powered),
    };
    put_numbers(writer, REPORT_NUMBERS(FLAGS_KEYS, FLAGS), power, true);
    put_known(writer, "charge_pin", power->pin_known, power->pin_high);
    put_text(writer, "adc_source", reading_source(power->adc_from_gauge, power->adc_known));
    put_known(writer, "adc_raw", power->adc_known, power->adc_raw);
    put_known(writer, "adc_millivolts", power->adc_known, battery_millivolts(power->adc_raw));
    put_unsigned(writer, "gauge_readings", power->gauge_readings);
    const uint32_t values[] = {
        power->gauge_millivolts,
        power->percent_plain,
        power->percent_gauge,
        power->percent_shown,
        charging,
        charging && power->percent_gauge >= BATTERY_CHARGING_MAX_PERCENT,
        power->percent_shown != power->percent_gauge,
    };
    put_known_list(writer, GAUGE_KEYS, values, 3, gauged);
    put_known(writer, GAUGE_KEYS[3], power->shown_known, values[3]);
    put_known_list(writer, GAUGE_KEYS + 4, values + 4, 2, gauged);
    put_known(writer, GAUGE_KEYS[6], gauged && power->shown_known, values[6]);
    writer->prefix = NULL;
}

static void append_sample(StringBuilder *text, PowerSample sample)
{
    string_builder_append_unsigned(text, sample.minute);
    string_builder_append_char(text, ' ');
    string_builder_append_unsigned(text, sample.millivolts);
    string_builder_append_char(text, ' ');
    string_builder_append_unsigned(text, sample.percent);
    string_builder_append_char(text, ' ');
    _Static_assert(POWER_SAMPLE_VBUS == 1 && POWER_SAMPLE_CHARGE_PIN == 2 &&
                       POWER_SAMPLE_PLAYING == 4 && POWER_SAMPLE_USB_POWERED == 8 &&
                       POWER_SAMPLE_USB_KEPT_ON == 16,
                   "power samples report flags from low to high");
    for (unsigned flag = 1; flag <= POWER_SAMPLE_USB_KEPT_ON; flag <<= 1) {
        string_builder_append_char(text, (sample.flags & flag) != 0 ? '1' : '0');
    }
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
    writer->prefix = "start.";
    static const char COUNTS_KEYS[] = "count\0warm\0previous_uptime_ms\0previous_power_off";
    static const uint8_t COUNTS[] = {
        REPORT_NUMBER(HalStartInfo, starts),
        REPORT_NUMBER(HalStartInfo, warm),
        REPORT_NUMBER(HalStartInfo, previous_uptime_ms),
        REPORT_NUMBER(HalStartInfo, previous_power_off),
    };
    put_numbers(writer, (ReportNumbers){COUNTS_KEYS, COUNTS, 2}, start, true);
    put_numbers(writer, (ReportNumbers){COUNTS_KEYS + sizeof "count\0warm", COUNTS + 2, 2}, start,
                warm);
    put_text(writer, "last_step", warm ? NAME_IN(STEP_NAMES, start->last_step) : UNKNOWN);
    static const char STEPS_KEYS[] = "last_step_finished\0last_step_ms";
    static const uint8_t STEPS[] = {
        REPORT_NUMBER(HalStartInfo, last_step_finished),
        REPORT_NUMBER(HalStartInfo, last_step_ms),
    };
    put_numbers(writer, REPORT_NUMBERS(STEPS_KEYS, STEPS), start, warm);
    put_known(writer, "last_millivolts", warm && start->last_millivolts != 0,
              start->last_millivolts);
    writer->prefix = "fault.";
    static const char FAULTS_KEYS[] = "cfsr\0hfsr\0pc";
    static const uint8_t FAULTS[] = {
        REPORT_NUMBER(HalStartInfo, fault_status),
        REPORT_NUMBER(HalStartInfo, hard_fault_status),
        REPORT_NUMBER(HalStartInfo, fault_pc),
    };
    const char *key = FAULTS_KEYS;
    for (size_t index = 0; index < FIELD_COUNT(FAULTS); index++) {
        put_address(writer, key, warm, report_number(start, FAULTS[index]));
        key = next_key(key);
    }
    writer->prefix = NULL;
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

static void append_word(StringBuilder *text, const char *name, uint32_t value)
{
    string_builder_append_char(text, ' ');
    string_builder_append(text, name);
    string_builder_append(text, "=0x");
    append_hex(text, value, HEX_DIGITS);
}

static void put_writes(Writer *writer, const HalUsbWrites *writes)
{
    static const char FIELDS_KEYS[] = "writes=\0 receive_ms=\0 card_ms=\0 idle_ms=\0 fast=\0 "
                                      "fallbacks=\0 fifo_internal=\0 fifo_card=\0 ui_ms=";
    static const uint8_t FIELDS[] = {
        REPORT_NUMBER(HalUsbWrites, commands),      REPORT_NUMBER(HalUsbWrites, receive_ms),
        REPORT_NUMBER(HalUsbWrites, card_ms),       REPORT_NUMBER(HalUsbWrites, idle_ms),
        REPORT_NUMBER(HalUsbWrites, fast_cards),    REPORT_NUMBER(HalUsbWrites, fallbacks),
        REPORT_NUMBER(HalUsbWrites, fifo_words[0]), REPORT_NUMBER(HalUsbWrites, fifo_words[1]),
        REPORT_NUMBER(HalUsbWrites, ui_ms),
    };
    begin(writer, "usb.timing");
    const char *key = FIELDS_KEYS;
    for (size_t index = 0; index < FIELD_COUNT(FIELDS); index++) {
        string_builder_append(&writer->text, key);
        string_builder_append_unsigned(&writer->text, report_number(writes, FIELDS[index]));
        key = next_key(key);
    }
    append_word(&writer->text, "hcon_internal", writes->hcon[0]);
    append_word(&writer->text, "hcon_card", writes->hcon[1]);
    finish(writer);
}

static void put_usb_dac(Writer *writer, const HalUsbDacStats *dac)
{
    static const char FIELDS_KEYS[] =
        "rate\0bits\0high_speed\0packets\0underruns\0overflows\0realigned\0max_gap_us";
    static const uint8_t FIELDS[] = {
        REPORT_NUMBER(HalUsbDacStats, rate),       REPORT_NUMBER(HalUsbDacStats, bits),
        REPORT_NUMBER(HalUsbDacStats, high_speed), REPORT_NUMBER(HalUsbDacStats, packets),
        REPORT_NUMBER(HalUsbDacStats, underruns),  REPORT_NUMBER(HalUsbDacStats, overflows),
        REPORT_NUMBER(HalUsbDacStats, realigned),  REPORT_NUMBER(HalUsbDacStats, max_gap_us),
    };
    writer->prefix = "usb.dac.";
    put_numbers(writer, REPORT_NUMBERS(FIELDS_KEYS, FIELDS), dac, true);
    put_address(writer, "max_gap_pc", dac->max_gap_pc != 0, dac->max_gap_pc);
    writer->prefix = NULL;
}

static void put_usb(Writer *writer, const HalUsbLast *usb, bool kept_on)
{
    static const char FIELDS_KEYS[] =
        "left_mounted\0internal_sectors_written\0card_sectors_written\0writes_after_eject";
    static const uint8_t FIELDS[] = {
        REPORT_NUMBER(HalUsbLast, left_mounted),
        REPORT_NUMBER(HalUsbLast, internal_written),
        REPORT_NUMBER(HalUsbLast, card_written),
        REPORT_NUMBER(HalUsbLast, writes_after_eject),
    };
    bool known = usb->end != HAL_USB_END_NONE;
    writer->prefix = "usb.";
    put_text(writer, "sense_mode", kept_on ? "kept_on" : "normal");
    put_text(writer, "last_end", NAME_IN(USB_END_NAMES, usb->end));
    put_numbers(writer, REPORT_NUMBERS(FIELDS_KEYS, FIELDS), usb, known);
    writer->prefix = NULL;
    if (known) {
        put_writes(writer, &usb->writes);
    }
    if (usb->dac.active) {
        put_usb_dac(writer, &usb->dac);
    }
    if (usb->speed_fallback != 0) {
        begin(writer, "usb.speed_fallback");
        string_builder_append(&writer->text, NAME_IN(SPEED_FALLBACK_NAMES, usb->speed_fallback));
        string_builder_append(&writer->text, " commands=");
        string_builder_append_unsigned(&writer->text, usb->fallback_commands);
        put_setup_trace(&writer->text, &usb->fallback_setups);
        finish(writer);
    }
}

static void put_usb_words(Writer *writer, const DebugReportUsbWords *words)
{
    static const char *const START_KEYS[DEBUG_REPORT_GRF_WORDS] = {
        "grf_e8_start", "grf_84_start", "grf_b4_start", "grf_b8_start", "grf_98_start"};
    static const char *const KEYS[DEBUG_REPORT_GRF_WORDS] = {"grf_e8", "grf_84", "grf_b4", "grf_b8",
                                                             "grf_98"};
    writer->prefix = "usb.";
    for (uint32_t index = 0; index < DEBUG_REPORT_GRF_WORDS; index++) {
        put_address(writer, START_KEYS[index], words->grf_start != NULL,
                    words->grf_start != NULL ? words->grf_start[index] : 0);
    }
    for (uint32_t index = 0; index < DEBUG_REPORT_GRF_WORDS; index++) {
        put_address(writer, KEYS[index], true, words->grf[index]);
    }
    put_address(writer, "gotgctl", words->core_known, words->gotgctl);
    put_known(writer, "b_session", words->core_known,
              (words->gotgctl & DEBUG_REPORT_B_SESSION_VALID) != 0);
    writer->prefix = NULL;
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
    static const char *const KEYS[] = {"connect_attempts", "connect_answered",
                                       "connect_unanswered"};
    const DebugReportLoop *loop = &input->loop;
    _Static_assert(offsetof(DebugReportLoop, unanswered) - offsetof(DebugReportLoop, attempts) ==
                       (FIELD_COUNT(KEYS) - 1) * sizeof(uint32_t),
                   "the loop report reads the counters in member order");
    writer->prefix = "loop.";
    put_text(writer, "boot", boot_kind(input));
    put_word_list(writer, KEYS, (const uint8_t *)loop + offsetof(DebugReportLoop, attempts),
                  FIELD_COUNT(KEYS));
    put_text(writer, "last_connect", NAME_IN(CONNECT_NAMES, loop->last_connect));
    put_known(writer, "cable_bit_at_connect", loop->attempts != 0, loop->cable_at_connect);
    writer->prefix = NULL;
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
    static const char *const FIELDS[] = {"raw",       " present", " ejected", " ready",
                                         " detected", " powered", " tries",   " ms"};
    bool clean = card->ready && card->failures == 0 && card->attempts <= 1;
    if (card->attempts == 0 || clean) {
        return;
    }
    const uint32_t values[] = {card->raw_now,  card->present_now, card->ejected_now,
                               card->ready,    card->detected,    card->powered_before,
                               card->attempts, card->init_ms};
    begin(writer, "card.start");
    StringBuilder *text = &writer->text;
    for (uint32_t index = 0; index < sizeof values / sizeof values[0]; index++) {
        append_card_field(text, FIELDS[index], values[index]);
    }
    append_card_command(text, " last", card->last_command, card->last_response);
    append_card_command(text, " failed", card->failed_command, card->failed_interrupts);
    append_card_field(text, " errors", card->failures);
    append_card_field(text, " timeouts", card->timeouts);
    finish(writer);
}

static void put_bluetooth(Writer *writer, const HalBluetoothLink *link)
{
    static const char *const KEYS[] = {
        "module_on_ms", "init_done_ms", "connect_ms", "connected_ms", "stream_ms", "first_audio_ms",
    };
    if (link == NULL) {
        return;
    }
    _Static_assert(sizeof(HalBluetoothTimeline) == sizeof KEYS / sizeof KEYS[0] * sizeof(uint32_t),
                   "the Bluetooth report reads the timeline in member order");
    writer->prefix = "bluetooth.";
    put_word_list(writer, KEYS, &link->timeline, sizeof KEYS / sizeof KEYS[0]);
    begin(writer, "reports");
    for (uint32_t index = 0; index < link->event_count; index++) {
        string_builder_append_char(&writer->text, link->events[index][0]);
        string_builder_append_char(&writer->text, link->events[index][1]);
        string_builder_append_char(&writer->text, ' ');
    }
    finish(writer);
    writer->prefix = NULL;
}

enum {
    AUDIO_ALWAYS,
    AUDIO_RUNNING,
    AUDIO_DECODED,
    AUDIO_AVERAGE
};

#define AUDIO_FIELD(member, mode)                                                                  \
    (REPORT_OFFSET(HalAudioStats, member) | (mode) << REPORT_SIZE_SHIFT)

static void put_audio(Writer *writer, const DebugReportAudio *audio)
{
    static const char KEYS[] = "buffered_ms\0"
                               "underruns\0"
                               "output_underruns\0"
                               "blocks\0"
                               "handoff_avg_us\0"
                               "handoff_max_us\0"
                               "read_avg_us\0"
                               "read_max_us\0"
                               "busy_us\0"
                               "elapsed_us";
    static const uint8_t FIELDS[] = {
        AUDIO_FIELD(buffered_ms, AUDIO_RUNNING),     AUDIO_FIELD(underruns, AUDIO_RUNNING),
        AUDIO_FIELD(output_underruns, AUDIO_ALWAYS), AUDIO_FIELD(blocks, AUDIO_ALWAYS),
        AUDIO_FIELD(decode_total_us, AUDIO_AVERAGE), AUDIO_FIELD(decode_max_us, AUDIO_DECODED),
        AUDIO_FIELD(read_total_us, AUDIO_AVERAGE),   AUDIO_FIELD(read_max_us, AUDIO_DECODED),
        AUDIO_FIELD(busy_us, AUDIO_ALWAYS),          AUDIO_FIELD(elapsed_us, AUDIO_RUNNING),
    };
    const HalAudioStats *stats = &audio->stats;
    const uint8_t *bytes = (const uint8_t *)stats;
    writer->prefix = "audio.";
    put_text(writer, "state", NAME_IN(AUDIO_NAMES, audio->state));
    put_unsigned(writer, "running", audio->running);
    const char *key = KEYS;
    for (size_t index = 0; index < sizeof FIELDS; index++) {
        uint8_t kind = FIELDS[index] >> REPORT_SIZE_SHIFT;
        uint32_t value =
            *(const uint32_t *)(const void *)(bytes + (FIELDS[index] & REPORT_OFFSET_MASK));
        bool decoded = stats->blocks != 0;
        bool known = kind == AUDIO_ALWAYS || (kind == AUDIO_RUNNING ? audio->running : decoded);
        if (kind == AUDIO_AVERAGE && decoded) {
            value /= stats->blocks;
        }
        put_known(writer, key, known, value);
        key = next_key(key);
    }
    put_share(writer, "busy_percent", stats->busy_us, audio->running ? stats->elapsed_us : 0);
    writer->prefix = NULL;
}

static void put_frames(Writer *writer, const DebugFrames *frames)
{
    static const char *const KEYS[] = {"total_avg_us", "total_worst_us", "update_avg_us",
                                       "draw_avg_us", "send_avg_us"};
    bool drawn = frames->count != 0;
    writer->prefix = "frame.";
    put_unsigned(writer, "count", frames->count);
    _Static_assert(offsetof(DebugFrames, send_us) - offsetof(DebugFrames, total_us) ==
                       (FIELD_COUNT(KEYS) - 1) * sizeof(uint32_t),
                   "the frame report reads the counters in member order");
    put_known_list(writer, KEYS, (const uint8_t *)frames + offsetof(DebugFrames, total_us),
                   FIELD_COUNT(KEYS), drawn);
    put_known_list(writer, PART_NAMES, frames->part_us, DEBUG_FRAME_PARTS, drawn);
    writer->prefix = NULL;
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

enum {
    MEASURE_ALWAYS,
    MEASURE_FRAMES,
    MEASURE_RUNNING,
    MEASURE_BLOCKS,
    MEASURE_AVERAGE = 4
};

#define MEASURE_FIELD(member, mode) {offsetof(DebugMeasure, member), mode}

static void append_measured_fields(StringBuilder *text, const DebugMeasure *measure)
{
    static const char KEYS[] = "ms\0"
                               "frames\0"
                               "frame_avg_us\0"
                               "frame_worst_us\0"
                               "update_avg_us\0"
                               "draw_avg_us\0"
                               "send_avg_us\0"
                               "audio\0"
                               "buffered_ms\0"
                               "underruns\0"
                               "output_underruns\0"
                               "blocks\0"
                               "handoff_avg_us\0"
                               "handoff_max_us\0"
                               "read_avg_us\0"
                               "read_max_us";
    static const uint8_t FIELDS[][2] = {
        MEASURE_FIELD(duration_ms, MEASURE_ALWAYS),
        MEASURE_FIELD(frames, MEASURE_ALWAYS),
        MEASURE_FIELD(frame_us, MEASURE_FRAMES | MEASURE_AVERAGE),
        MEASURE_FIELD(frame_worst_us, MEASURE_FRAMES),
        MEASURE_FIELD(update_us, MEASURE_FRAMES | MEASURE_AVERAGE),
        MEASURE_FIELD(draw_us, MEASURE_FRAMES | MEASURE_AVERAGE),
        MEASURE_FIELD(send_us, MEASURE_FRAMES | MEASURE_AVERAGE),
        MEASURE_FIELD(audio_running, MEASURE_ALWAYS),
        MEASURE_FIELD(buffered_ms, MEASURE_RUNNING),
        MEASURE_FIELD(underruns, MEASURE_RUNNING),
        MEASURE_FIELD(output_underruns, MEASURE_ALWAYS),
        MEASURE_FIELD(blocks, MEASURE_ALWAYS),
        MEASURE_FIELD(handoff_us, MEASURE_BLOCKS | MEASURE_AVERAGE),
        MEASURE_FIELD(handoff_max_us, MEASURE_BLOCKS),
        MEASURE_FIELD(read_us, MEASURE_BLOCKS | MEASURE_AVERAGE),
        MEASURE_FIELD(read_max_us, MEASURE_BLOCKS),
    };
    const uint32_t counts[] = {1, measure->frames, measure->audio_running, measure->blocks};
    const uint8_t *bytes = (const uint8_t *)measure;
    const char *key = KEYS;
    for (size_t index = 0; index < FIELD_COUNT(FIELDS); index++) {
        uint8_t mode = FIELDS[index][1];
        uint32_t count = counts[mode & (MEASURE_AVERAGE - 1)];
        uint32_t value = *(const uint32_t *)(const void *)(bytes + FIELDS[index][0]);
        if ((mode & MEASURE_AVERAGE) != 0 && count != 0) {
            value /= count;
        }
        append_field(text, key, count != 0, value);
        key = next_key(key);
    }
}

static void begin_script_line(Writer *writer, const char *name, uint32_t line)
{
    begin(writer, name);
    string_builder_append(&writer->text, "line=");
    string_builder_append_unsigned(&writer->text, line);
}

static void put_measure(Writer *writer, const DebugMeasure *measure)
{
    begin_script_line(writer, "script.measure", measure->line);
    append_measured_fields(&writer->text, measure);
    append_share(&writer->text, "audio_busy_percent", measure->audio_busy_us,
                 measure->audio_running != 0 ? measure->audio_elapsed_us : 0);
    append_share(&writer->text, "core0_busy_percent", measure->clock_us - measure->idle_us,
                 measure->clock_us);
    uint32_t core1_busy = measure->core1_cycles - measure->core1_idle_cycles;
    append_permille(&writer->text, "core1_busy_permille", core1_busy, measure->core1_cycles);
    finish(writer);
}

static void put_probe(Writer *writer, const DebugProbe *probe)
{
    begin_script_line(writer, "script.probe", probe->line);
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
    static const char SEPARATORS[] = "--T::";
    _Static_assert(offsetof(RtcTime, second) - offsetof(RtcTime, month) == sizeof SEPARATORS - 2,
                   "the RTC report reads the time fields in member order");
    const uint8_t *fields = (const uint8_t *)time + offsetof(RtcTime, month);
    for (size_t index = 0; index < sizeof SEPARATORS - 1; index++) {
        append_two_digits(text, SEPARATORS[index], fields[index]);
    }
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
    append_field(text, "vl", true, time.voltage_low ? 1u : 0u);
    if (valid) {
        append_rtc_time(text, &time);
    } else {
        string_builder_append(text, " time=invalid");
    }
    append_field(text, "weekday", true, time.weekday);
    append_field(text, "century", true, time.century ? 1u : 0u);
}

static void append_rtc_head(StringBuilder *text, const DebugRtcRead *read)
{
    append_field(text, "ms", true, read->time_ms);
    string_builder_append(text, " action=");
    string_builder_append(text, NAME_IN(RTC_ACTIONS, read->action));
    append_field(text, "written_s", read->written_s != 0, read->written_s);
    string_builder_append(text, " status=");
    string_builder_append(text, NAME_IN(RTC_STATUSES, read->status));
}

static void put_rtc(Writer *writer, const DebugRtcRead *read)
{
    begin_script_line(writer, "script.rtc", read->line);
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
    begin_script_line(writer, "script.soak", soak->line);
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

static void append_refusal(StringBuilder *text, uint32_t value)
{
    string_builder_append_unsigned(text, value & STATE_MASK);
    string_builder_append(text, " track ");
    string_builder_append_unsigned(text, value >> TRACK_SHIFT);
}

static void append_event_value(StringBuilder *text, const DebugEvent *event)
{
    uint32_t value = event->value;
    switch (event->kind) {
    case DEBUG_EVENT_KEY:
        string_builder_append(text, key_script_key_name((Key)(value & KEY_MASK)));
        string_builder_append_char(text, ' ');
        string_builder_append(text, NAME_IN(GESTURE_NAMES, (value >> GESTURE_SHIFT) & 0xFFu));
        string_builder_append_char(text, ' ');
        string_builder_append_unsigned(text, value >> 16);
        break;
    case DEBUG_EVENT_LADDER:
        string_builder_append(text, key_script_key_name((Key)(value & KEY_MASK)));
        string_builder_append(text, (value & LADDER_PRESSED) != 0 ? " press " : " release ");
        string_builder_append_unsigned(text, value >> 16);
        string_builder_append(text, (value & LADDER_CHORD) != 0 ? " chord" : "");
        break;
    case DEBUG_EVENT_PLAYER:
        append_named_pair(text, NAME_IN(PLAYER_NAMES, value & STATE_MASK), value >> TRACK_SHIFT);
        break;
    case DEBUG_EVENT_REFUSAL:
        append_refusal(text, value);
        break;
    case DEBUG_EVENT_USB_END:
        string_builder_append(text, NAME_IN(USB_END_NAMES, value));
        break;
    case DEBUG_EVENT_SCRIPT:
        if (value >= DEBUG_SCRIPT_EVENT_STATE) {
            string_builder_append(text, NAME_IN(SCRIPT_STATES, value - DEBUG_SCRIPT_EVENT_STATE));
        } else {
            string_builder_append_unsigned(text, value);
        }
        break;
    case DEBUG_EVENT_SCAN:
        append_named_pair(text, NAME_IN(SCAN_PHASES, value >> DEBUG_SCAN_PHASE_SHIFT),
                          value & DEBUG_SCAN_NUMBER_MASK);
        break;
    case DEBUG_EVENT_COVER:
        append_named_pair(text, (value & DEBUG_COVER_FULL) != 0 ? "full" : "half",
                          value & DEBUG_COVER_ALBUM_MASK);
        string_builder_append(text, (value & DEBUG_COVER_FOUND) != 0 ? " found" : " missing");
        break;
    case DEBUG_EVENT_USB_CONNECT:
        string_builder_append(text, NAME_IN(CONNECT_NAMES, value));
        break;
    case DEBUG_EVENT_POWER:
        append_named_pair(text, (value & POWER_SAMPLE_VBUS) != 0 ? "1" : "0",
                          (value & POWER_SAMPLE_CHARGE_PIN) != 0 ? 1 : 0);
        break;
    default:
        string_builder_append_unsigned(text, value);
        break;
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

static void put_refusals(Writer *writer, const DebugRefusals *refusals)
{
    if (refusals->count == 0) {
        return;
    }
    begin(writer, "refusals");
    string_builder_append_unsigned(&writer->text, refusals->count);
    string_builder_append(&writer->text, " last ");
    string_builder_append_unsigned(&writer->text, refusals->time_ms);
    string_builder_append_char(&writer->text, ' ');
    append_refusal(&writer->text, refusals->value);
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
    static const char *const KEYS[] = {"samples", "idle", "overlay", "other"};
    _Static_assert(offsetof(PcProfile, other_samples) - offsetof(PcProfile, samples) ==
                       (sizeof KEYS / sizeof KEYS[0] - 1) * sizeof(uint32_t),
                   "the profile report reads the counters in member order");
    put_text(writer, "profile.state", profile_state(profile));
    writer->prefix = "profile.";
    put_word_list(writer, KEYS, (const uint8_t *)profile + offsetof(PcProfile, samples),
                  sizeof KEYS / sizeof KEYS[0]);
    put_address(writer, "code_base", true, profile->code.base);
    put_unsigned(writer, "bucket_bytes", 1u << profile->bucket_shift);
    put_unsigned(writer, "buckets", profile->bucket_count);
    writer->prefix = NULL;
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

static void put_boot(Writer *writer, const uint64_t *times)
{
    static const char KEYS[] = "clock_us\0display_us\0glyphs_us\0app_us\0"
                               "frame_us\0extension_us\0interactive_us\0audio_us";
    const char *key = KEYS;
    writer->prefix = "boot.";
    for (int index = 0; index < DEBUG_BOOT_COUNT; index++) {
        uint64_t value = times != NULL ? times[index] : 0;
        put_known_u64(writer, key, value != 0, value - 1);
        key = next_key(key);
    }
    writer->prefix = NULL;
}

static void put_dac(Writer *writer, const DacDiagnostics *dac)
{
    static const char *const KEYS[] = {"start_us", "stop_us", "gain_us"};
    _Static_assert(offsetof(DacDiagnostics, gain_us) - offsetof(DacDiagnostics, start_us) ==
                       (FIELD_COUNT(KEYS) - 1) * sizeof(uint32_t),
                   "the DAC report reads the timings in member order");
    writer->prefix = "dac.";
    put_unsigned(writer, "up", dac->up);
    put_known(writer, "power_left", (dac->valid & 1) != 0, dac->power[0]);
    put_known(writer, "power_right", (dac->valid & 2) != 0, dac->power[1]);
    put_word_list(writer, KEYS, (const uint8_t *)dac + offsetof(DacDiagnostics, start_us),
                  FIELD_COUNT(KEYS));
    if (dac->overflow != 0) {
        put_unsigned(writer, "overflow", dac->overflow);
    }
    writer->prefix = NULL;
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
    put_usb(&writer, &input->usb, input->power.usb_kept_on);
    put_usb_words(&writer, &input->usb_words);
    put_loop(&writer, input);
    put_card(&writer, &input->card);
    put_audio(&writer, &input->audio);
    put_refusals(&writer, &input->refusals);
    put_bluetooth(&writer, input->bluetooth);
    put_dac(&writer, &input->dac);
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
