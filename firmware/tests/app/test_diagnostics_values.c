#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "diagnostics_rows.h"
#include "diagnostics_values.h"
#include "fake_hal.h"
#include "gfx/text.h"
#include "ui/label.h"

enum {
    DAC_ID_DIGITS = 6
};

static const char *const DAC_LABELS[DAC_IDENTITY_CHIPS] = {"DAC A", "DAC B"};
static const DacIdentity DAC_WORDS[] = {
    {.source = DAC_IDENTITY_NOT_READ},
    {.source = DAC_IDENTITY_SIMULATED},
    {.source = DAC_IDENTITY_READ, .read_mask = 0},
};
static const DacIdentity DAC_KNOWN[] = {
    {DAC_IDENTITY_READ, DAC_IDENTITY_ALL_READ, {0x43, 0x19, 0x80, 0xA0, 0x00}},
    {DAC_IDENTITY_READ, DAC_IDENTITY_ALL_READ, {0x43, 0x13, 0x10, 0xB1, 0x07}},
    {DAC_IDENTITY_READ, 0x0B, {0x43, 0x19, 0x00, 0xA0, 0x00}},
};
static const DacIdentity DAC_WIDEST = {
    DAC_IDENTITY_READ, DAC_IDENTITY_ALL_READ, {0xDD, 0xDD, 0xDD, 0xDD, 0xDD}};

static bool text_is(const char *expected, const char *actual)
{
    return strcmp(expected, actual) == 0;
}

static Theme dac_theme(int size, Orientation orientation)
{
    Theme theme = BOARD_THEME;
    default_theme_set_text_size(&theme, (TextSize)size);
    default_theme_set_orientation(&theme, orientation);
    return theme;
}

static int dac_value_room(const Theme *theme, int chip)
{
    const ThemeMetrics *metrics = &theme->metrics;
    int row = metrics->screen_width - 2 * metrics->padding - metrics->scrollbar_width -
              metrics->scrollbar_inset;
    return row - text_width(theme->fonts.body, DAC_LABELS[chip]) - metrics->detail_gap;
}

static LabelFit dac_value_fit(const Theme *theme, int chip, DacIdentity identity)
{
    const Font *font = theme_is_portrait(theme) ? theme->fonts.small : theme->fonts.body;
    fake_hal_audio_set_dac_identity(chip, identity);
    const char *value = chip == 0 ? diagnostics_row_dac_a(NULL) : diagnostics_row_dac_b(NULL);
    return label_fit(font, value, dac_value_room(theme, chip));
}

static bool dac_values_fit(const Theme *theme, const DacIdentity *identities, size_t count)
{
    for (size_t index = 0; index < count * DAC_IDENTITY_CHIPS; index++) {
        int chip = (int)(index % DAC_IDENTITY_CHIPS);
        if (dac_value_fit(theme, chip, identities[index / DAC_IDENTITY_CHIPS]).ellipsis) {
            return false;
        }
    }
    return true;
}

static void the_dac_rows_show_what_the_hal_read_from_each_chip(void)
{
    fake_hal_reset();
    fake_hal_audio_set_dac_identity(0, DAC_KNOWN[0]);
    fake_hal_audio_set_dac_identity(1, DAC_KNOWN[1]);
    CHECK(text_is("431980 A0.00", diagnostics_row_dac_a(NULL)));
    CHECK(text_is("431310 B1.07", diagnostics_row_dac_b(NULL)));
    CHECK(text_is("431980 A0.00", diagnostics_row_dac_a(NULL)));
}

static void the_dac_rows_ask_for_a_track_until_the_dac_has_started(void)
{
    fake_hal_reset();
    CHECK(text_is("Play a track", diagnostics_row_dac_a(NULL)));
    CHECK(text_is("Play a track", diagnostics_row_dac_b(NULL)));
}

static void the_dac_rows_fit_in_landscape_at_every_text_size_whatever_was_read(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = dac_theme(size, ORIENTATION_LANDSCAPE);
        CHECK(dac_values_fit(&theme, DAC_WORDS, sizeof DAC_WORDS / sizeof DAC_WORDS[0]));
        CHECK(dac_values_fit(&theme, DAC_KNOWN, sizeof DAC_KNOWN / sizeof DAC_KNOWN[0]));
        CHECK(dac_values_fit(&theme, &DAC_WIDEST, 1));
    }
}

