#include <stdio.h>
#include <string.h>

#include "check.h"
#include "core/key.h"
#include "core/key_tracker.h"
#include "device/debug_report.h"

enum {
    BUCKETS = 1229,
    BUSY_BUCKETS = 256,
    GUARD_BYTES = 16
};

static char text[DEBUG_REPORT_BYTES + GUARD_BYTES + 1];
static uint16_t buckets[BUCKETS];
static DebugLog events;
static PcProfile profile;
static PowerHistory history;
static DebugScriptRun script;

static DebugReportInput plain_input(void)
{
    memset(buckets, 0, sizeof buckets);
    debug_log_reset(&events);
    history.recorded = 0;
    memset(&script, 0, sizeof script);
    profile = (PcProfile){.buckets = buckets,
                          .bucket_count = BUCKETS,
                          .bucket_shift = 7,
                          .code = {0x03050000, 0x2666C}};
    return (DebugReportInput){.sequence = 12,
                              .reason = DEBUG_REPORT_USB_SESSION,
                              .name = FIRMWARE_NAME,
                              .version = "1.0.0-1051212",
                              .uptime_ms = 61000,
                              .clock_mhz = 200,
                              .clock_us = 61000000,
                              .idle_us = 45750000,
                              .power = {.history = &history},
                              .script = &script,
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
    char wanted[256];
    snprintf(wanted, sizeof wanted, "\n%s\n", line);
    return strstr(text, wanted) != NULL;
}

static void dac_diagnostics_report_cached_reads_and_sequence_durations(void)
{
    DebugReportInput input = plain_input();
    input.dac = (DacDiagnostics){.up = false,
                                 .valid = 1,
                                 .power = {0xB6, 0},
                                 .start_us = 12345,
                                 .stop_us = 150123,
                                 .gain_us = 162123};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("dac.up: 0"));
    CHECK(has_line("dac.power_left: 182"));
    CHECK(has_line("dac.power_right: unknown"));
    CHECK(has_line("dac.start_us: 12345"));
    CHECK(has_line("dac.stop_us: 150123"));
    CHECK(has_line("dac.gain_us: 162123"));
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

static void missing_boot_markers_are_reported_as_unknown(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("boot.clock_us: unknown"));
    CHECK(has_line("boot.interactive_us: unknown"));
    CHECK(has_line("boot.audio_us: unknown"));
}

static void boot_markers_keep_zero_and_completed_phases_separate_from_unknown(void)
{
    uint32_t times[DEBUG_BOOT_COUNT] = {
        [DEBUG_BOOT_CLOCK] = 1, [DEBUG_BOOT_FRAME] = 400001, [DEBUG_BOOT_INTERACTIVE] = 1500001};
    DebugReportInput input = plain_input();
    input.boot_us = times;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("boot.clock_us: 0"));
    CHECK(has_line("boot.frame_us: 400000"));
    CHECK(has_line("boot.interactive_us: 1500000"));
    CHECK(has_line("boot.audio_us: unknown"));
}

static DebugReportInput plugged_in_at_97_percent(void)
{
    DebugReportInput input = plain_input();
    input.battery = (HalBattery){.known = true, .percent = 97, .millivolts = 4180, .usb_power = 1};
    input.power = (DebugReportPower){.vbus = true,
                                     .usb_powered = true,
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

static void the_report_says_which_one_it_is_and_what_wrote_it(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK_EQUAL(0, strncmp(text,
                           "debug_report: 1\nreport.sequence: 12\nreport.reason: usb_session\n"
                           "report.cold_start: 0\n",
                           79));
    input.reason = DEBUG_REPORT_AUTO_OFF;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("report.reason: auto_off"));
    static const char *const NAMES[] = {"none",      "usb_session", "shutdown",
                                        "auto_off",  "sleep_timer", "low_battery",
                                        "unplugged", "restart",     "no_host"};
    for (int reason = 0; reason < DEBUG_REPORT_REASON_COUNT; reason++) {
        char line[64];
        input.reason = (DebugReportReason)reason;
        format(&input, DEBUG_REPORT_BYTES);
        snprintf(line, sizeof line, "report.reason: %s", NAMES[reason]);
        CHECK(has_line(line));
    }
}

static void a_report_in_the_first_seconds_of_a_cold_start_says_so(void)
{
    DebugReportInput input = plain_input();
    input.uptime_ms = 789;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("report.cold_start: 1"));
    input.uptime_ms = DEBUG_REPORT_COLD_MS;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("report.cold_start: 0"));
    input.uptime_ms = 789;
    input.start.warm = true;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("report.cold_start: 0"));
}

static void before_the_gauge_has_a_reading_the_start_up_sample_stands_in_and_is_labelled(void)
{
    DebugReportInput input = plain_input();
    input.battery = (HalBattery){.known = false, .usb_power = true};
    input.power.adc_known = true;
    input.power.adc_raw = 812;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("battery.source: start_up_sample"));
    CHECK(has_line("battery.millivolts: 3964"));
    CHECK(has_line("battery.percent: 75"));
    CHECK(has_line("battery.charging: 0"));
    CHECK(has_line("battery.usb_power: 1"));
    CHECK(has_line("power.adc_source: start_up_sample"));
    CHECK(has_line("power.adc_raw: 812"));
    CHECK(has_line("power.adc_millivolts: 3964"));
    CHECK(has_line("power.gauge_millivolts: unknown"));
}

static void with_gauge_readings_the_battery_lines_are_the_gauges(void)
{
    DebugReportInput input = plugged_in_at_97_percent();
    input.power.adc_from_gauge = true;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("battery.source: gauge"));
    CHECK(has_line("battery.millivolts: 4180"));
    CHECK(has_line("battery.percent: 97"));
    CHECK(has_line("power.adc_source: gauge"));
}

static DebugReportSlot head_of_formatted(const DebugReportInput *input)
{
    format(input, DEBUG_REPORT_BYTES);
    return debug_report_read_head(text, DEBUG_REPORT_HEAD_BYTES);
}

static void the_head_of_a_report_gives_its_sequence_and_cold_start_flag(void)
{
    DebugReportInput input = plain_input();
    DebugReportSlot slot = head_of_formatted(&input);
    CHECK(slot.valid);
    CHECK_EQUAL(12, slot.sequence);
    CHECK(!slot.cold_start);
    input.sequence = 4000000000u;
    input.uptime_ms = 700;
    slot = head_of_formatted(&input);
    CHECK(slot.valid);
    CHECK_EQUAL(4000000000u, slot.sequence);
    CHECK(slot.cold_start);
}

