#include "check.h"
#include "core/battery_estimate.h"

enum {
    MINUTE = 60000,
    STEP = BATTERY_ESTIMATE_SAMPLE_MS
};

static void read_at(BatteryEstimate *estimate, uint32_t now, uint32_t mv, BatteryLoadClass load)
{
    battery_estimate_update(estimate, (BatteryEstimateReading){now, mv, load, true, false});
}

static void usb_at(BatteryEstimate *estimate, uint32_t now)
{
    battery_estimate_update(estimate,
                            (BatteryEstimateReading){now, 4200, BATTERY_LOAD_SCREEN, false, true});
}

typedef struct Discharge {
    uint32_t start;
    uint32_t minutes;
    uint32_t first_mv;
    uint32_t last_mv;
    BatteryLoadClass load;
} Discharge;

static void discharge(BatteryEstimate *estimate, Discharge trace)
{
    uint32_t count = trace.minutes * MINUTE / STEP;
    for (uint32_t i = 0; i <= count; i++) {
        uint32_t mv = trace.first_mv - (trace.first_mv - trace.last_mv) * i / count;
        read_at(estimate, trace.start + i * STEP, mv, trace.load);
    }
}

static BatteryEstimate calibrated(uint16_t rate)
{
    return (BatteryEstimate){.habit = rate,
                             .rate = {[BATTERY_LOAD_LIGHT] = rate},
                             .learned = {[BATTERY_LOAD_LIGHT] = 2}};
}

static void the_level_curve_is_monotonic_and_clamped(void)
{
    CHECK_EQUAL(0, battery_estimate_level_for(0));
    CHECK_EQUAL(0, battery_estimate_level_for(3450));
    CHECK_EQUAL(4300, battery_estimate_level_for(3758));
    CHECK_EQUAL(10000, battery_estimate_level_for(4200));
    CHECK_EQUAL(10000, battery_estimate_level_for(UINT32_MAX));
    for (uint32_t mv = 3450; mv < 4200; mv++) {
        CHECK(battery_estimate_level_for(mv) < battery_estimate_level_for(mv + 1));
    }
}

static void two_clean_half_hours_unlock_the_estimate(void)
{
    BatteryEstimate estimate = {0};
    discharge(&estimate, (Discharge){0, 31, 3861, 3836, BATTERY_LOAD_LIGHT});
    CHECK_EQUAL(0, estimate.learned[BATTERY_LOAD_LIGHT]);
    CHECK_EQUAL(0, estimate.shown_minutes);
    read_at(&estimate, 32 * MINUTE, 3835, BATTERY_LOAD_LIGHT);
    CHECK_EQUAL(1, estimate.learned[BATTERY_LOAD_LIGHT]);
    CHECK_EQUAL(0, estimate.shown_minutes);
    discharge(&estimate, (Discharge){33 * MINUTE, 29, 3834, 3810, BATTERY_LOAD_LIGHT});
    CHECK_EQUAL(2, estimate.learned[BATTERY_LOAD_LIGHT]);
    CHECK_EQUAL(2, estimate.session_windows);
    CHECK(estimate.shown_minutes > 0);
    CHECK(estimate.dirty);
}

static void replay_overnight_tail(BatteryEstimate *estimate)
{
    static const uint16_t TAIL[][2] = {
        {0, 3861},   {3, 3861},   {6, 3856},   {10, 3853},  {14, 3851},  {18, 3846},  {22, 3844},
        {26, 3841},  {30, 3836},  {35, 3832},  {39, 3831},  {41, 3828},  {46, 3824},  {51, 3822},
        {55, 3817},  {59, 3814},  {63, 3812},  {67, 3807},  {72, 3803},  {77, 3800},  {78, 3798},
        {83, 3796},  {87, 3792},  {91, 3788},  {95, 3786},  {96, 3784},  {101, 3779}, {106, 3777},
        {111, 3773}, {113, 3772}, {114, 3770}, {119, 3767}, {123, 3763}, {127, 3758},
    };
    for (unsigned i = 1; i < sizeof TAIL / sizeof TAIL[0]; i++) {
        uint32_t start = 307 + TAIL[i - 1][0];
        uint32_t span = TAIL[i][0] - TAIL[i - 1][0];
        discharge(estimate, (Discharge){start * MINUTE, span, TAIL[i - 1][1], TAIL[i][1],
                                        BATTERY_LOAD_LIGHT});
    }
}

