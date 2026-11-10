#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/key.h"
#include "core/key_tracker.h"
#include "device/debug_report.h"

enum {
    BUCKETS = 1229,
    GUARD_BYTES = 16
};

static char text[DEBUG_REPORT_BYTES + GUARD_BYTES + 1];
static uint16_t buckets[BUCKETS];
static DebugLog events;
static PcProfile profile;
static PowerHistory history;

static DebugReportInput plain_input(void)
{
    memset(buckets, 0, sizeof buckets);
    debug_log_reset(&events);
    history.recorded = 0;
    profile = (PcProfile){.buckets = buckets,
                          .bucket_count = BUCKETS,
                          .bucket_shift = 7,
                          .code = {0x03050000, 0x2666C}};
    return (DebugReportInput){.name = "Doublesharp Echo",
                              .version = "1.0.0-1051212",
                              .uptime_ms = 61000,
                              .clock_mhz = 200,
                              .clock_us = 61000000,
                              .idle_us = 45750000,
                              .power = {.history = &history},
                              .log = &events,
                              .profile = &profile};
}

static uint32_t format(const DebugReportInput *input, uint32_t capacity)
{
    memset(text, '#', sizeof text - 1);
    text[sizeof text - 1] = '\0';
    return debug_report_format(input, text, capacity);
}

static bool has_line(const char *line)
{
    char wanted[160];
    snprintf(wanted, sizeof wanted, "\n%s\n", line);
    return strstr(text, wanted) != NULL;
}

static void record(DebugEventKind kind, uint32_t value, const char *label)
{
    DebugEvent event = debug_event_make(1000 + events.recorded, kind, value, label);
    debug_log_record(&events, &event);
}

static void the_schema_version_is_the_first_line(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK_EQUAL(0, strncmp(text, "debug_report: 1\n", 16));
}

static void the_firmware_is_named_with_its_version_and_build_tag(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("firmware.name: Doublesharp Echo"));
    CHECK(has_line("firmware.version: 1.0.0"));
    CHECK(has_line("firmware.build: 1051212"));
    CHECK(has_line("uptime_ms: 61000"));
    CHECK(has_line("clock_mhz: 200"));
}

static void a_version_without_a_tag_reports_the_build_as_unknown(void)
{
    DebugReportInput input = plain_input();
    input.version = "1.0.0";
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("firmware.version: 1.0.0"));
    CHECK(has_line("firmware.build: unknown"));
}

static void core_0_busy_time_is_a_share_of_the_uptime(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("core0.idle_us: 45750000"));
    CHECK(has_line("core0.busy_percent: 25"));
}

static void a_wrapped_microsecond_clock_gives_no_busy_share(void)
{
    DebugReportInput input = plain_input();
    input.uptime_ms = 4300000;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("core0.busy_percent: unknown"));
}

static void an_unread_battery_is_unknown_not_zero(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("battery.millivolts: unknown"));
    CHECK(has_line("battery.percent: unknown"));
    input.battery = (HalBattery){.known = true, .percent = 71, .millivolts = 3987, .usb_power = 1};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("battery.millivolts: 3987"));
    CHECK(has_line("battery.percent: 71"));
    CHECK(has_line("battery.usb_power: 1"));
}

static DebugReportInput plugged_in_at_97_percent(void)
{
    DebugReportInput input = plain_input();
    input.battery = (HalBattery){.known = true, .percent = 97, .millivolts = 4180, .usb_power = 1};
    input.power = (DebugReportPower){.vbus = true,
                                     .pin_known = true,
                                     .pin_high = true,
                                     .adc_known = true,
                                     .adc_raw = 856,
                                     .gauge_readings = 64,
                                     .gauge_millivolts = 4176,
                                     .percent_plain = 97,
                                     .percent_gauge = 97,
                                     .shown_known = true,
                                     .percent_shown = 97,
                                     .history = &history};
    return input;
}