static DebugReportSlot head(const char *start)
{
    char bytes[DEBUG_REPORT_HEAD_BYTES];
    memset(bytes, '\n', sizeof bytes);
    memcpy(bytes, start, strlen(start));
    return debug_report_read_head(bytes, sizeof bytes);
}

static void a_file_that_is_not_a_finished_report_is_not_a_valid_slot(void)
{
    char zeros[DEBUG_REPORT_HEAD_BYTES] = {0};
    CHECK(!debug_report_read_head(zeros, sizeof zeros).valid);
    CHECK(!head("").valid);
    CHECK(!head("debug_report: 2\nreport.sequence: 5\nreport.cold_start: 0\n").valid);
    CHECK(!head("debug_report: 1\nreport.cold_start: 0\n").valid);
    CHECK(!head("debug_report: 1\nreport.sequence: five\nreport.cold_start: 0\n").valid);
    CHECK(!head("debug_report: 1\nreport.sequence: 5\n").valid);
    CHECK(!head("xdebug_report: 1\nreport.sequence: 5\nreport.cold_start: 0\n").valid);
    CHECK(head("debug_report: 1\nreport.sequence: 5\nreport.reason: x\nreport.cold_start: 0\n")
              .valid);
    CHECK(!debug_report_read_head("debug_report: 1\nreport.sequence: 5", 33).valid);
}

static DebugReportPlace place(const uint32_t *sequences, const bool *cold, bool cold_start)
{
    DebugReportSlot slots[DEBUG_REPORT_SLOTS];
    for (int slot = 0; slot < DEBUG_REPORT_SLOTS; slot++) {
        slots[slot] = (DebugReportSlot){
            .valid = sequences[slot] != 0, .cold_start = cold[slot], .sequence = sequences[slot]};
    }
    return debug_report_place(slots, cold_start);
}

static void the_first_reports_fill_the_empty_slots_in_turn(void)
{
    const bool warm[DEBUG_REPORT_SLOTS] = {false, false, false};
    DebugReportPlace first = place((uint32_t[]){0, 0, 0}, warm, false);
    DebugReportPlace second = place((uint32_t[]){1, 0, 0}, warm, false);
    DebugReportPlace third = place((uint32_t[]){1, 2, 0}, warm, false);
    CHECK_EQUAL(0, first.slot);
    CHECK_EQUAL(1, first.sequence);
    CHECK_EQUAL(1, second.slot);
    CHECK_EQUAL(2, second.sequence);
    CHECK_EQUAL(2, third.slot);
    CHECK_EQUAL(3, third.sequence);
}

static void a_new_report_replaces_the_oldest_and_counts_on_from_the_newest(void)
{
    const bool warm[DEBUG_REPORT_SLOTS] = {false, false, false};
    DebugReportPlace next = place((uint32_t[]){7, 5, 6}, warm, false);
    CHECK_EQUAL(1, next.slot);
    CHECK_EQUAL(8, next.sequence);
    next = place((uint32_t[]){7, 8, 6}, warm, true);
    CHECK_EQUAL(2, next.slot);
    CHECK_EQUAL(9, next.sequence);
}

static void a_cold_start_report_replaces_the_cold_start_report_before_it(void)
{
    const bool cold[DEBUG_REPORT_SLOTS] = {false, false, true};
    DebugReportPlace next = place((uint32_t[]){7, 8, 9}, cold, true);
    CHECK_EQUAL(2, next.slot);
    CHECK_EQUAL(10, next.sequence);
    next = place((uint32_t[]){7, 8, 9}, cold, false);
    CHECK_EQUAL(0, next.slot);
    CHECK_EQUAL(10, next.sequence);
}

static void an_older_cold_start_report_is_not_what_a_new_one_replaces(void)
{
    const bool cold[DEBUG_REPORT_SLOTS] = {true, false, false};
    DebugReportPlace next = place((uint32_t[]){7, 8, 9}, cold, true);
    CHECK_EQUAL(0, next.slot);
    const bool middle[DEBUG_REPORT_SLOTS] = {false, true, false};
    next = place((uint32_t[]){7, 8, 9}, middle, true);
    CHECK_EQUAL(0, next.slot);
    CHECK_EQUAL(10, next.sequence);
}

static void the_firmware_is_named_with_its_version_and_build_tag(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("firmware.name: " FIRMWARE_NAME));
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
    CHECK(has_line("battery.source: unknown"));
    CHECK(has_line("battery.millivolts: unknown"));
    CHECK(has_line("battery.percent: unknown"));
    CHECK(has_line("power.adc_source: unknown"));
    input.battery = (HalBattery){.known = true, .percent = 71, .millivolts = 3987, .usb_power = 1};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("battery.millivolts: 3987"));
    CHECK(has_line("battery.percent: 71"));
    CHECK(has_line("battery.usb_power: 1"));
}

static void the_power_section_gives_the_raw_pin_the_adc_and_each_percent(void)
{
    DebugReportInput input = plugged_in_at_97_percent();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("power.vbus: 1"));
    CHECK(has_line("power.usb_powered: 1"));
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
    CHECK(has_line("power.sample: 12 4176 97 11000; 13 4170 97 00100"));
    for (uint16_t minute = 14; minute < 17; minute++) {
        power_history_record(
            &history,
            (PowerSample){minute, 4100, 90, POWER_SAMPLE_USB_POWERED | POWER_SAMPLE_USB_KEPT_ON});
    }
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line(
        "power.sample: 12 4176 97 11000; 13 4170 97 00100; 14 4100 90 00011; 15 4100 90 00011"));
    CHECK(has_line("power.sample: 16 4100 90 00011"));
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

static void load_script(const char *text_to_run)
{
    size_t length = strlen(text_to_run);
    memcpy(script.text, text_to_run, length);
    debug_script_load(&script, (uint32_t)length, (DebugScriptHost){"1.0.0-abc1234", false});
}

static void without_a_script_the_section_says_none(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.state: none"));
    CHECK(has_line("script.started_ms: unknown"));
    CHECK(has_line("script.interrupted_line: 0"));
    CHECK(strstr(text, "script.line") == NULL);
    input.script = NULL;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.state: unknown"));
}