static void the_dac_rows_fit_in_portrait_at_small_and_medium_text(void)
{
    for (int size = TEXT_SIZE_SMALL; size <= TEXT_SIZE_MEDIUM; size++) {
        Theme theme = dac_theme(size, ORIENTATION_PORTRAIT);
        CHECK(dac_values_fit(&theme, DAC_WORDS, sizeof DAC_WORDS / sizeof DAC_WORDS[0]));
        CHECK(dac_values_fit(&theme, DAC_KNOWN, sizeof DAC_KNOWN / sizeof DAC_KNOWN[0]));
    }
}

static void large_text_in_portrait_fits_the_words_and_cuts_only_after_the_revision(void)
{
    Theme theme = dac_theme(TEXT_SIZE_LARGE, ORIENTATION_PORTRAIT);
    CHECK(dac_values_fit(&theme, DAC_WORDS, sizeof DAC_WORDS / sizeof DAC_WORDS[0]));
    for (int chip = 0; chip < DAC_IDENTITY_CHIPS; chip++) {
        CHECK(dac_value_fit(&theme, chip, DAC_KNOWN[0]).byte_count >= sizeof "431980 A0" - 1);
        CHECK(dac_value_fit(&theme, chip, DAC_KNOWN[1]).byte_count >= sizeof "431310 B1" - 1);
    }
}

static void the_id_bytes_of_an_unknown_part_are_never_cut(void)
{
    for (int size = 0; size < TEXT_SIZE_COUNT; size++) {
        Theme theme = dac_theme(size, ORIENTATION_PORTRAIT);
        for (int chip = 0; chip < DAC_IDENTITY_CHIPS; chip++) {
            CHECK(dac_value_fit(&theme, chip, DAC_WIDEST).byte_count > DAC_ID_DIGITS);
        }
    }
}

static void an_untried_tone_shows_nothing(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    DmaToneView view = {.tried = false};
    diagnostics_dma_tone_text(view, text, sizeof text);
    CHECK(text_is("", text));
}

static void a_running_tone_shows_the_read_position(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    DmaToneView view = {
        .tried = true, .result = HAL_TONE_PLAYED, .status = {.running = true, .position = 1234}};
    diagnostics_dma_tone_text(view, text, sizeof text);
    CHECK(text_is("Playing 1234", text));
}

static void a_tone_that_ended_by_itself_says_so(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    DmaToneView view = {.tried = true, .result = HAL_TONE_PLAYED, .status = {.running = false}};
    diagnostics_dma_tone_text(view, text, sizeof text);
    CHECK(text_is("Ended", text));
    view.stopped = true;
    diagnostics_dma_tone_text(view, text, sizeof text);
    CHECK(text_is("Stopped", text));
}

static void a_failed_start_names_the_step(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    DmaToneView view = {.tried = true, .result = HAL_TONE_NO_DMAC};
    diagnostics_dma_tone_text(view, text, sizeof text);
    CHECK(text_is("No DMAC", text));
    view.result = HAL_TONE_NO_DAC;
    diagnostics_dma_tone_text(view, text, sizeof text);
    CHECK(text_is("No DAC", text));
}

static void a_missing_dac_names_the_i2c_step_and_status(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_tone_text(HAL_TONE_NO_DAC, (HalToneDetail){.stage = 3, .pending = 0x48}, text,
                          sizeof text);
    CHECK(text_is("No DAC s3 48", text));
    diagnostics_tone_text(HAL_TONE_PLAYED, (HalToneDetail){.stage = 0, .pending = 0}, text,
                          sizeof text);
    CHECK(text_is("Played", text));
}

static void boot_clocks_show_the_low_half_of_each_register(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    HalBootClocks clocks = {
        .mode = 0x00200001u, .clock_select = 0x00001023u, .pclk_select = 0x0100u};
    diagnostics_boot_clocks_text(clocks, text, sizeof text);
    CHECK(text_is("0001 1023 0100", text));
}

static void usb_registers_show_two_whole_words(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_usb_registers_text(true, 0x0FE80488u, 0x228DDCD0u, text, sizeof text);
    CHECK(text_is("0FE80488 228DDCD0", text));
}