static void the_power_section_gives_the_raw_pin_the_adc_and_each_percent(void)
{
    DebugReportInput input = plugged_in_at_97_percent();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("power.vbus: 1"));
    CHECK(has_line("power.charge_pin: 1"));
    CHECK(has_line("power.adc_raw: 856"));
    CHECK(has_line("power.adc_millivolts: 4179"));
    CHECK(has_line("power.gauge_readings: 64"));
    CHECK(has_line("power.gauge_millivolts: 4176"));
    CHECK(has_line("power.percent_plain: 97"));
    CHECK(has_line("power.percent_gauge: 97"));
    CHECK(has_line("power.percent_shown: 97"));
    CHECK(has_line("power.offset_applied: 0"));
    CHECK(has_line("power.capped: 0"));
    CHECK(has_line("power.held: 0"));
}

static void a_charging_gauge_at_its_limit_is_reported_as_capped(void)
{
    DebugReportInput input = plugged_in_at_97_percent();
    input.battery.charging = true;
    input.power.pin_high = false;
    input.power.percent_plain = 100;
    input.power.percent_gauge = 99;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("power.charge_pin: 0"));
    CHECK(has_line("power.offset_applied: 1"));
    CHECK(has_line("power.capped: 1"));
    CHECK(has_line("power.held: 1"));
}

static void a_shown_percent_that_lags_the_gauge_is_reported_as_held(void)
{
    DebugReportInput input = plugged_in_at_97_percent();
    input.power.percent_gauge = 92;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("power.capped: 0"));
    CHECK(has_line("power.held: 1"));
}

static void power_values_not_read_yet_are_unknown(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("power.vbus: 0"));
    CHECK(has_line("power.charge_pin: unknown"));
    CHECK(has_line("power.adc_raw: unknown"));
    CHECK(has_line("power.adc_millivolts: unknown"));
    CHECK(has_line("power.gauge_readings: 0"));
    CHECK(has_line("power.gauge_millivolts: unknown"));
    CHECK(has_line("power.percent_gauge: unknown"));
    CHECK(has_line("power.percent_shown: unknown"));
    CHECK(has_line("power.capped: unknown"));
    CHECK(has_line("power.held: unknown"));
    CHECK(has_line("power.samples: 0"));
    input.power.history = NULL;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("power.samples: unknown"));
}

static void the_power_history_lists_a_sample_per_minute_oldest_first(void)
{
    DebugReportInput input = plain_input();
    power_history_record(&history,
                         (PowerSample){12, 4176, 97, POWER_SAMPLE_VBUS | POWER_SAMPLE_CHARGE_PIN});
    power_history_record(&history, (PowerSample){13, 4170, 97, POWER_SAMPLE_PLAYING});
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("power.samples: 2"));
    CHECK(has_line("power.sample_interval_s: 60"));
    CHECK(has_line("power.sample: 12 4176 97 110"));
    CHECK(has_line("power.sample: 13 4170 97 001"));
    CHECK(strstr(text, "power.sample: 12") < strstr(text, "power.sample: 13"));
}

static void a_power_event_gives_vbus_and_the_raw_charge_pin(void)
{
    DebugReportInput input = plain_input();
    record(DEBUG_EVENT_POWER, POWER_SAMPLE_VBUS, NULL);
    record(DEBUG_EVENT_POWER, POWER_SAMPLE_CHARGE_PIN, NULL);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("event: 1000 power 1 0"));
    CHECK(has_line("event: 1001 power 0 1"));
}

static void a_cold_start_has_no_previous_run_to_report(void)
{
    DebugReportInput input = plain_input();
    input.start = (HalStartInfo){.warm = false, .starts = 1};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("start.count: 1"));
    CHECK(has_line("start.warm: 0"));
    CHECK(has_line("start.previous_uptime_ms: unknown"));
    CHECK(has_line("start.last_step: unknown"));
    CHECK(has_line("fault.cfsr: unknown"));
    CHECK(has_line("fault.pc: unknown"));
}

static void a_warm_start_reports_the_previous_run_and_its_fault(void)
{
    DebugReportInput input = plain_input();
    input.start = (HalStartInfo){.warm = true,
                                 .starts = 4,
                                 .previous_uptime_ms = 41000,
                                 .fault_status = 0x00020000,
                                 .hard_fault_status = 0x40000000,
                                 .fault_pc = 0x0305A1B2,
                                 .last_step = HAL_START_STEP_CLOCK_SWITCH,
                                 .last_step_ms = 40990,
                                 .last_millivolts = 3712};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("start.count: 4"));
    CHECK(has_line("start.previous_uptime_ms: 41000"));
    CHECK(has_line("start.previous_power_off: 0"));
    CHECK(has_line("start.last_step: clock_switch"));
    CHECK(has_line("start.last_step_finished: 0"));
    CHECK(has_line("start.last_millivolts: 3712"));
    CHECK(has_line("fault.cfsr: 0x00020000"));
    CHECK(has_line("fault.hfsr: 0x40000000"));
    CHECK(has_line("fault.pc: 0x0305a1b2"));
}