static void a_finished_script_lists_each_line_with_its_outcome(void)
{
    DebugReportInput input = plain_input();
    load_script(
        "# open music\nbuild abc1234\n\nplay,down\nerase 1\nwait soon\nset Flash=on\nreconnect\n"
        "play\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    script.started_ms = 5000;
    script.finished_ms = 5900;
    script.status[3] = DEBUG_LINE_OK;
    script.status[6] = DEBUG_LINE_FAILED;
    script.status[7] = DEBUG_LINE_OK;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.state: finished"));
    CHECK(has_line("script.started_ms: 5000"));
    CHECK(has_line("script.finished_ms: 5900"));
    CHECK(has_line("script.line: 2 ok build abc1234"));
    CHECK(has_line("script.line: 4 ok play,down"));
    CHECK(has_line("script.line: 5 unknown erase 1"));
    CHECK(has_line("script.line: 6 bad_argument wait soon"));
    CHECK(has_line("script.line: 7 failed set Flash=on"));
    CHECK(has_line("script.line: 8 ok reconnect"));
    CHECK(has_line("script.line: 9 not_run play"));
    CHECK(strstr(text, "script.line: 1 ") == NULL);
    CHECK(strstr(text, "script.line: 3 ") == NULL);
}

static void a_refused_script_says_why_and_shows_its_lines_as_not_run(void)
{
    DebugReportInput input = plain_input();
    load_script("build 7777777\nplay\n");
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.state: refused_build"));
    CHECK(has_line("script.started_ms: unknown"));
    CHECK(has_line("script.line: 1 not_run build 7777777"));
    CHECK(has_line("script.line: 2 not_run play"));
}

static void a_running_script_has_no_finish_time(void)
{
    DebugReportInput input = plain_input();
    load_script("build any\nwait 60000\n");
    script.state = DEBUG_SCRIPT_RUNNING;
    script.started_ms = 7000;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.state: running"));
    CHECK(has_line("script.started_ms: 7000"));
    CHECK(has_line("script.finished_ms: unknown"));
}

static void a_script_cut_short_by_a_reset_names_the_line_it_was_on(void)
{
    DebugReportInput input = plain_input();
    input.interrupted_line = 7;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.interrupted_line: 7"));
}

static void script_text_outside_plain_ascii_is_replaced(void)
{
    DebugReportInput input = plain_input();
    load_script("build any\nset Skip songs=Hold vol\xC2\xB1\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.line: 2 not_run set Skip songs=Hold vol??"));
}

static void a_measure_is_one_line_of_named_figures(void)
{
    DebugReportInput input = plain_input();
    load_script("build any\nmeasure 3000\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    script.measure_count = 1;
    script.measures[0] = (DebugMeasure){.line = 2,
                                        .duration_ms = 3000,
                                        .frames = 100,
                                        .frame_us = 2100000,
                                        .frame_worst_us = 44000,
                                        .update_us = 300000,
                                        .draw_us = 1200000,
                                        .send_us = 600000,
                                        .audio_running = 1,
                                        .buffered_ms = 148,
                                        .underruns = 2,
                                        .output_underruns = 1,
                                        .blocks = 40,
                                        .handoff_us = 200000,
                                        .handoff_max_us = 9100,
                                        .read_us = 48000,
                                        .read_max_us = 4200,
                                        .audio_busy_us = 450000,
                                        .audio_elapsed_us = 3000000,
                                        .clock_us = 3000000,
                                        .idle_us = 1200000};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, "\nscript.measure: line=2 ms=3000 frames=100 frame_avg_us=21000 "
                       "frame_worst_us=44000 update_avg_us=3000 draw_avg_us=12000 "
                       "send_avg_us=6000 audio=1 buffered_ms=148 underruns=2 output_underruns=1 "
                       "blocks=40 handoff_avg_us=5000 handoff_max_us=9100 read_avg_us=1200 "
                       "read_max_us=4200 audio_busy_percent=15 core0_busy_percent=60 "
                       "core1_busy_permille=unknown\n") != NULL);
}

static void a_measure_without_frames_or_audio_reports_them_as_unknown(void)
{
    DebugReportInput input = plain_input();
    load_script("build any\nmeasure 100\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    script.measure_count = 1;
    script.measures[0] = (DebugMeasure){.line = 2, .duration_ms = 100, .clock_us = 100000};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, "\nscript.measure: line=2 ms=100 frames=0 frame_avg_us=unknown "
                       "frame_worst_us=unknown update_avg_us=unknown draw_avg_us=unknown "
                       "send_avg_us=unknown audio=0 buffered_ms=unknown underruns=unknown "
                       "output_underruns=0 blocks=0 handoff_avg_us=unknown handoff_max_us=unknown "
                       "read_avg_us=unknown read_max_us=unknown audio_busy_percent=unknown "
                       "core0_busy_percent=100 core1_busy_permille=unknown\n") != NULL);
}

static void a_measure_reports_core_1_load_from_its_idle_cycles(void)
{
    DebugReportInput input = plain_input();
    load_script("build any\nmeasure 100\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    script.measure_count = 1;
    script.measures[0] = (DebugMeasure){.line = 2,
                                        .duration_ms = 100,
                                        .clock_us = 100000,
                                        .core1_cycles = 10000000,
                                        .core1_idle_cycles = 7654321};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, " core1_busy_permille=234\n") != NULL);
}

static void a_measure_with_more_core_1_idle_than_elapsed_reports_unknown_load(void)
{
    DebugReportInput input = plain_input();
    load_script("build any\nmeasure 100\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    script.measure_count = 1;
    script.measures[0] = (DebugMeasure){.line = 2,
                                        .duration_ms = 100,
                                        .clock_us = 100000,
                                        .core1_cycles = 5000,
                                        .core1_idle_cycles = 6000};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, " core1_busy_permille=unknown\n") != NULL);
}