static void usb_registers_ask_for_a_session_until_captured(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_usb_registers_text(false, 1, 2, text, sizeof text);
    CHECK(text_is("Plug in USB first", text));
}

static void the_last_usb_row_waits_for_a_session(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_usb_last_text((HalUsbLast){.end = HAL_USB_END_NONE}, text, sizeof text);
    CHECK(text_is("No session yet", text));
}

static void the_last_usb_row_shows_a_clean_eject_and_the_sectors_written(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    HalUsbLast last = {.end = HAL_USB_END_EJECTED, .internal_written = 12, .card_written = 3};
    diagnostics_usb_last_text(last, text, sizeof text);
    CHECK(text_is("Ejected W12/3", text));
}

static void the_last_usb_row_counts_drives_left_mounted_at_unplug(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    HalUsbLast last = {.end = HAL_USB_END_UNPLUGGED, .left_mounted = 3, .internal_written = 1};
    diagnostics_usb_last_text(last, text, sizeof text);
    CHECK(text_is("Unplugged 2 up W1/0", text));
}

static void the_last_usb_row_flags_writes_after_an_eject(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    HalUsbLast last = {.end = HAL_USB_END_EJECTED, .writes_after_eject = 2};
    diagnostics_usb_last_text(last, text, sizeof text);
    CHECK(text_is("Ejected W0/0 A2", text));
}

static void the_last_usb_row_says_when_no_computer_answered(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_usb_last_text((HalUsbLast){.end = HAL_USB_END_NO_HOST}, text, sizeof text);
    CHECK(text_is("No computer", text));
}

static void the_boot_pll_shows_its_fields_and_core_speed(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    HalBootClocks clocks = {.pll_con0 = 0xFFFF1078u, .pll_con1 = 0x1546u, .core_hz = 96000000u};
    diagnostics_boot_pll_text(clocks, text, sizeof text);
    CHECK(text_is("1078 1546 96 MHz", text));
}

static void the_clock_row_steps_up_from_the_current_speed_then_wraps(void)
{
    CHECK_EQUAL(96u, diagnostics_next_clock_mhz(48));
    CHECK_EQUAL(96u, diagnostics_next_clock_mhz(24));
    CHECK_EQUAL(200u, diagnostics_next_clock_mhz(96));
    CHECK_EQUAL(24u, diagnostics_next_clock_mhz(200));
}

static void the_clock_row_shows_the_actual_speed(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_clock_text(48, false, text, sizeof text);
    CHECK(text_is("48 MHz", text));
    diagnostics_clock_text(48, true, text, sizeof text);
    CHECK(text_is("No lock", text));
}

static void the_loop_row_is_empty_until_run(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_loop_text(false, (HalLoopCycles){0}, text, sizeof text);
    CHECK(text_is("", text));
}

static void the_loop_row_shows_cycles_per_iteration(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_loop_text(true, (HalLoopCycles){.register_centi = 302, .memory_centi = 1507}, text,
                          sizeof text);
    CHECK(text_is("r3.02 m15.07", text));
    diagnostics_loop_text(true, (HalLoopCycles){.register_centi = 400, .memory_centi = 5}, text,
                          sizeof text);
    CHECK(text_is("r4.00 m0.05", text));
}

static void the_core_1_row_is_empty_until_run(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_core1_text(false, (HalCore1Test){0}, text, sizeof text);
    CHECK(text_is("", text));
}

static void a_core_1_run_shows_its_clock_and_loop_cycles(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    HalCore1Test test = {.status = HAL_CORE1_RAN,
                         .mhz = 100,
                         .cycles = {.register_centi = 300, .memory_centi = 912}};
    diagnostics_core1_text(true, test, text, sizeof text);
    CHECK(text_is("100 r3.00 m9.12", text));
}