static void the_measured_overnight_tail_predicts_about_twelve_hours(void)
{
    BatteryEstimate estimate = {0};
    read_at(&estimate, 0, 4200, BATTERY_LOAD_LIGHT);
    replay_overnight_tail(&estimate);
    CHECK_EQUAL(4, estimate.learned[BATTERY_LOAD_LIGHT]);
    CHECK(estimate.shown_minutes >= 690 && estimate.shown_minutes <= 750);
    CHECK_EQUAL(12, estimate.shown_hours);
}

static void a_continuous_seven_hour_run_retains_the_faster_early_drain_in_its_habit(void)
{
    BatteryEstimate estimate = {0};
    discharge(&estimate, (Discharge){0, 307, 4200, 3861, BATTERY_LOAD_LIGHT});
    replay_overnight_tail(&estimate);
    CHECK_EQUAL(14, estimate.learned[BATTERY_LOAD_LIGHT]);
    CHECK(estimate.habit > estimate.session_rate);
    CHECK(estimate.shown_minutes >= 240 && estimate.shown_minutes <= 360);
}

static void a_screen_glance_keeps_the_window_and_the_playback_estimate(void)
{
    BatteryEstimate estimate = calibrated(400);
    discharge(&estimate, (Discharge){0, 20, 3870, 3853, BATTERY_LOAD_LIGHT});
    uint16_t before = estimate.shown_minutes;
    read_at(&estimate, 20 * MINUTE + STEP, 3820, BATTERY_LOAD_SCREEN);
    CHECK_EQUAL(2 * MINUTE, estimate.anchor_ms);
    CHECK_EQUAL(before, estimate.shown_minutes);
    discharge(&estimate, (Discharge){21 * MINUTE, 11, 3852, 3840, BATTERY_LOAD_LIGHT});
    CHECK_EQUAL(3, estimate.learned[BATTERY_LOAD_LIGHT]);

    read_at(&estimate, 33 * MINUTE, 3800, BATTERY_LOAD_SCREEN);
    read_at(&estimate, 34 * MINUTE, 3800, BATTERY_LOAD_SCREEN);
    CHECK_EQUAL(BATTERY_LOAD_SCREEN, estimate.active_class);
    CHECK_EQUAL(BATTERY_LOAD_LIGHT, estimate.effective_class);
    read_at(&estimate, 34 * MINUTE + STEP, 3840, BATTERY_LOAD_LIGHT);
    CHECK_EQUAL(BATTERY_LOAD_LIGHT, estimate.effective_class);
    read_at(&estimate, 35 * MINUTE, 3840, BATTERY_LOAD_LIGHT);
    CHECK_EQUAL(BATTERY_LOAD_LIGHT, estimate.effective_class);
    read_at(&estimate, 36 * MINUTE, 3800, BATTERY_LOAD_SCREEN);
    read_at(&estimate, 37 * MINUTE, 3800, BATTERY_LOAD_SCREEN);
    read_at(&estimate, 38 * MINUTE, 3800, BATTERY_LOAD_SCREEN);
    CHECK_EQUAL(BATTERY_LOAD_SCREEN, estimate.effective_class);
}