static void screenshots_are_listed_by_slot_with_their_names(void)
{
    DebugReportInput input = plain_input();
    load_script("build any\nscreenshot home\nplay\nscreenshot music\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    script.screenshot_count = 2;
    script.screenshot_lines[0] = 2;
    script.screenshot_lines[1] = 4;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.screenshot: 0 home"));
    CHECK(has_line("script.screenshot: 1 music"));
}

static void probes_are_listed_with_both_words_the_time_and_the_two_levels(void)
{
    DebugReportInput input = plain_input();
    load_script("build any\npeek 0x500100E8\npoke 0x99999999 1\ngpio 2.0 low\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    script.status[1] = DEBUG_LINE_OK;
    script.status[2] = DEBUG_LINE_REFUSED;
    script.status[3] = DEBUG_LINE_OK;
    script.probes[0] = (DebugProbe){2, HAL_PROBE_CABLE, 5120, 0x00000002, 0x00000002};
    script.probes[1] = (DebugProbe){4, HAL_PROBE_CHARGE_KNOWN | HAL_PROBE_CHARGE_PIN, 5184,
                                    0x00000011, 0x00000010};
    script.probe_count = 2;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.line: 2 ok peek 0x500100E8"));
    CHECK(has_line("script.line: 3 refused poke 0x99999999 1"));
    CHECK(has_line("script.probe: line=2 ms=5120 before=0x00000002 after=0x00000002 cable=1 "
                   "charge_pin=unknown"));
    CHECK(has_line("script.probe: line=4 ms=5184 before=0x00000011 after=0x00000010 cable=0 "
                   "charge_pin=1"));
}

static void rtc_reads_list_every_register_the_decoded_time_and_the_trust_flag(void)
{
    DebugReportInput input = plain_input();
    load_script("build any\nrtc read\nrtc read\nrtc read\nrtc set\nrtc set\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    script.rtc[0] = (DebugRtcRead){.line = 2,
                                   .status = HAL_RTC_OK,
                                   .time_ms = 5120,
                                   .registers = {0x00, 0x00, 0x05, 0x00, 0x12, 0x03, 0x06, 0x01,
                                                 0x26, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00}};
    script.rtc[1] = (DebugRtcRead){.line = 3,
                                   .status = HAL_RTC_OK,
                                   .time_ms = 5200,
                                   .registers = {0x08, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    script.rtc[2] = (DebugRtcRead){.line = 4, .status = HAL_RTC_NO_ACK, .time_ms = 5300};
    script.rtc_count = 3;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.rtc: line=2 ms=5120 action=read written_s=unknown status=ok "
                   "registers=00000500120306012680808080000000 vl=0 time=2026-01-03T12:00:05 "
                   "weekday=6 century=0"));
    CHECK(has_line("script.rtc: line=3 ms=5200 action=read written_s=unknown status=ok "
                   "registers=0800ffffffffffffffffffffffffffff vl=1 time=invalid weekday=7 "
                   "century=1"));
    CHECK(has_line("script.rtc: line=4 ms=5300 action=read written_s=unknown status=no_ack "
                   "registers=unknown vl=unknown time=unknown weekday=unknown century=unknown"));
    script.rtc[0] =
        (DebugRtcRead){.line = 5,
                       .status = HAL_RTC_OK,
                       .action = DEBUG_RTC_SET,
                       .time_ms = 5400,
                       .written_s = 1794390060,
                       .registers = {0x00, 0x00, 0x00, 0x41, 0x09, 0x11, 0x03, 0x11, 0x26}};
    script.rtc[1] = (DebugRtcRead){.line = 6, .status = HAL_RTC_NO_TIME, .action = DEBUG_RTC_SET};
    script.rtc_count = 2;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.rtc: line=5 ms=5400 action=set written_s=1794390060 status=ok "
                   "registers=00000041091103112600000000000000 vl=0 time=2026-11-11T09:41:00 "
                   "weekday=3 century=0"));
    CHECK(has_line("script.rtc: line=6 ms=0 action=set written_s=unknown status=no_time "
                   "registers=unknown vl=unknown time=unknown weekday=unknown century=unknown"));
}

static DebugSoak soak_over(uint32_t first_sample, uint32_t samples)
{
    for (uint32_t index = 0; index < first_sample + samples + 2; index++) {
        uint32_t within = index - first_sample;
        bool soaking = index >= first_sample && within < samples;
        power_history_record(&history, (PowerSample){(uint16_t)(85 + index * 3 / 2),
                                                     (uint16_t)(soaking ? 4000 - 4 * within : 4150),
                                                     90, POWER_SAMPLE_VBUS});
    }
    load_script("build any\nsupply off\nscreen off\nsoak 45\n");
    script.state = DEBUG_SCRIPT_FINISHED;
    script.soak_count = 1;
    return (DebugSoak){.line = 4,
                       .end = DEBUG_SOAK_DONE,
                       .started_ms = 6000000,
                       .ended_ms = 6000000 + 45 * 60000,
                       .idle_ms = 2376000,
                       .start = {first_sample,
                                 DEBUG_SOAK_CABLE | DEBUG_SOAK_CHARGE_KNOWN |
                                     DEBUG_SOAK_CHARGE_PIN | DEBUG_SOAK_SUPPLY_OFF |
                                     DEBUG_SOAK_AUDIO,
                                 200},
                       .finish = {first_sample + samples,
                                  DEBUG_SOAK_CABLE | DEBUG_SOAK_CHARGE_KNOWN |
                                      DEBUG_SOAK_SUPPLY_OFF | DEBUG_SOAK_AUDIO,
                                  96}};
}

static void a_soak_is_reported_with_its_voltage_slope_its_load_and_both_ends(void)
{
    DebugReportInput input = plain_input();
    script.soaks[0] = soak_over(10, 30);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("script.soak: line=4 minutes=45 end=done samples=30 first_mv=3996 last_mv=3888 "
                   "mv_per_hour=-160 busy_percent=12 cable_start=1 charge_pin_start=1 "
                   "supply_off_start=1 audio_start=1 screen_start=0 clock_mhz_start=200 "
                   "cable_end=1 charge_pin_end=0 supply_off_end=1 audio_end=1 screen_end=0 "
                   "clock_mhz_end=96"));
    CHECK(has_line("script.line: 4 not_run soak 45"));
}

static void a_rising_battery_gives_a_positive_slope(void)
{
    DebugReportInput input = plain_input();
    script.soaks[0] = soak_over(0, 8);
    for (uint32_t index = 0; index < 8; index++) {
        history.samples[index].millivolts = (uint16_t)(3900 + 10 * index);
    }
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, " samples=8 first_mv=3910 last_mv=3960 mv_per_hour=409 ") != NULL);
}

static void a_soak_with_fewer_than_four_samples_has_means_but_no_slope(void)
{
    DebugReportInput input = plain_input();
    script.soaks[0] = soak_over(5, 2);
    script.soaks[0].start.levels = DEBUG_SOAK_SCREEN;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, " samples=2 first_mv=3998 last_mv=3998 mv_per_hour=unknown ") != NULL);
    CHECK(strstr(text, " cable_start=0 charge_pin_start=unknown supply_off_start=0 audio_start=0 "
                       "screen_start=1 ") != NULL);
    script.soaks[0] = soak_over(5, 0);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, " samples=0 first_mv=unknown last_mv=unknown mv_per_hour=unknown ") != NULL);
}

static void a_soak_whose_first_samples_left_the_history_gives_no_voltages(void)
{
    DebugReportInput input = plain_input();
    script.soaks[0] = soak_over(4, POWER_HISTORY_SAMPLES);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, " samples=128 first_mv=unknown last_mv=unknown mv_per_hour=unknown ") !=
          NULL);
    input.power.history = NULL;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, " samples=128 first_mv=unknown ") != NULL);
}