static void core_1_failures_name_the_step(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    static const struct {
        HalCore1Status status;
        const char *text;
    } cases[] = {
        {HAL_CORE1_UNAVAILABLE, "Not here"}, {HAL_CORE1_NEEDS_PLL, "Needs 96+ MHz"},
        {HAL_CORE1_NO_POWER, "No power"},    {HAL_CORE1_NO_REPLY, "No reply"},
        {HAL_CORE1_BAD_REPLY, "Bad reply"},  {HAL_CORE1_NO_STOP, "No stop"},
        {HAL_CORE1_BUSY, "Busy with music"},
    };
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        diagnostics_core1_text(true, (HalCore1Test){.status = cases[index].status}, text,
                               sizeof text);
        CHECK(text_is(cases[index].text, text));
    }
}

static void the_accelerator_row_shows_both_classes_and_the_timing(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_core1_accel_text(false, (HalCore1Accel){0}, text, sizeof text);
    CHECK(text_is("", text));
    diagnostics_core1_accel_text(true,
                                 (HalCore1Accel){.status = HAL_CORE1_RAN,
                                                 .narrow_ok = true,
                                                 .wide_ok = true,
                                                 .hardware_us = 1230,
                                                 .software_us = 9760},
                                 text, sizeof text);
    CHECK(text_is("N ok W ok 1.2/9.8 ms", text));
    diagnostics_core1_accel_text(
        true,
        (HalCore1Accel){
            .status = HAL_CORE1_RAN, .narrow_ok = true, .failed_vector = 9, .failed_sample = 123},
        text, sizeof text);
    CHECK(text_is("N ok W no t9@123", text));
    diagnostics_core1_accel_text(
        true, (HalCore1Accel){.status = HAL_CORE1_RAN, .timed_out = true, .failed_vector = 5}, text,
        sizeof text);
    CHECK(text_is("Timeout at t5", text));
    diagnostics_core1_accel_text(true, (HalCore1Accel){.status = HAL_CORE1_NO_REPLY}, text,
                                 sizeof text);
    CHECK(text_is("No reply", text));
}

static void the_battery_row_shows_millivolts_and_the_charger_state(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_battery_text((HalBattery){0}, text, sizeof text);
    CHECK(text_is("No reading", text));
    HalBattery battery = {.known = true, .percent = 50, .millivolts = 3803};
    diagnostics_battery_text(battery, text, sizeof text);
    CHECK(text_is("3803 mV", text));
    battery.usb_power = true;
    battery.charging = true;
    diagnostics_battery_text(battery, text, sizeof text);
    CHECK(text_is("3803 mV charging", text));
    battery.charging = false;
    diagnostics_battery_text(battery, text, sizeof text);
    CHECK(text_is("3803 mV full", text));
}

static void the_boot_hold_row_shows_how_the_rom_left_the_pin(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_boot_hold_text((HalPowerHold){.function = 0, .output = false, .high = true}, text,
                               sizeof text);
    CHECK(text_is("fn0 input high", text));
    diagnostics_boot_hold_text((HalPowerHold){.function = 2, .output = true, .high = false}, text,
                               sizeof text);
    CHECK(text_is("fn2 output low", text));
}

static void a_jack_row_shows_the_live_level_and_how_the_rom_left_the_pin(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_jack_pin_text(
        (HalJackPin){.high = true, .boot_function = 0, .boot_output = false, .pull = true}, text,
        sizeof text);
    CHECK(text_is("high, boot fn0 in pull", text));
    diagnostics_jack_pin_text(
        (HalJackPin){.high = false, .boot_function = 1, .boot_output = true, .pull = false}, text,
        sizeof text);
    CHECK(text_is("low, boot fn1 out", text));
}

static void the_headset_row_shows_the_level_and_the_button_it_reads_as(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_headset_text((HalHeadsetReading){.known = false}, text, sizeof text);
    CHECK(text_is("Not read", text));
    diagnostics_headset_text(
        (HalHeadsetReading){.known = true, .level = 812, .button = HAL_HEADSET_BUTTON_NONE}, text,
        sizeof text);
    CHECK(text_is("812 none", text));
    diagnostics_headset_text(
        (HalHeadsetReading){.known = true, .level = 30, .button = HAL_HEADSET_BUTTON_VOLUME_UP},
        text, sizeof text);
    CHECK(text_is("30 vol+", text));
}

