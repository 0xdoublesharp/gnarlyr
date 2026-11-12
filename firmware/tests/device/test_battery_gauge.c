#include "check.h"
#include "device/battery_gauge.h"

static uint16_t raw_for(uint32_t millivolts)
{
    return (uint16_t)((millivolts * 1024u + 4999u) / 5000u);
}

static void readings_scale_to_millivolts_through_the_divider(void)
{
    CHECK_EQUAL(0u, battery_millivolts(0));
    CHECK_EQUAL(2500u, battery_millivolts(512));
    CHECK_EQUAL(4995u, battery_millivolts(1023));
}

static void the_curve_runs_from_empty_to_full(void)
{
    CHECK_EQUAL(0, battery_percent_for(3000));
    CHECK_EQUAL(0, battery_percent_for(3450));
    CHECK_EQUAL(50, battery_percent_for(3800));
    CHECK_EQUAL(100, battery_percent_for(4200));
    CHECK_EQUAL(100, battery_percent_for(4400));
}

static void the_curve_is_linear_between_points(void)
{
    CHECK_EQUAL(55, battery_percent_for(3825));
    CHECK_EQUAL(5, battery_percent_for(3515));
    CHECK_EQUAL(95, battery_percent_for(4150));
}

static void a_new_gauge_knows_nothing(void)
{
    BatteryGauge gauge;
    battery_gauge_init(&gauge);
    CHECK(!battery_gauge_known(&gauge));
}

static void one_reading_gives_a_level(void)
{
    BatteryGauge gauge;
    battery_gauge_init(&gauge);
    battery_gauge_add(&gauge, raw_for(3800));
    CHECK(battery_gauge_known(&gauge));
    CHECK(battery_gauge_millivolts(&gauge) >= 3795 && battery_gauge_millivolts(&gauge) <= 3805);
    CHECK_EQUAL(50, battery_gauge_percent(&gauge, false));
}

static void a_single_odd_reading_moves_the_level_only_a_little(void)
{
    BatteryGauge gauge;
    battery_gauge_init(&gauge);
    for (int reading = 0; reading < BATTERY_GAUGE_READINGS; reading++) {
        battery_gauge_add(&gauge, raw_for(3800));
    }
    battery_gauge_add(&gauge, raw_for(3450));
    CHECK(battery_gauge_millivolts(&gauge) > 3750);
}

static void charging_reads_50_mv_lower(void)
{
    BatteryGauge gauge;
    battery_gauge_init(&gauge);
    battery_gauge_add(&gauge, raw_for(3850));
    CHECK_EQUAL(50, battery_gauge_percent(&gauge, true));
}

static void charging_never_shows_full(void)
{
    BatteryGauge gauge;
    battery_gauge_init(&gauge);
    battery_gauge_add(&gauge, raw_for(4300));
    CHECK_EQUAL(BATTERY_CHARGING_MAX_PERCENT, battery_gauge_percent(&gauge, true));
    CHECK_EQUAL(100, battery_gauge_percent(&gauge, false));
}

static void a_restart_keeps_only_the_latest_reading(void)
{
    BatteryGauge gauge;
    battery_gauge_init(&gauge);
    for (int reading = 0; reading < BATTERY_GAUGE_READINGS + 3; reading++) {
        battery_gauge_add(&gauge, raw_for(4200));
    }
    battery_gauge_add(&gauge, raw_for(3900));
    battery_gauge_restart(&gauge);
    CHECK(battery_gauge_known(&gauge));
    CHECK(battery_gauge_millivolts(&gauge) >= 3895 && battery_gauge_millivolts(&gauge) <= 3905);
    battery_gauge_add(&gauge, raw_for(3700));
    CHECK(battery_gauge_millivolts(&gauge) >= 3795 && battery_gauge_millivolts(&gauge) <= 3805);
}