static void a_soak_that_was_cut_short_says_why(void)
{
    static const char *const ENDS[] = {" end=key ", " end=low_battery ", " end=stopped "};
    DebugReportInput input = plain_input();
    script.soaks[0] = soak_over(0, 6);
    script.status[3] = DEBUG_LINE_CUT_SHORT;
    for (int end = DEBUG_SOAK_KEY; end <= DEBUG_SOAK_STOPPED; end++) {
        script.soaks[0].end = (uint8_t)end;
        script.soaks[0].ended_ms = script.soaks[0].started_ms + 119999;
        format(&input, DEBUG_REPORT_BYTES);
        CHECK(strstr(text, ENDS[end - DEBUG_SOAK_KEY]) != NULL);
        CHECK(strstr(text, "script.soak: line=4 minutes=1 ") != NULL);
    }
    CHECK(has_line("script.line: 4 cut_short soak 45"));
}

static void two_soaks_are_two_lines(void)
{
    DebugReportInput input = plain_input();
    script.soaks[0] = soak_over(0, 6);
    script.soaks[1] = script.soaks[0];
    script.soaks[1].line = 9;
    script.soak_count = 2;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, "\nscript.soak: line=4 ") != NULL);
    CHECK(strstr(text, "\nscript.soak: line=9 ") != NULL);
}

static void script_events_name_the_line_or_the_state(void)
{
    DebugReportInput input = plain_input();
    record(DEBUG_EVENT_SCRIPT, DEBUG_SCRIPT_EVENT_STATE + DEBUG_SCRIPT_RUNNING, NULL);
    record(DEBUG_EVENT_SCRIPT, 4, NULL);
    record(DEBUG_EVENT_SCRIPT, DEBUG_SCRIPT_EVENT_STATE + DEBUG_SCRIPT_REFUSED_BUILD, NULL);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("event: 1000 script running"));
    CHECK(has_line("event: 1001 script 4"));
    CHECK(has_line("event: 1002 script refused_build"));
}

static void the_loop_section_says_how_this_run_started(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("loop.boot: cold"));
    input.start = (HalStartInfo){.warm = true, .previous_power_off = true};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("loop.boot: restart"));
    input.start = (HalStartInfo){.warm = true};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("loop.boot: watchdog_or_reset"));
    input.start = (HalStartInfo){.warm = true, .fault_pc = 0x0305A1B2};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("loop.boot: fault"));
    input.loop.restarted = true;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("loop.boot: loop_restart"));
    input.start = (HalStartInfo){.warm = false};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("loop.boot: loop_restart"));
}

static void the_loop_section_counts_sessions_tried_without_the_cable_bit(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("loop.connect_attempts: 0"));
    CHECK(has_line("loop.last_connect: none"));
    CHECK(has_line("loop.cable_bit_at_connect: unknown"));
    input.loop = (DebugReportLoop){
        .attempts = 3, .answered = 2, .unanswered = 1, .last_connect = DEBUG_LOOP_CONNECT_NO_HOST};
    input.reason = DEBUG_REPORT_NO_HOST;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("report.reason: no_host"));
    CHECK(has_line("loop.connect_attempts: 3"));
    CHECK(has_line("loop.connect_answered: 2"));
    CHECK(has_line("loop.connect_unanswered: 1"));
    CHECK(has_line("loop.last_connect: no_host"));
    CHECK(has_line("loop.cable_bit_at_connect: 0"));
    input.loop.last_connect = DEBUG_LOOP_CONNECT_ANSWERED;
    input.loop.cable_at_connect = true;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("loop.last_connect: answered"));
    CHECK(has_line("loop.cable_bit_at_connect: 1"));
}

static void connect_events_say_what_became_of_the_attempt(void)
{
    DebugReportInput input = plain_input();
    record(DEBUG_EVENT_USB_CONNECT, DEBUG_LOOP_CONNECT_WAITING, NULL);
    record(DEBUG_EVENT_USB_CONNECT, DEBUG_LOOP_CONNECT_NO_HOST, NULL);
    record(DEBUG_EVENT_USB_CONNECT, DEBUG_LOOP_CONNECT_ANSWERED, NULL);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("event: 1000 usb_connect waiting"));
    CHECK(has_line("event: 1001 usb_connect no_host"));
    CHECK(has_line("event: 1002 usb_connect answered"));
}

static void scan_and_cover_events_name_their_phase_and_size(void)
{
    DebugReportInput input = plain_input();
    record(DEBUG_EVENT_SCAN, DEBUG_SCAN_START << DEBUG_SCAN_PHASE_SHIFT, NULL);
    record(DEBUG_EVENT_SCAN, (uint32_t)DEBUG_SCAN_LIBRARY << DEBUG_SCAN_PHASE_SHIFT | 4003, NULL);
    record(DEBUG_EVENT_SCAN, (uint32_t)DEBUG_SCAN_COVERS_STOPPED << DEBUG_SCAN_PHASE_SHIFT | 12,
           NULL);
    record(DEBUG_EVENT_COVER, 7 | DEBUG_COVER_FULL | DEBUG_COVER_FOUND, NULL);
    record(DEBUG_EVENT_COVER, 9, NULL);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("event: 1000 scan start 0"));
    CHECK(has_line("event: 1001 scan library 4003"));
    CHECK(has_line("event: 1002 scan covers_stopped 12"));
    CHECK(has_line("event: 1003 cover full 7 found"));
    CHECK(has_line("event: 1004 cover half 9 missing"));
}

static void emmc_starts_and_a_lost_start_flag_name_their_phase(void)
{
    DebugReportInput input = plain_input();
    record(DEBUG_EVENT_SCAN, (uint32_t)DEBUG_SCAN_EMMC_OK << DEBUG_SCAN_PHASE_SHIFT | 31, NULL);
    record(DEBUG_EVENT_SCAN, (uint32_t)DEBUG_SCAN_EMMC_FAILED << DEBUG_SCAN_PHASE_SHIFT | 0x231000,
           NULL);
    record(DEBUG_EVENT_SCAN, (uint32_t)DEBUG_SCAN_EMMC_FLAG_LOST << DEBUG_SCAN_PHASE_SHIFT | 0x301,
           NULL);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("event: 1000 scan emmc_ok 31"));
    CHECK(has_line("event: 1001 scan emmc_failed 2297856"));
    CHECK(has_line("event: 1002 scan emmc_flag_lost 769"));
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