static void the_headset_row_holds_the_last_dip_with_its_length_and_count(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    HalHeadsetReading reading = {.known = true,
                                 .level = 1023,
                                 .button = HAL_HEADSET_BUTTON_NONE,
                                 .dipped = true,
                                 .lowest = 512,
                                 .dip_ms = 40,
                                 .dips = 3};
    diagnostics_headset_text(reading, text, sizeof text);
    CHECK(text_is("1023 min 512 40ms 3 dips", text));
    reading.dips = 1;
    diagnostics_headset_text(reading, text, sizeof text);
    CHECK(text_is("1023 min 512 40ms 1 dip", text));
}

static void the_cpu_row_shows_the_clock_alone_until_a_second_of_audio_passed(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    LoadWindow window = {.started = false};
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 0, .elapsed_us = 0});
    diagnostics_cpu_text(200, &window, text, sizeof text);
    CHECK(text_is("200 MHz", text));
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 1000, .elapsed_us = 10000});
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 200000, .elapsed_us = 900000});
    diagnostics_cpu_text(200, &window, text, sizeof text);
    CHECK(text_is("200 MHz", text));
}

static void the_cpu_row_shows_the_audio_share_of_the_last_second(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    LoadWindow window = {.started = false};
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 50000, .elapsed_us = 100000});
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 360000, .elapsed_us = 1100000});
    diagnostics_cpu_text(200, &window, text, sizeof text);
    CHECK(text_is("200 MHz 31%", text));
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 460000, .elapsed_us = 2100000});
    diagnostics_cpu_text(96, &window, text, sizeof text);
    CHECK(text_is("96 MHz 10%", text));
}

static void the_busy_row_shows_dashes_then_the_share_of_the_last_second(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    LoadWindow window = {.started = false};
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 50000, .elapsed_us = 100000});
    diagnostics_busy_text(&window, text, sizeof text);
    CHECK(text_is("--", text));
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 300050, .elapsed_us = 1100000});
    diagnostics_busy_text(&window, text, sizeof text);
    CHECK(text_is("25%", text));
}

static void the_cpu_row_drops_the_share_when_audio_restarts(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    LoadWindow window = {.started = false};
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 0, .elapsed_us = 100000});
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 300000, .elapsed_us = 1100000});
    diagnostics_load_update(&window, (HalAudioStats){.busy_us = 0, .elapsed_us = 0});
    diagnostics_cpu_text(24, &window, text, sizeof text);
    CHECK(text_is("24 MHz", text));
}

static void the_headset_row_says_when_the_plug_has_no_mic(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    HalHeadsetReading reading = {
        .known = true, .level = 0, .button = HAL_HEADSET_BUTTON_PLAY, .no_mic = true};
    diagnostics_headset_text(reading, text, sizeof text);
    CHECK(text_is("0 no mic", text));
    reading.dipped = true;
    reading.lowest = 0;
    reading.dip_ms = 39242;
    reading.dips = 2;
    diagnostics_headset_text(reading, text, sizeof text);
    CHECK(text_is("0 no mic", text));
}

static void the_last_start_row_tells_cold_starts_resets_and_faults_apart(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_start_text((HalStartInfo){.warm = false, .starts = 1}, text, sizeof text);
    CHECK(text_is("Cold start", text));
    diagnostics_start_text((HalStartInfo){.warm = true, .starts = 3, .previous_uptime_ms = 61400},
                           text, sizeof text);
    CHECK(text_is("#3 reset after 61 s", text));
    diagnostics_start_text((HalStartInfo){.warm = true, .starts = 2, .previous_power_off = true},
                           text, sizeof text);
    CHECK(text_is("#2 after power-off", text));
    diagnostics_start_text(
        (HalStartInfo){
            .warm = true, .starts = 2, .fault_status = 0x00020000u, .fault_pc = 0x0305ABCDu},
        text, sizeof text);
    CHECK(text_is("Fault at 0305ABCD", text));
}