static void the_recent_level_follows_the_last_readings(void)
{
    BatteryGauge gauge;
    battery_gauge_init(&gauge);
    for (int reading = 0; reading < BATTERY_GAUGE_READINGS; reading++) {
        battery_gauge_add(&gauge, raw_for(3800));
    }
    for (int reading = 0; reading < BATTERY_RECENT_READINGS; reading++) {
        battery_gauge_add(&gauge, raw_for(3400));
    }
    CHECK(battery_gauge_recent_millivolts(&gauge) <= 3405);
    CHECK(battery_gauge_millivolts(&gauge) > 3700);
}

static void the_average_spans_several_seconds_of_readings(void)
{
    CHECK(BATTERY_GAUGE_READINGS >= 64);
}

static void the_shown_percent_starts_at_the_first_level(void)
{
    BatteryDisplay display;
    battery_display_init(&display);
    CHECK_EQUAL(34, battery_display_update(&display, 34, false));
}

static void a_one_point_wobble_keeps_the_shown_percent(void)
{
    BatteryDisplay display;
    battery_display_init(&display);
    battery_display_update(&display, 34, false);
    CHECK_EQUAL(34, battery_display_update(&display, 33, false));
    CHECK_EQUAL(34, battery_display_update(&display, 35, false));
    CHECK_EQUAL(34, battery_display_update(&display, 34, false));
}

static void on_battery_the_shown_percent_moves_after_a_three_point_change(void)
{
    BatteryDisplay display;
    battery_display_init(&display);
    battery_display_update(&display, 34, false);
    CHECK_EQUAL(34, battery_display_update(&display, 32, false));
    CHECK_EQUAL(31, battery_display_update(&display, 31, false));
    CHECK_EQUAL(31, battery_display_update(&display, 33, false));
    CHECK_EQUAL(34, battery_display_update(&display, 34, false));
}

static void on_battery_the_shown_percent_recovers_after_a_load_ends(void)
{
    BatteryDisplay display;
    battery_display_init(&display);
    battery_display_update(&display, 60, false);
    CHECK_EQUAL(52, battery_display_update(&display, 52, false));
    CHECK_EQUAL(52, battery_display_update(&display, 54, false));
    CHECK_EQUAL(59, battery_display_update(&display, 59, false));
}

static void on_battery_jitter_does_not_flap_the_shown_percent(void)
{
    BatteryDisplay display;
    battery_display_init(&display);
    battery_display_update(&display, 50, false);
    for (int round = 0; round < 4; round++) {
        CHECK_EQUAL(50, battery_display_update(&display, 48, false));
        CHECK_EQUAL(50, battery_display_update(&display, 52, false));
    }
}

static void on_usb_power_the_shown_percent_rises_freely_and_falls_reluctantly(void)
{
    BatteryDisplay display;
    battery_display_init(&display);
    battery_display_update(&display, 34, true);
    CHECK_EQUAL(36, battery_display_update(&display, 36, true));
    CHECK_EQUAL(36, battery_display_update(&display, 30, true));
    CHECK_EQUAL(26, battery_display_update(&display, 26, true));
}

static void plugging_in_does_not_drop_the_shown_percent(void)
{
    BatteryDisplay display;
    battery_display_init(&display);
    battery_display_update(&display, 60, false);
    CHECK_EQUAL(60, battery_display_update(&display, 53, true));
    CHECK_EQUAL(60, battery_display_update(&display, 59, true));
    CHECK_EQUAL(62, battery_display_update(&display, 62, true));
}

static void unplugging_does_not_raise_the_shown_percent(void)
{
    BatteryDisplay display;
    battery_display_init(&display);
    battery_display_update(&display, 70, true);
    CHECK_EQUAL(70, battery_display_update(&display, 72, false));
    CHECK_EQUAL(67, battery_display_update(&display, 67, false));
}

static void a_boot_below_3400_mv_on_battery_switches_off(void)
{
    CHECK(battery_boot_cutoff(3399, false));
    CHECK(!battery_boot_cutoff(3400, false));
    CHECK(!battery_boot_cutoff(3000, true));
}

static ChargeState charge_at(ChargeTracker *tracker, uint32_t now_ms, bool status_high,
                             uint32_t millivolts)
{
    return charge_tracker_update(tracker, (ChargeReading){.usb_power = true,
                                                          .status_high = status_high,
                                                          .millivolts = millivolts,
                                                          .now_ms = now_ms});
}