static void the_usb_words_read_at_start_up_and_at_the_report_are_printed_whole(void)
{
    static const uint32_t AT_START[DEBUG_REPORT_GRF_WORDS] = {0x00000002, 0x0000001C, 0x00000001,
                                                              0x00000001, 0x00000000};
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.grf_e8_start: unknown"));
    CHECK(has_line("usb.grf_98_start: unknown"));
    input.usb_words = (DebugReportUsbWords){
        .grf_start = AT_START, .grf = {0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000004}};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.grf_e8_start: 0x00000002"));
    CHECK(has_line("usb.grf_84_start: 0x0000001c"));
    CHECK(has_line("usb.grf_b4_start: 0x00000001"));
    CHECK(has_line("usb.grf_b8_start: 0x00000001"));
    CHECK(has_line("usb.grf_98_start: 0x00000000"));
    CHECK(has_line("usb.grf_e8: 0x00000000"));
    CHECK(has_line("usb.grf_84: 0x00000003"));
    CHECK(has_line("usb.grf_b4: 0x00000002"));
    CHECK(has_line("usb.grf_b8: 0x00000000"));
    CHECK(has_line("usb.grf_98: 0x00000004"));
}

static void the_cores_session_bit_is_reported_only_while_the_controller_can_be_read(void)
{
    DebugReportInput input = plain_input();
    input.usb_words.gotgctl = 0x000D0000;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.gotgctl: unknown"));
    CHECK(has_line("usb.b_session: unknown"));
    input.usb_words.core_known = true;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.gotgctl: 0x000d0000"));
    CHECK(has_line("usb.b_session: 1"));
    input.usb_words.gotgctl = 0x00010000;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.gotgctl: 0x00010000"));
    CHECK(has_line("usb.b_session: 0"));
}

static void the_last_usb_session_is_reported_once_there_was_one(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.sense_mode: normal"));
    CHECK(has_line("usb.last_end: none"));
    CHECK(has_line("usb.internal_sectors_written: unknown"));
    input.power.usb_kept_on = true;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.sense_mode: kept_on"));
    input.power.usb_kept_on = false;
    input.usb = (HalUsbLast){.end = HAL_USB_END_EJECTED, .internal_written = 96};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.last_end: ejected"));
    CHECK(has_line("usb.internal_sectors_written: 96"));
    CHECK(has_line("usb.card_sectors_written: 0"));
    CHECK(strstr(text, "usb.speed_fallback") == NULL);
}

static void the_last_usb_session_splits_its_write_time(void)
{
    DebugReportInput input = plain_input();
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, "usb.timing") == NULL);
    input.usb = (HalUsbLast){.end = HAL_USB_END_EJECTED,
                             .writes = {.commands = 812,
                                        .receive_ms = 9400,
                                        .card_ms = 2100,
                                        .idle_ms = 350,
                                        .fast_cards = 3,
                                        .fallbacks = 1}};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.timing: writes=812 receive_ms=9400 card_ms=2100 idle_ms=350 fast=3 "
                   "fallbacks=1"));
}

static void the_card_start_reports_detection_power_and_the_failing_command(void)
{
    DebugReportInput input = plain_input();
    input.card = (CardStartLog){.init_ms = 1043,
                                .last_response = 0,
                                .failed_interrupts = 0x104,
                                .failures = 101,
                                .timeouts = 100,
                                .attempts = 2,
                                .last_command = 41,
                                .failed_command = 41,
                                .detected = true,
                                .raw_now = true,
                                .present_now = true};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("card.start: raw1 present1 ejected0 ready0 detected1 powered0 tries2 ms1043 "
                   "last41=00000000 failed41=00000104 errors101 timeouts100"));
    input.card = (CardStartLog){.attempts = 1, .ready = true, .detected = true};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, "card.start") == NULL);
}

static void a_full_speed_fallback_reports_why_it_fired(void)
{
    DebugReportInput input = plain_input();
    input.usb =
        (HalUsbLast){.end = HAL_USB_END_EJECTED, .speed_fallback = 2, .fallback_commands = 1};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.speed_fallback: few_commands commands=1 setups=0"));
    input.usb.speed_fallback = 1;
    input.usb.fallback_commands = 0;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.speed_fallback: not_configured commands=0 setups=0"));
    for (uint8_t index = 0; index < 10; index++) {
        input.usb.fallback_setups.setups[index % HAL_USB_SETUP_TRACE] =
            (HalUsbSetup){0x80, 6, HAL_USB_SETUP_ANSWERED, (uint16_t)(0x0100 + index), 64};
    }
    input.usb.fallback_setups.seen = 10;
    input.usb.fallback_setups.setups[9 % HAL_USB_SETUP_TRACE] =
        (HalUsbSetup){0x80, 6, HAL_USB_SETUP_STALLED, 0x0600, 10};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("usb.speed_fallback: not_configured commands=0 setups=10 80.06.0102/64a "
                   "80.06.0103/64a 80.06.0104/64a 80.06.0105/64a 80.06.0106/64a 80.06.0107/64a "
                   "80.06.0108/64a 80.06.0600/10s"));
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
    record(DEBUG_EVENT_KEY, KEY_PLAY | KEY_GESTURE_LONG_PRESS << 8 | 377u << 16, NULL);
    record(DEBUG_EVENT_SCREEN, 2, "Now Playing");
    record(DEBUG_EVENT_PLAYER, 1 | 17 << 8, NULL);
    record(DEBUG_EVENT_UNDERRUN, 3, NULL);
    record(DEBUG_EVENT_USB_END, 1, NULL);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("events.recorded: 5"));
    CHECK(has_line("events.dropped: 0"));
    CHECK(has_line("event: 1000 key play long 377"));
    CHECK(has_line("event: 1001 screen 2 Now Pla"));
    CHECK(has_line("event: 1002 player playing 17"));
    CHECK(has_line("event: 1003 underrun 3"));
    CHECK(has_line("event: 1004 usb_end ejected"));
    CHECK(strstr(text, "event: 1000") < strstr(text, "event: 1004"));
}

static void a_paused_player_with_its_output_powered_down_is_named_standby(void)
{
    DebugReportInput input = plain_input();
    record(DEBUG_EVENT_PLAYER, 2 | 17 << 8, NULL);
    record(DEBUG_EVENT_PLAYER, DEBUG_PLAYER_STANDBY | 17 << 8, NULL);
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("event: 1000 player paused 17"));
    CHECK(has_line("event: 1001 player standby 17"));
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
    buckets[0x1a3] = 7;
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(has_line("profile.row: 0 1 1f"));
    CHECK(has_line("profile.row: 1a0 fffe 0 0 7"));
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
        power_history_record(&history, (PowerSample){65535, 4200, 100, 31});
    }
    for (int index = 0; index < DEBUG_LOG_CAPACITY + 3; index++) {
        record(DEBUG_EVENT_OUTPUT_UNDERRUN, 0xFFFFFFFFu, "WWWWWWW");
        events.events[index % DEBUG_LOG_CAPACITY].time_ms = 0xFFFFFFFFu;
    }
}