static void the_last_step_row_tells_an_unfinished_step_from_a_finished_one(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_last_step_text((HalStartInfo){.warm = false}, text, sizeof text);
    CHECK(text_is("None", text));
    diagnostics_last_step_text((HalStartInfo){.warm = true, .last_step = HAL_START_STEP_NONE}, text,
                               sizeof text);
    CHECK(text_is("None", text));
    diagnostics_last_step_text((HalStartInfo){.warm = true,
                                              .last_step = HAL_START_STEP_CLOCK_SWITCH,
                                              .last_step_ms = 41900},
                               text, sizeof text);
    CHECK(text_is("In clock switch at 41 s", text));
    diagnostics_last_step_text((HalStartInfo){.warm = true,
                                              .last_step = HAL_START_STEP_BACKLIGHT_OFF,
                                              .last_step_finished = true,
                                              .last_step_ms = 12345000},
                               text, sizeof text);
    CHECK(text_is("Backlight off at 12345 s", text));
}

static void the_last_battery_row_shows_the_reading_before_the_start(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_last_battery_text((HalStartInfo){.warm = false}, text, sizeof text);
    CHECK(text_is("No reading", text));
    diagnostics_last_battery_text((HalStartInfo){.warm = true, .last_millivolts = 3712}, text,
                                  sizeof text);
    CHECK(text_is("3712 mV", text));
}

static void the_high_ram_row_reports_each_outcome(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_high_ram_text(false, (HalHighRamTest){.status = HAL_HIGH_RAM_OK}, text,
                              sizeof text);
    CHECK(text_is("", text));
    diagnostics_high_ram_text(true, (HalHighRamTest){.status = HAL_HIGH_RAM_OK, .size = 151552},
                              text, sizeof text);
    CHECK(text_is("OK 148 KB, call works", text));
    diagnostics_high_ram_text(true, (HalHighRamTest){.status = HAL_HIGH_RAM_UNAVAILABLE}, text,
                              sizeof text);
    CHECK(text_is("Not on this player", text));
    diagnostics_high_ram_text(
        true, (HalHighRamTest){.status = HAL_HIGH_RAM_BAD_MEMORY, .bad_address = 0x0307A010u}, text,
        sizeof text);
    CHECK(text_is("Bad at 0307A010", text));
    diagnostics_high_ram_text(
        true, (HalHighRamTest){.status = HAL_HIGH_RAM_BAD_CALL, .call_result = 0x12u}, text,
        sizeof text);
    CHECK(text_is("Call gave 00000012", text));
}

static void the_watchdog_test_is_named_in_the_last_step_row(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_last_step_text((HalStartInfo){.warm = true,
                                              .last_step = HAL_START_STEP_WATCHDOG_TEST,
                                              .last_step_finished = false,
                                              .last_step_ms = 12000},
                               text, sizeof text);
    CHECK(strcmp("In watchdog test at 12 s", text) == 0);
}

static void the_usb_sense_row_shows_the_mode_the_cable_bit_and_the_power(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_usb_sense_text((HalUsbSense){false, false, false, false}, text, sizeof text);
    CHECK(text_is("Auto 0 off", text));
    diagnostics_usb_sense_text((HalUsbSense){false, true, false, false}, text, sizeof text);
    CHECK(text_is("Auto 1 off", text));
}

static void with_usb_powered_the_row_adds_the_cores_own_session_bit_after_the_cable_bit(void)
{
    char text[DIAGNOSTICS_VALUE_CAPACITY];
    diagnostics_usb_sense_text((HalUsbSense){false, true, true, true}, text, sizeof text);
    CHECK(text_is("Auto 1/1", text));
    diagnostics_usb_sense_text((HalUsbSense){true, false, true, true}, text, sizeof text);
    CHECK(text_is("Kept 0/1", text));
    diagnostics_usb_sense_text((HalUsbSense){true, false, true, false}, text, sizeof text);
    CHECK(text_is("Kept 0/0", text));
    diagnostics_usb_sense_text((HalUsbSense){true, true, true, false}, text, sizeof text);
    CHECK(text_is("Kept 1/0", text));
}