static void the_last_usb_session_is_reported_once_there_was_one(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.last_end: none"));
    CHECK(has_line("usb.internal_sectors_written: unknown"));
    input.usb = (HalUsbLast){.end = HAL_USB_END_EJECTED, .internal_written = 96};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.last_end: ejected"));
    CHECK(has_line("usb.internal_sectors_written: 96"));
    CHECK(has_line("usb.card_sectors_written: 0"));
}

static void stopped_audio_reports_its_running_figures_as_unknown(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("audio.state: stopped"));
    CHECK(has_line("audio.running: 0"));
    CHECK(has_line("audio.buffered_ms: unknown"));
    CHECK(has_line("audio.underruns: unknown"));
    CHECK(has_line("audio.handoff_avg_us: unknown"));
    CHECK(has_line("audio.busy_percent: unknown"));
}

static void running_audio_reports_averages_worst_times_and_the_busy_share(void)
{
    DebugReportInput input = plain_input();
    input.audio = (DebugReportAudio){.running = true,
                                     .state = HAL_AUDIO_PLAYING,
                                     .stats = {.buffered_ms = 148,
                                               .underruns = 2,
                                               .output_underruns = 1,
                                               .blocks = 40,
                                               .decode_max_us = 9100,
                                               .decode_total_us = 200000,
                                               .read_max_us = 4200,
                                               .read_total_us = 48000,
                                               .busy_us = 300000,
                                               .elapsed_us = 2000000}};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("audio.state: playing"));
    CHECK(has_line("audio.buffered_ms: 148"));
    CHECK(has_line("audio.underruns: 2"));
    CHECK(has_line("audio.output_underruns: 1"));
    CHECK(has_line("audio.blocks: 40"));
    CHECK(has_line("audio.handoff_avg_us: 5000"));
    CHECK(has_line("audio.handoff_max_us: 9100"));
    CHECK(has_line("audio.read_avg_us: 1200"));
    CHECK(has_line("audio.read_max_us: 4200"));
    CHECK(has_line("audio.busy_percent: 15"));
}

static void frame_figures_are_unknown_until_a_frame_was_drawn(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("frame.count: 0"));
    CHECK(has_line("frame.total_avg_us: unknown"));
    input.frames = (DebugFrames){.count = 32,
                                 .total_us = 21000,
                                 .worst_us = 44000,
                                 .update_us = 3000,
                                 .draw_us = 12000,
                                 .send_us = 6000,
                                 .part_us = {1, 2, 3, 4, 5, 6}};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("frame.total_avg_us: 21000"));
    CHECK(has_line("frame.total_worst_us: 44000"));
    CHECK(has_line("frame.update_avg_us: 3000"));
    CHECK(has_line("frame.draw_avg_us: 12000"));
    CHECK(has_line("frame.send_avg_us: 6000"));
    CHECK(has_line("frame.part.covers_us: 1"));
    CHECK(has_line("frame.part.audio_us: 6"));
}

static void events_are_listed_oldest_first_with_names(void)
{
    DebugReportInput input = plain_input();
    record(DEBUG_EVENT_KEY, KEY_PLAY | KEY_GESTURE_LONG_PRESS << 8, NULL);
    record(DEBUG_EVENT_SCREEN, 2, "Now Playing");
    record(DEBUG_EVENT_PLAYER, 1 | 17 << 8, NULL);
    record(DEBUG_EVENT_UNDERRUN, 3, NULL);
    record(DEBUG_EVENT_USB_END, 1, NULL);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("events.recorded: 5"));
    CHECK(has_line("events.dropped: 0"));
    CHECK(has_line("event: 1000 key play long"));
    CHECK(has_line("event: 1001 screen 2 Now Pla"));
    CHECK(has_line("event: 1002 player playing 17"));
    CHECK(has_line("event: 1003 underrun 3"));
    CHECK(has_line("event: 1004 usb_end ejected"));
    CHECK(strstr(text, "event: 1000") < strstr(text, "event: 1004"));
}