static void without_usb_power_the_player_is_on_battery(void)
{
    ChargeTracker tracker;
    charge_tracker_init(&tracker);
    CHECK_EQUAL(
        CHARGE_ON_BATTERY,
        charge_tracker_update(
            &tracker, (ChargeReading){.status_high = true, .millivolts = 4200, .now_ms = 60000}));
}

static void usb_power_alone_shows_charging_whatever_the_status_pin_says_at_first(void)
{
    ChargeTracker tracker;
    charge_tracker_init(&tracker);
    CHECK_EQUAL(CHARGE_CHARGING, charge_at(&tracker, 1000, true, 4200));
    CHECK_EQUAL(CHARGE_CHARGING, charge_at(&tracker, 1000 + CHARGE_SETTLE_MS - 1, true, 4200));
}

static void a_low_status_pin_is_charging(void)
{
    ChargeTracker tracker;
    charge_tracker_init(&tracker);
    charge_at(&tracker, 0, false, 3900);
    CHECK_EQUAL(CHARGE_CHARGING, charge_at(&tracker, 60000, false, 3900));
}

static void a_status_pin_held_high_after_settling_is_full(void)
{
    ChargeTracker tracker;
    charge_tracker_init(&tracker);
    charge_at(&tracker, 0, false, 4180);
    CHECK_EQUAL(CHARGE_CHARGING, charge_at(&tracker, CHARGE_SETTLE_MS, true, 4180));
    CHECK_EQUAL(CHARGE_CHARGING,
                charge_at(&tracker, CHARGE_SETTLE_MS + CHARGE_FULL_HOLD_MS - 1, true, 4180));
    CHECK_EQUAL(CHARGE_FULL,
                charge_at(&tracker, CHARGE_SETTLE_MS + CHARGE_FULL_HOLD_MS, true, 4180));
}

static void a_brief_high_reading_is_not_full(void)
{
    ChargeTracker tracker;
    charge_tracker_init(&tracker);
    charge_at(&tracker, 0, false, 4180);
    charge_at(&tracker, 6000, true, 4180);
    charge_at(&tracker, 7000, false, 4180);
    CHECK_EQUAL(CHARGE_CHARGING, charge_at(&tracker, 8500, true, 4180));
    CHECK_EQUAL(CHARGE_FULL, charge_at(&tracker, 10500, true, 4180));
}

static void a_high_status_pin_on_a_low_battery_is_not_full(void)
{
    ChargeTracker tracker;
    charge_tracker_init(&tracker);
    charge_at(&tracker, 0, true, 3700);
    CHECK_EQUAL(CHARGE_CHARGING, charge_at(&tracker, 20000, true, CHARGE_FULL_MIN_MILLIVOLTS - 1));
    CHECK_EQUAL(CHARGE_CHARGING, charge_at(&tracker, 40000, true, CHARGE_FULL_MIN_MILLIVOLTS - 1));
}

static void full_stays_full_until_the_cable_comes_out(void)
{
    ChargeTracker tracker;
    charge_tracker_init(&tracker);
    charge_at(&tracker, 0, true, 4180);
    charge_at(&tracker, 6000, true, 4180);
    CHECK_EQUAL(CHARGE_FULL, charge_at(&tracker, 9000, true, 4180));
    CHECK_EQUAL(CHARGE_FULL, charge_at(&tracker, 60000, false, 4120));
    CHECK_EQUAL(CHARGE_ON_BATTERY,
                charge_tracker_update(&tracker, (ChargeReading){.now_ms = 61000}));
    CHECK_EQUAL(CHARGE_CHARGING, charge_at(&tracker, 62000, true, 4180));
}