static void the_usb_sense_row_fits_the_narrowest_value_room_of_a_portrait_row(void)
{
    enum {
        NARROW_SHARE = 40,
        SCROLLBAR_ROOM = 8
    };
    Theme theme = BOARD_THEME;
    default_theme_set_orientation(&theme, ORIENTATION_PORTRAIT);
    int row = theme.metrics.screen_width - 2 * theme.metrics.padding - SCROLLBAR_ROOM;
    for (int state = 0; state < 16; state++) {
        char text[DIAGNOSTICS_VALUE_CAPACITY];
        HalUsbSense sense = {(state & 1) != 0, (state & 2) != 0, (state & 4) != 0,
                             (state & 8) != 0};
        diagnostics_usb_sense_text(sense, text, sizeof text);
        CHECK(text_width(theme.fonts.small, text) <= row * NARROW_SHARE / 100);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_watchdog_test_is_named_in_the_last_step_row),
        TEST_CASE(the_dac_rows_show_what_the_hal_read_from_each_chip),
        TEST_CASE(the_dac_rows_ask_for_a_track_until_the_dac_has_started),
        TEST_CASE(the_dac_rows_fit_in_landscape_at_every_text_size_whatever_was_read),
        TEST_CASE(the_dac_rows_fit_in_portrait_at_small_and_medium_text),
        TEST_CASE(large_text_in_portrait_fits_the_words_and_cuts_only_after_the_revision),
        TEST_CASE(the_id_bytes_of_an_unknown_part_are_never_cut),
        TEST_CASE(an_untried_tone_shows_nothing),
        TEST_CASE(the_loop_row_is_empty_until_run),
        TEST_CASE(the_headset_row_shows_the_level_and_the_button_it_reads_as),
        TEST_CASE(the_headset_row_holds_the_last_dip_with_its_length_and_count),
        TEST_CASE(the_headset_row_says_when_the_plug_has_no_mic),
        TEST_CASE(the_cpu_row_shows_the_clock_alone_until_a_second_of_audio_passed),
        TEST_CASE(the_cpu_row_shows_the_audio_share_of_the_last_second),
        TEST_CASE(the_busy_row_shows_dashes_then_the_share_of_the_last_second),
        TEST_CASE(the_cpu_row_drops_the_share_when_audio_restarts),
        TEST_CASE(the_last_start_row_tells_cold_starts_resets_and_faults_apart),
        TEST_CASE(the_last_step_row_tells_an_unfinished_step_from_a_finished_one),
        TEST_CASE(the_last_battery_row_shows_the_reading_before_the_start),
        TEST_CASE(the_high_ram_row_reports_each_outcome),
        TEST_CASE(the_boot_hold_row_shows_how_the_rom_left_the_pin),
        TEST_CASE(a_jack_row_shows_the_live_level_and_how_the_rom_left_the_pin),
        TEST_CASE(the_battery_row_shows_millivolts_and_the_charger_state),
        TEST_CASE(the_core_1_row_is_empty_until_run),
        TEST_CASE(a_core_1_run_shows_its_clock_and_loop_cycles),
        TEST_CASE(core_1_failures_name_the_step),
        TEST_CASE(the_accelerator_row_shows_both_classes_and_the_timing),
        TEST_CASE(the_loop_row_shows_cycles_per_iteration),
        TEST_CASE(a_running_tone_shows_the_read_position),
        TEST_CASE(a_tone_that_ended_by_itself_says_so),
        TEST_CASE(a_failed_start_names_the_step),
        TEST_CASE(a_missing_dac_names_the_i2c_step_and_status),
        TEST_CASE(boot_clocks_show_the_low_half_of_each_register),
        TEST_CASE(the_boot_pll_shows_its_fields_and_core_speed),
        TEST_CASE(usb_registers_show_two_whole_words),
        TEST_CASE(usb_registers_ask_for_a_session_until_captured),
        TEST_CASE(the_last_usb_row_waits_for_a_session),
        TEST_CASE(the_last_usb_row_shows_a_clean_eject_and_the_sectors_written),
        TEST_CASE(the_last_usb_row_counts_drives_left_mounted_at_unplug),
        TEST_CASE(the_last_usb_row_flags_writes_after_an_eject),
        TEST_CASE(the_last_usb_row_says_when_no_computer_answered),
        TEST_CASE(the_clock_row_steps_up_from_the_current_speed_then_wraps),
        TEST_CASE(the_clock_row_shows_the_actual_speed),
        TEST_CASE(the_usb_sense_row_shows_the_mode_the_cable_bit_and_the_power),
        TEST_CASE(with_usb_powered_the_row_adds_the_cores_own_session_bit_after_the_cable_bit),
        TEST_CASE(the_usb_sense_row_fits_the_narrowest_value_room_of_a_portrait_row),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
