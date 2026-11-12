#include <string.h>

#include "check.h"
#include "default_theme.h"
#include "diagnostics_rows.h"
#include "diagnostics_values.h"
#include "fake_hal.h"
#include "gfx/text.h"
#include "ui/label.h"

static bool text_is(const char *expected, const char *actual)
{
    return strcmp(expected, actual) == 0;
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

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_headset_row_shows_the_level_and_the_button_it_reads_as),
        TEST_CASE(the_headset_row_holds_the_last_dip_with_its_length_and_count),
        TEST_CASE(the_headset_row_says_when_the_plug_has_no_mic),
        TEST_CASE(the_cpu_row_shows_the_clock_alone_until_a_second_of_audio_passed),
        TEST_CASE(the_cpu_row_shows_the_audio_share_of_the_last_second),
        TEST_CASE(the_busy_row_shows_dashes_then_the_share_of_the_last_second),
        TEST_CASE(the_cpu_row_drops_the_share_when_audio_restarts),
        TEST_CASE(a_jack_row_shows_the_live_level_and_how_the_rom_left_the_pin),
        TEST_CASE(the_battery_row_shows_millivolts_and_the_charger_state),
        TEST_CASE(the_clock_row_steps_up_from_the_current_speed_then_wraps),
        TEST_CASE(the_clock_row_shows_the_actual_speed),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