static void fill_script(void)
{
    static char full[DEBUG_SCRIPT_BYTES + 1];
    strcpy(full, "build any\n");
    for (int index = 0; index < DEBUG_SCRIPT_SCREENSHOTS; index++) {
        strcat(full, "screenshot abcdefghijkl\nmeasure 60000\n");
    }
    while (strlen(full) + 18 <= DEBUG_SCRIPT_BYTES) {
        strcat(full, "set Brightness=10\n");
    }
    load_script(full);
    script.state = DEBUG_SCRIPT_FINISHED;
    script.started_ms = 0xFFFFFFFFu;
    script.finished_ms = 0xFFFFFFFFu;
    for (int index = 0; index < DEBUG_SCRIPT_SCREENSHOTS; index++) {
        script.screenshot_lines[script.screenshot_count++] = (uint8_t)(2 + 2 * index);
        memset(&script.measures[script.measure_count], 0x7F, sizeof(DebugMeasure));
        script.measures[script.measure_count++].audio_running = 1;
    }
    for (int index = 0; index < DEBUG_SCRIPT_SOAKS; index++) {
        memset(&script.soaks[script.soak_count], 0xFF, sizeof(DebugSoak));
        script.soaks[script.soak_count++].end = DEBUG_SOAK_LOW_BATTERY;
    }
}

static const char *last_body_line(void)
{
    const char *tail = strstr(text, "\ntruncated: ");
    const char *line = tail;
    while (line > text && line[-1] != '\n') {
        line--;
    }
    return line;
}

static void a_full_report_with_a_busy_histogram_fits_the_file(void)
{
    DebugReportInput input = plugged_in_at_97_percent();
    fill_everything(0);
    fill_script();
    for (int index = 0; index < BUSY_BUCKETS; index++) {
        buckets[index] = 0xfffe;
    }
    for (int index = BUSY_BUCKETS; index < BUCKETS; index += 8) {
        buckets[index] = 0xfffe;
    }
    uint32_t length = format(&input, DEBUG_REPORT_BYTES);
    CHECK(length <= DEBUG_REPORT_BYTES);
    CHECK(has_line("truncated: 0"));
    CHECK(has_line("script.screenshot: 3 abcdefghijkl"));
    CHECK(strstr(text, "\nscript.soak: line=255 minutes=0 end=low_battery ") != NULL);
    CHECK(strstr(text, "; 65535 4200 100 11111\n") != NULL);
    CHECK(has_line("profile.row: 4c0 fffe 0 0 0 0 0 0 0 fffe"));
}