static void it_settles_for_three_minutes_after_short_usb_and_twenty_after_long_usb(void)
{
    BatteryEstimate estimate = calibrated(400);
    discharge(&estimate, (Discharge){0, 32, 3870, 3845, BATTERY_LOAD_LIGHT});
    CHECK(estimate.shown_minutes > 0);
    usb_at(&estimate, 33 * MINUTE);
    CHECK_EQUAL(0, estimate.shown_minutes);
    CHECK_EQUAL(0, estimate.session_windows);
    read_at(&estimate, 35 * MINUTE, 4020, BATTERY_LOAD_LIGHT);
    CHECK_EQUAL(2 * MINUTE, estimate.usb_duration_ms);
    read_at(&estimate, 37 * MINUTE, 3940, BATTERY_LOAD_LIGHT);
    read_at(&estimate, 38 * MINUTE - STEP, 3905, BATTERY_LOAD_LIGHT);
    CHECK(!estimate.anchored);
    CHECK_EQUAL(0, estimate.shown_minutes);
    read_at(&estimate, 38 * MINUTE, 3900, BATTERY_LOAD_LIGHT);
    CHECK(estimate.anchored);
    CHECK(estimate.shown_minutes > 0);
    usb_at(&estimate, 40 * MINUTE);
    read_at(&estimate, 45 * MINUTE, 4150, BATTERY_LOAD_LIGHT);
    CHECK_EQUAL(5 * MINUTE, estimate.usb_duration_ms);
    CHECK_EQUAL(65 * MINUTE, estimate.anchor_ms);
    read_at(&estimate, 65 * MINUTE - STEP, 3905, BATTERY_LOAD_LIGHT);
    CHECK(!estimate.anchored);
    read_at(&estimate, 65 * MINUTE, 3900, BATTERY_LOAD_LIGHT);
    CHECK(estimate.anchored);
    CHECK_EQUAL(3, estimate.learned[BATTERY_LOAD_LIGHT]);
}

static void a_gap_or_voltage_spike_restarts_settling(void)
{
    BatteryEstimate estimate = calibrated(400);
    discharge(&estimate, (Discharge){0, 29, 3880, 3861, BATTERY_LOAD_LIGHT});
    uint16_t before = estimate.shown_minutes;
    read_at(&estimate, 30 * MINUTE, 3873, BATTERY_LOAD_LIGHT);
    CHECK(!estimate.anchored);
    CHECK_EQUAL(before, estimate.shown_minutes);
    read_at(&estimate, 32 * MINUTE, 3861, BATTERY_LOAD_LIGHT);
    CHECK(estimate.anchored);
    read_at(&estimate, 33 * MINUTE, 3836, BATTERY_LOAD_LIGHT);
    CHECK(!estimate.anchored);
    read_at(&estimate, 35 * MINUTE, 3855, BATTERY_LOAD_LIGHT);
    CHECK(estimate.anchored);
    read_at(&estimate, 37 * MINUTE + STEP, 3850, BATTERY_LOAD_LIGHT);
    CHECK(!estimate.anchored);
    CHECK_EQUAL(2, estimate.learned[BATTERY_LOAD_LIGHT]);
}

static void idle_waits_for_rebound_and_rejects_flat_or_rising_voltage(void)
{
    BatteryEstimate estimate = {0};
    discharge(&estimate, (Discharge){0, 5, 3866, 3861, BATTERY_LOAD_LIGHT});
    read_at(&estimate, 6 * MINUTE, 3880, BATTERY_LOAD_IDLE);
    read_at(&estimate, 7 * MINUTE, 3900, BATTERY_LOAD_IDLE);
    CHECK_EQUAL(17 * MINUTE, estimate.anchor_ms);
    discharge(&estimate, (Discharge){7 * MINUTE + STEP, 9, 3900, 3900, BATTERY_LOAD_IDLE});
    CHECK(!estimate.anchored);
    discharge(&estimate, (Discharge){17 * MINUTE, 240, 3902, 3902, BATTERY_LOAD_IDLE});
    CHECK_EQUAL(0, estimate.learned[BATTERY_LOAD_IDLE]);
    read_at(&estimate, 258 * MINUTE, 3907, BATTERY_LOAD_IDLE);
    discharge(&estimate, (Discharge){258 * MINUTE + STEP, 240, 3910, 3910, BATTERY_LOAD_IDLE});
    CHECK_EQUAL(0, estimate.learned[BATTERY_LOAD_IDLE]);
    CHECK(!estimate.dirty);
}

static void outlier_and_below_minimum_rates_do_not_update_the_model(void)
{
    BatteryEstimate estimate = calibrated(4000);
    discharge(&estimate, (Discharge){0, 32, 3861, 3841, BATTERY_LOAD_LIGHT});
    CHECK_EQUAL(2, estimate.learned[BATTERY_LOAD_LIGHT]);
    CHECK_EQUAL(4000, estimate.habit);
    CHECK(!estimate.dirty);
    estimate = (BatteryEstimate){0};
    discharge(&estimate, (Discharge){0, 62, 3820, 3817, BATTERY_LOAD_LIGHT});
    CHECK_EQUAL(0, estimate.learned[BATTERY_LOAD_LIGHT]);
    CHECK_EQUAL(0, estimate.habit);
}