static void event_text_outside_plain_ascii_is_replaced(void)
{
    DebugReportInput input = plain_input();
    record(DEBUG_EVENT_SCREEN, 1, "Caf\xC3\xA9\nx");
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("event: 1000 screen 1 Caf???x"));
}

static void an_unknown_event_kind_is_named_as_such(void)
{
    DebugReportInput input = plain_input();
    record((DebugEventKind)200, 9, NULL);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("event: 1000 unknown 9"));
}

static void missing_sources_are_reported_as_unknown(void)
{
    DebugReportInput input = plain_input();
    input.log = NULL;
    input.profile = NULL;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("events.recorded: unknown"));
    CHECK(has_line("profile.state: unknown"));
}

static void the_histogram_lists_runs_of_used_buckets_in_hex(void)
{
    DebugReportInput input = plain_input();
    buckets[0] = 1;
    buckets[1] = 0x1f;
    buckets[0x1a0] = 0xfffe;
    profile.enabled = true;
    profile.samples = 70000;
    profile.idle_samples = 4000;
    profile.overlay_samples = 12;
    profile.other_samples = 3;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("profile.state: on"));
    CHECK(has_line("profile.samples: 70000"));
    CHECK(has_line("profile.idle: 4000"));
    CHECK(has_line("profile.overlay: 12"));
    CHECK(has_line("profile.other: 3"));
    CHECK(has_line("profile.code_base: 0x03050000"));
    CHECK(has_line("profile.bucket_bytes: 128"));
    CHECK(has_line("profile.buckets: 1229"));
    CHECK(has_line("profile.row: 0 1 1f"));
    CHECK(has_line("profile.row: 1a0 fffe"));
}

static void a_long_run_of_buckets_breaks_into_rows(void)
{
    DebugReportInput input = plain_input();
    for (int index = 0; index < DEBUG_REPORT_ROW_BUCKETS + 2; index++) {
        buckets[index] = 1;
    }
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(
        has_line("profile.row: 0 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1"));
    CHECK(has_line("profile.row: 20 1 1"));
}

static void a_full_profile_says_so(void)
{
    DebugReportInput input = plain_input();
    profile.full = true;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("profile.state: full"));
}

static void the_end_line_gives_the_length_of_the_body(void)
{
    DebugReportInput input = plain_input();
    uint32_t length = format(&input, DEBUG_REPORT_BYTES);
    const char *tail = strstr(text, "\ntruncated: 0\nend: ");
    CHECK(tail != NULL);
    unsigned body = 0;
    CHECK_EQUAL(1, sscanf(tail, "\ntruncated: 0\nend: %u\n", &body));
    CHECK_EQUAL(tail + 1 - text, body);
    CHECK_EQUAL('\n', text[length - 1]);
}

static void the_rest_of_the_file_is_newlines_and_nothing_is_written_past_it(void)
{
    DebugReportInput input = plain_input();
    uint32_t length = format(&input, DEBUG_REPORT_BYTES);
    CHECK(length < DEBUG_REPORT_BYTES);
    for (uint32_t index = length; index < DEBUG_REPORT_BYTES; index++) {
        CHECK_EQUAL('\n', text[index]);
    }
    for (uint32_t index = 0; index < GUARD_BYTES; index++) {
        CHECK_EQUAL('#', text[DEBUG_REPORT_BYTES + index]);
    }
}

static void fill_everything(uint16_t bucket_count)
{
    for (int index = 0; index < BUCKETS; index++) {
        buckets[index] = bucket_count;
    }
    for (int index = 0; index < POWER_HISTORY_SAMPLES + 3; index++) {
        power_history_record(&history, (PowerSample){65535, 4200, 100, 7});
    }
    for (int index = 0; index < DEBUG_LOG_CAPACITY + 3; index++) {
        record(DEBUG_EVENT_OUTPUT_UNDERRUN, 0xFFFFFFFFu, "WWWWWWW");
        events.events[index % DEBUG_LOG_CAPACITY].time_ms = 0xFFFFFFFFu;
    }
}