static void the_clock_wrapping_does_not_end_the_settling_early_or_late(void)
{
    ChargeTracker tracker;
    charge_tracker_init(&tracker);
    uint32_t start = UINT32_MAX - 1000u;
    charge_at(&tracker, start, true, 4180);
    CHECK_EQUAL(CHARGE_CHARGING, charge_at(&tracker, start + CHARGE_SETTLE_MS - 1, true, 4180));
    charge_at(&tracker, start + CHARGE_SETTLE_MS, true, 4180);
    CHECK_EQUAL(CHARGE_FULL,
                charge_at(&tracker, start + CHARGE_SETTLE_MS + CHARGE_FULL_HOLD_MS, true, 4180));
}

static void a_full_battery_shows_100_at_once(void)
{
    BatteryDisplay display;
    battery_display_init(&display);
    battery_display_update(&display, 97, true);
    battery_display_show_full(&display);
    CHECK_EQUAL(100, battery_display_update(&display, 99, true));
    CHECK_EQUAL(97, battery_display_update(&display, 97, false));
}

static void usb_only_keeps_charging_for_unknown_or_low_batteries_and_uses_hysteresis(void)
{
    BatteryCareReading reading = {.usb_power = true, .mode = 2};
    CHECK(battery_care_charge(false, reading));
    reading.known = true;
    reading.millivolts = 3500;
    CHECK(battery_care_charge(false, reading));
    reading.millivolts = 3700;
    CHECK(battery_care_charge(true, reading));
    CHECK(!battery_care_charge(false, reading));
    reading.millivolts = 3900;
    CHECK(!battery_care_charge(true, reading));
    reading.millivolts = 3599;
    CHECK(battery_care_charge(false, reading));
    reading.mode = 0;
    reading.millivolts = 4800;
    CHECK(battery_care_charge(false, reading));
    reading.mode = 1;
    CHECK(battery_care_charge(false, reading));
    reading.usb_power = false;
    CHECK(!battery_care_charge(true, reading));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(usb_only_keeps_charging_for_unknown_or_low_batteries_and_uses_hysteresis),
        TEST_CASE(readings_scale_to_millivolts_through_the_divider),
        TEST_CASE(the_curve_runs_from_empty_to_full),
        TEST_CASE(the_curve_is_linear_between_points),
        TEST_CASE(a_new_gauge_knows_nothing),
        TEST_CASE(one_reading_gives_a_level),
        TEST_CASE(a_single_odd_reading_moves_the_level_only_a_little),
        TEST_CASE(charging_reads_50_mv_lower),
        TEST_CASE(charging_never_shows_full),
        TEST_CASE(a_restart_keeps_only_the_latest_reading),
        TEST_CASE(the_recent_level_follows_the_last_readings),
        TEST_CASE(the_average_spans_several_seconds_of_readings),
        TEST_CASE(the_shown_percent_starts_at_the_first_level),
        TEST_CASE(a_one_point_wobble_keeps_the_shown_percent),
        TEST_CASE(on_battery_the_shown_percent_moves_after_a_three_point_change),
        TEST_CASE(on_battery_the_shown_percent_recovers_after_a_load_ends),
        TEST_CASE(on_battery_jitter_does_not_flap_the_shown_percent),
        TEST_CASE(on_usb_power_the_shown_percent_rises_freely_and_falls_reluctantly),
        TEST_CASE(plugging_in_does_not_drop_the_shown_percent),
        TEST_CASE(unplugging_does_not_raise_the_shown_percent),
        TEST_CASE(a_boot_below_3400_mv_on_battery_switches_off),
        TEST_CASE(without_usb_power_the_player_is_on_battery),
        TEST_CASE(usb_power_alone_shows_charging_whatever_the_status_pin_says_at_first),
        TEST_CASE(a_low_status_pin_is_charging),
        TEST_CASE(a_status_pin_held_high_after_settling_is_full),
        TEST_CASE(a_brief_high_reading_is_not_full),
        TEST_CASE(a_high_status_pin_on_a_low_battery_is_not_full),
        TEST_CASE(full_stays_full_until_the_cable_comes_out),
        TEST_CASE(the_clock_wrapping_does_not_end_the_settling_early_or_late),
        TEST_CASE(a_full_battery_shows_100_at_once),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