static void heavy_windows_blend_with_habit_and_calibrate_the_session(void)
{
    BatteryEstimate estimate = {0};
    discharge(&estimate, (Discharge){0, 32, 3840, 3810, BATTERY_LOAD_HEAVY});
    uint16_t first = estimate.habit;
    CHECK(first > 0);
    CHECK_EQUAL(first, estimate.session_rate);
    CHECK_EQUAL(0, estimate.shown_minutes);
    discharge(&estimate, (Discharge){33 * MINUTE, 29, 3808, 3765, BATTERY_LOAD_HEAVY});
    CHECK_EQUAL(2, estimate.learned[BATTERY_LOAD_HEAVY]);
    CHECK_EQUAL(2, estimate.session_windows);
    CHECK(estimate.session_rate > estimate.habit);
    CHECK(estimate.habit > first);
    CHECK(estimate.shown_minutes > 0);
}

static void time_falls_freely_but_needs_a_large_increase_to_rise(void)
{
    BatteryEstimate estimate = calibrated(500);
    discharge(&estimate, (Discharge){0, 2, 3758, 3758, BATTERY_LOAD_LIGHT});
    uint16_t before = estimate.shown_minutes;
    read_at(&estimate, 3 * MINUTE, 3766, BATTERY_LOAD_LIGHT);
    CHECK_EQUAL(before, estimate.shown_minutes);
    read_at(&estimate, 4 * MINUTE, 3740, BATTERY_LOAD_LIGHT);
    read_at(&estimate, 6 * MINUTE, 3733, BATTERY_LOAD_LIGHT);
    CHECK(estimate.shown_minutes < before);
    for (uint32_t i = 1; i <= 20; i++) {
        read_at(&estimate, 6 * MINUTE + i * STEP, 3733 + i * 10, BATTERY_LOAD_LIGHT);
    }
    CHECK(estimate.shown_minutes > before);
}

static void minute_rounding_and_counter_wrap_use_the_same_windows(void)
{
    BatteryEstimate estimate = {0};
    discharge(&estimate, (Discharge){UINT32_MAX - 10 * MINUTE, 62, 3861, 3810, BATTERY_LOAD_LIGHT});
    CHECK_EQUAL(2, estimate.learned[BATTERY_LOAD_LIGHT]);
    CHECK(estimate.shown_minutes >= 100);
    CHECK_EQUAL(0, estimate.shown_minutes % 15);
    estimate = calibrated(5000);
    discharge(&estimate, (Discharge){0, 2, 3465, 3465, BATTERY_LOAD_LIGHT});
    CHECK_EQUAL(5, estimate.shown_minutes);
    CHECK_EQUAL(0, estimate.shown_hours);
    estimate = calibrated(50);
    discharge(&estimate, (Discharge){0, 2, 4200, 4200, BATTERY_LOAD_LIGHT});
    CHECK_EQUAL(99, estimate.shown_hours);
    CHECK_EQUAL(12000, estimate.shown_minutes);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_level_curve_is_monotonic_and_clamped),
        TEST_CASE(two_clean_half_hours_unlock_the_estimate),
        TEST_CASE(the_measured_overnight_tail_predicts_about_twelve_hours),
        TEST_CASE(a_continuous_seven_hour_run_retains_the_faster_early_drain_in_its_habit),
        TEST_CASE(a_screen_glance_keeps_the_window_and_the_playback_estimate),
        TEST_CASE(it_settles_for_three_minutes_after_short_usb_and_twenty_after_long_usb),
        TEST_CASE(a_gap_or_voltage_spike_restarts_settling),
        TEST_CASE(idle_waits_for_rebound_and_rejects_flat_or_rising_voltage),
        TEST_CASE(outlier_and_below_minimum_rates_do_not_update_the_model),
        TEST_CASE(heavy_windows_blend_with_habit_and_calibrate_the_session),
        TEST_CASE(time_falls_freely_but_needs_a_large_increase_to_rise),
        TEST_CASE(minute_rounding_and_counter_wrap_use_the_same_windows),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