static void the_fullest_report_fits_the_file_without_truncation(void)
{
    DebugReportInput input = plugged_in_at_97_percent();
    fill_everything(0xfffe);
    uint32_t length = format(&input, DEBUG_REPORT_BYTES);
    CHECK(length <= DEBUG_REPORT_BYTES);
    CHECK(has_line("truncated: 0"));
    CHECK(has_line("power.sample: 65535 4200 100 111"));
    CHECK(has_line("profile.row: 4c0 fffe fffe fffe fffe fffe fffe fffe fffe fffe fffe fffe fffe "
                   "fffe"));
}

static void a_small_buffer_cuts_at_a_line_and_says_so(void)
{
    enum {
        SMALL = 1024
    };
    DebugReportInput input = plain_input();
    fill_everything(0xfffe);
    uint32_t length = format(&input, SMALL);
    CHECK(length <= SMALL);
    CHECK(has_line("truncated: 1"));
    CHECK_EQUAL(0, strncmp(text, "debug_report: 1\n", 16));
    const char *tail = strstr(text, "\ntruncated: 1\nend: ");
    CHECK(tail != NULL);
    CHECK_EQUAL('#', text[SMALL]);
    for (const char *cursor = text; cursor < tail; cursor++) {
        const char *line_end = strchr(cursor, '\n');
        CHECK(memchr(cursor, ':', (size_t)(line_end - cursor)) != NULL);
        cursor = line_end;
    }
}

static void a_buffer_too_small_for_the_end_lines_gets_nothing(void)
{
    DebugReportInput input = plain_input();
    CHECK_EQUAL(0, format(&input, DEBUG_REPORT_TAIL_BYTES));
    CHECK_EQUAL('#', text[0]);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_schema_version_is_the_first_line),
        TEST_CASE(the_firmware_is_named_with_its_version_and_build_tag),
        TEST_CASE(a_version_without_a_tag_reports_the_build_as_unknown),
        TEST_CASE(core_0_busy_time_is_a_share_of_the_uptime),
        TEST_CASE(a_wrapped_microsecond_clock_gives_no_busy_share),
        TEST_CASE(an_unread_battery_is_unknown_not_zero),
        TEST_CASE(the_power_section_gives_the_raw_pin_the_adc_and_each_percent),
        TEST_CASE(a_charging_gauge_at_its_limit_is_reported_as_capped),
        TEST_CASE(a_shown_percent_that_lags_the_gauge_is_reported_as_held),
        TEST_CASE(power_values_not_read_yet_are_unknown),
        TEST_CASE(the_power_history_lists_a_sample_per_minute_oldest_first),
        TEST_CASE(a_power_event_gives_vbus_and_the_raw_charge_pin),
        TEST_CASE(a_cold_start_has_no_previous_run_to_report),
        TEST_CASE(a_warm_start_reports_the_previous_run_and_its_fault),
        TEST_CASE(the_last_usb_session_is_reported_once_there_was_one),
        TEST_CASE(stopped_audio_reports_its_running_figures_as_unknown),
        TEST_CASE(running_audio_reports_averages_worst_times_and_the_busy_share),
        TEST_CASE(frame_figures_are_unknown_until_a_frame_was_drawn),
        TEST_CASE(events_are_listed_oldest_first_with_names),
        TEST_CASE(event_text_outside_plain_ascii_is_replaced),
        TEST_CASE(an_unknown_event_kind_is_named_as_such),
        TEST_CASE(missing_sources_are_reported_as_unknown),
        TEST_CASE(the_histogram_lists_runs_of_used_buckets_in_hex),
        TEST_CASE(a_long_run_of_buckets_breaks_into_rows),
        TEST_CASE(a_full_profile_says_so),
        TEST_CASE(the_end_line_gives_the_length_of_the_body),
        TEST_CASE(the_rest_of_the_file_is_newlines_and_nothing_is_written_past_it),
        TEST_CASE(the_fullest_report_fits_the_file_without_truncation),
        TEST_CASE(a_small_buffer_cuts_at_a_line_and_says_so),
        TEST_CASE(a_buffer_too_small_for_the_end_lines_gets_nothing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