static void the_fullest_report_is_cut_inside_the_histogram_which_comes_last(void)
{
    DebugReportInput input = plugged_in_at_97_percent();
    fill_everything(0xfffe);
    fill_script();
    uint32_t length = format(&input, DEBUG_REPORT_BYTES);
    CHECK(length <= DEBUG_REPORT_BYTES);
    CHECK(has_line("truncated: 1"));
    CHECK(has_line("script.screenshot: 3 abcdefghijkl"));
    CHECK(strstr(text, "\nscript.measure: line=2139062143 ") != NULL);
    CHECK(strstr(text, "; 65535 4200 100 11111\n") != NULL);
    CHECK(has_line("profile.buckets: 1229"));
    CHECK_EQUAL(0, strncmp(last_body_line(), "profile.row: ", 13));
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

static void bluetooth_timeline_is_reported_in_milliseconds(void)
{
    DebugReportInput input = plain_input();
    HalBluetoothLink link = {.timeline = {.module_on_ms = 100,
                                          .init_done_ms = 402,
                                          .connect_ms = 403,
                                          .connected_ms = 1903,
                                          .stream_ms = 1905,
                                          .first_audio_ms = 1906}};
    input.bluetooth = &link;
    debug_report_format(&input, text, sizeof text);
    CHECK(strstr(text, "bluetooth.module_on_ms: 100\n") != NULL);
    CHECK(strstr(text, "bluetooth.first_audio_ms: 1906\n") != NULL);
}

static void standby_reports_the_drain_window_and_wake_latency(void)
{
    DebugReportInput input = plain_input();
    input.standby = (Standby){.entries = 3,
                              .wakes = 2,
                              .entry_ms = 1000,
                              .wake_ms = 3601000,
                              .entry_mv = 4000,
                              .wake_mv = 3990,
                              .latency_ms = 125};
    format(&input, DEBUG_REPORT_BYTES);
    CHECK(strstr(text, "standby.entries: 3\n") != NULL);
    CHECK(strstr(text, "standby.wakes: 2\n") != NULL);
    CHECK(strstr(text, "standby.wake_latency_ms: 125\n") != NULL);
    CHECK(strstr(text, "standby.entry_mv: 4000\n") != NULL);
    CHECK(strstr(text, "standby.wake_mv: 3990\n") != NULL);
    CHECK(strstr(text, "standby.wake_ms: 3601000\n") != NULL);
}

static void unknown_enum_values_are_reported_without_indexing_past_their_names(void)
{
    DebugReportInput input = plain_input();
    input.reason = (DebugReportReason)UINT32_MAX;
    input.start.warm = true;
    input.start.last_step = (HalStartStep)UINT32_MAX;
    input.usb.end = (HalUsbEnd)UINT32_MAX;
    input.loop.last_connect = UINT8_MAX;
    input.audio.state = (HalAudioState)UINT32_MAX;
    load_script("build any\nrtc read\nsoak 1\n");
    script.state = UINT8_MAX;
    script.status[0] = UINT8_MAX;
    script.soak_count = 1;
    script.soaks[0].end = UINT8_MAX;
    script.rtc_count = 1;
    script.rtc[0].action = UINT8_MAX;
    script.rtc[0].status = UINT8_MAX;
    record(DEBUG_EVENT_KEY, KEY_PLAY | UINT32_C(0xff) << 8, NULL);
    record(DEBUG_EVENT_PLAYER, UINT32_C(0xff), NULL);
    record(DEBUG_EVENT_USB_END, UINT32_MAX, NULL);
    record(DEBUG_EVENT_SCRIPT, DEBUG_SCRIPT_EVENT_STATE + UINT32_C(0xff), NULL);
    record(DEBUG_EVENT_USB_CONNECT, UINT32_MAX, NULL);

    format(&input, DEBUG_REPORT_BYTES);

    CHECK(has_line("report.reason: unknown"));
    CHECK(has_line("start.last_step: unknown"));
    CHECK(has_line("usb.last_end: unknown"));
    CHECK(has_line("loop.last_connect: unknown"));
    CHECK(has_line("audio.state: unknown"));
    CHECK(has_line("script.state: unknown"));
    CHECK(has_line("script.line: 1 unknown build any"));
    CHECK(strstr(text, " end=unknown ") != NULL);
    CHECK(strstr(text, " action=unknown ") != NULL);
    CHECK(strstr(text, " status=unknown ") != NULL);
    CHECK(has_line("event: 1000 key play unknown 0"));
    CHECK(has_line("event: 1001 player unknown 0"));
    CHECK(has_line("event: 1002 usb_end unknown"));
    CHECK(has_line("event: 1003 script unknown"));
    CHECK(has_line("event: 1004 usb_connect unknown"));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(bluetooth_timeline_is_reported_in_milliseconds),
        TEST_CASE(the_card_start_reports_detection_power_and_the_failing_command),
        TEST_CASE(dac_diagnostics_report_cached_reads_and_sequence_durations),
        TEST_CASE(standby_reports_the_drain_window_and_wake_latency),
        TEST_CASE(the_schema_version_is_the_first_line),
        TEST_CASE(missing_boot_markers_are_reported_as_unknown),
        TEST_CASE(boot_markers_keep_zero_and_completed_phases_separate_from_unknown),
        TEST_CASE(the_report_says_which_one_it_is_and_what_wrote_it),
        TEST_CASE(a_report_in_the_first_seconds_of_a_cold_start_says_so),
        TEST_CASE(before_the_gauge_has_a_reading_the_start_up_sample_stands_in_and_is_labelled),
        TEST_CASE(with_gauge_readings_the_battery_lines_are_the_gauges),
        TEST_CASE(the_head_of_a_report_gives_its_sequence_and_cold_start_flag),
        TEST_CASE(a_file_that_is_not_a_finished_report_is_not_a_valid_slot),
        TEST_CASE(the_first_reports_fill_the_empty_slots_in_turn),
        TEST_CASE(a_new_report_replaces_the_oldest_and_counts_on_from_the_newest),
        TEST_CASE(a_cold_start_report_replaces_the_cold_start_report_before_it),
        TEST_CASE(an_older_cold_start_report_is_not_what_a_new_one_replaces),
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
        TEST_CASE(without_a_script_the_section_says_none),
        TEST_CASE(a_finished_script_lists_each_line_with_its_outcome),
        TEST_CASE(a_refused_script_says_why_and_shows_its_lines_as_not_run),
        TEST_CASE(a_running_script_has_no_finish_time),
        TEST_CASE(a_script_cut_short_by_a_reset_names_the_line_it_was_on),
        TEST_CASE(script_text_outside_plain_ascii_is_replaced),
        TEST_CASE(rtc_reads_list_every_register_the_decoded_time_and_the_trust_flag),
        TEST_CASE(a_measure_is_one_line_of_named_figures),
        TEST_CASE(a_measure_without_frames_or_audio_reports_them_as_unknown),
        TEST_CASE(a_measure_reports_core_1_load_from_its_idle_cycles),
        TEST_CASE(a_measure_with_more_core_1_idle_than_elapsed_reports_unknown_load),
        TEST_CASE(screenshots_are_listed_by_slot_with_their_names),
        TEST_CASE(probes_are_listed_with_both_words_the_time_and_the_two_levels),
        TEST_CASE(a_soak_is_reported_with_its_voltage_slope_its_load_and_both_ends),
        TEST_CASE(a_rising_battery_gives_a_positive_slope),
        TEST_CASE(a_soak_with_fewer_than_four_samples_has_means_but_no_slope),
        TEST_CASE(a_soak_whose_first_samples_left_the_history_gives_no_voltages),
        TEST_CASE(a_soak_that_was_cut_short_says_why),
        TEST_CASE(two_soaks_are_two_lines),
        TEST_CASE(script_events_name_the_line_or_the_state),
        TEST_CASE(the_loop_section_says_how_this_run_started),
        TEST_CASE(the_loop_section_counts_sessions_tried_without_the_cable_bit),
        TEST_CASE(connect_events_say_what_became_of_the_attempt),
        TEST_CASE(scan_and_cover_events_name_their_phase_and_size),
        TEST_CASE(emmc_starts_and_a_lost_start_flag_name_their_phase),
        TEST_CASE(a_cold_start_has_no_previous_run_to_report),
        TEST_CASE(a_warm_start_reports_the_previous_run_and_its_fault),
        TEST_CASE(the_usb_words_read_at_start_up_and_at_the_report_are_printed_whole),
        TEST_CASE(the_cores_session_bit_is_reported_only_while_the_controller_can_be_read),
        TEST_CASE(the_last_usb_session_is_reported_once_there_was_one),
        TEST_CASE(the_last_usb_session_splits_its_write_time),
        TEST_CASE(a_full_speed_fallback_reports_why_it_fired),
        TEST_CASE(stopped_audio_reports_its_running_figures_as_unknown),
        TEST_CASE(running_audio_reports_averages_worst_times_and_the_busy_share),
        TEST_CASE(frame_figures_are_unknown_until_a_frame_was_drawn),
        TEST_CASE(events_are_listed_oldest_first_with_names),
        TEST_CASE(a_paused_player_with_its_output_powered_down_is_named_standby),
        TEST_CASE(event_text_outside_plain_ascii_is_replaced),
        TEST_CASE(an_unknown_event_kind_is_named_as_such),
        TEST_CASE(missing_sources_are_reported_as_unknown),
        TEST_CASE(the_histogram_lists_runs_of_used_buckets_in_hex),
        TEST_CASE(a_long_run_of_buckets_breaks_into_rows),
        TEST_CASE(a_full_profile_says_so),
        TEST_CASE(the_end_line_gives_the_length_of_the_body),
        TEST_CASE(the_rest_of_the_file_is_newlines_and_nothing_is_written_past_it),
        TEST_CASE(a_full_report_with_a_busy_histogram_fits_the_file),
        TEST_CASE(the_fullest_report_is_cut_inside_the_histogram_which_comes_last),
        TEST_CASE(a_small_buffer_cuts_at_a_line_and_says_so),
        TEST_CASE(a_buffer_too_small_for_the_end_lines_gets_nothing),
        TEST_CASE(unknown_enum_values_are_reported_without_indexing_past_their_names),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
