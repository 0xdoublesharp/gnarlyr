#include "core/battery_estimate.h"

#include "core/deadline.h"

enum {
    MINUTE_MS = 60000,
    CLASS_DEBOUNCE_MS = MINUTE_MS,
    SETTLE_MS = 2 * MINUTE_MS,
    IDLE_SETTLE_MS = 10 * MINUTE_MS,
    USB_LONG_MS = 5 * MINUTE_MS,
    USB_SHORT_SETTLE_MS = 3 * MINUTE_MS,
    USB_LONG_SETTLE_MS = 20 * MINUTE_MS,
    GAP_MS = 2 * MINUTE_MS,
    SCREEN_GLANCE_MS = 2 * MINUTE_MS,
    WINDOW_MINUTES = 30,
    IDLE_WINDOW_MINUTES = 240,
    HABIT_MINUTES = 600,
    MIN_LEVEL_DROP = 20,
    SPIKE_UP_MV = 12,
    SPIKE_DOWN_MV = 25
};

static const uint16_t LEVEL_POINTS[][2] = {
    {3450, 0},    {3550, 1000}, {3650, 2600}, {3725, 3800}, {3758, 4300},  {3800, 4620},
    {3861, 5050}, {3930, 6200}, {4000, 7600}, {4100, 9000}, {4200, 10000},
};

uint16_t battery_estimate_level_for(uint32_t mv)
{
    if (mv <= LEVEL_POINTS[0][0]) {
        return 0;
    }
    for (unsigned i = 1; i < sizeof LEVEL_POINTS / sizeof LEVEL_POINTS[0]; i++) {
        uint32_t high = LEVEL_POINTS[i][0];
        if (mv <= high) {
            uint32_t low = LEVEL_POINTS[i - 1][0];
            uint32_t base = LEVEL_POINTS[i - 1][1];
            return (uint16_t)(base + (mv - low) * (LEVEL_POINTS[i][1] - base) / (high - low));
        }
    }
    return 10000;
}

static void settle(BatteryEstimate *estimate, uint32_t now, uint32_t delay)
{
    estimate->anchored = false;
    estimate->anchor_ms = now + delay;
}

static void reset_class(BatteryEstimate *estimate, BatteryEstimateReading reading)
{
    estimate->since_ms = reading.now_ms;
    estimate->active_class = (uint8_t)reading.load;
    estimate->candidate_class = (uint8_t)reading.load;
}

static void usb_changed(BatteryEstimate *estimate, BatteryEstimateReading reading)
{
    uint32_t now = reading.now_ms;
    estimate->on_usb = reading.usb_power;
    if (reading.usb_power) {
        estimate->usb_start_ms = now;
        estimate->shown_minutes = 0;
        estimate->shown_hours = 0;
        estimate->level_known = false;
        estimate->session_rate = 0;
        estimate->session_windows = 0;
        estimate->anchored = false;
    } else {
        estimate->usb_duration_ms = now - estimate->usb_start_ms;
        reset_class(estimate, reading);
        settle(estimate, now,
               estimate->usb_duration_ms >= USB_LONG_MS ? USB_LONG_SETTLE_MS : USB_SHORT_SETTLE_MS);
    }
    estimate->last_mv = 0;
}

static void change_class(BatteryEstimate *estimate, BatteryEstimateReading reading)
{
    uint8_t load = (uint8_t)reading.load;
    uint32_t now = reading.now_ms;
    if (load != estimate->candidate_class) {
        estimate->candidate_class = load;
        estimate->since_ms = now;
    }
    if (load != estimate->active_class &&
        deadline_elapsed(now, estimate->since_ms, CLASS_DEBOUNCE_MS)) {
        estimate->active_class = load;
        uint32_t delay = load == BATTERY_LOAD_IDLE ? IDLE_SETTLE_MS : SETTLE_MS;
        if (estimate->anchored || deadline_reached(now + delay, estimate->anchor_ms)) {
            settle(estimate, now, delay);
        }
    }
}

static bool valid_rate(const BatteryEstimate *estimate, unsigned load, uint32_t sample)
{
    uint32_t rate = estimate->rate[load];
    return sample >= BATTERY_ESTIMATE_MIN_RATE && sample <= BATTERY_ESTIMATE_MAX_RATE &&
           (estimate->learned[load] < 2 || (sample * 4 >= rate && sample <= 4 * rate));
}

static void learn(BatteryEstimate *estimate, uint32_t sample, uint32_t minutes)
{
    unsigned load = estimate->active_class;
    if (!valid_rate(estimate, load, sample)) {
        return;
    }
    uint8_t windows = estimate->learned[load];
    estimate->rate[load] =
        (uint16_t)(windows == 0 ? sample : (3u * estimate->rate[load] + sample) / 4);
    estimate->learned[load] = (uint8_t)(windows + (windows != UINT8_MAX));
    uint32_t habit = estimate->habit;
    estimate->habit = (uint16_t)(habit == 0 ? sample
                                            : (habit * HABIT_MINUTES + sample * minutes) /
                                                  (HABIT_MINUTES + minutes));
    estimate->session_rate =
        (uint16_t)(estimate->session_windows == 0 ? sample : (estimate->session_rate + sample) / 2);
    if (estimate->session_windows != UINT8_MAX) {
        estimate->session_windows++;
    }
    estimate->dirty = true;
}

static void follow_window(BatteryEstimate *estimate, uint32_t now, uint16_t level)
{
    int drop = estimate->anchor_level - level;
    uint32_t elapsed = now - estimate->anchor_ms;
    if (estimate->anchored) {
        bool expired = elapsed >= IDLE_WINDOW_MINUTES * MINUTE_MS;
        if (elapsed < WINDOW_MINUTES * MINUTE_MS || (drop < MIN_LEVEL_DROP && !expired)) {
            return;
        }
        if (drop >= MIN_LEVEL_DROP || (drop > 0 && estimate->active_class == BATTERY_LOAD_IDLE)) {
            learn(estimate, (uint32_t)drop * (6 * MINUTE_MS) / (elapsed / 10), elapsed / MINUTE_MS);
        }
    }
    estimate->anchored = true;
    estimate->anchor_level = level;
    estimate->anchor_ms = now;
}

static void predict(BatteryEstimate *estimate, uint32_t now)
{
    unsigned load = estimate->active_class;
    if (load == BATTERY_LOAD_SCREEN &&
        (estimate->candidate_class != BATTERY_LOAD_SCREEN ||
         !deadline_elapsed(now, estimate->since_ms, SCREEN_GLANCE_MS))) {
        load = estimate->effective_class;
    }
    if (!estimate->level_known || estimate->habit == 0 ||
        (estimate->learned[BATTERY_LOAD_LIGHT] < 2 && estimate->learned[load] < 2 &&
         estimate->session_windows < 2)) {
        estimate->shown_minutes = 0;
        estimate->shown_hours = 0;
        estimate->effective_class = (uint8_t)load;
        return;
    }
    uint32_t rate = estimate->rate[load];
    if (estimate->session_windows != 0) {
        rate = rate == 0 ? estimate->session_rate : (rate + estimate->session_rate) / 2;
    }
    uint32_t effective = rate == 0 ? estimate->habit : (3u * estimate->habit + rate) / 4;
    uint32_t minutes = (uint32_t)estimate->settled_level * 60 / effective;
    uint32_t current = estimate->shown_minutes;
    if (load == estimate->effective_class && current != 0 && minutes > current &&
        minutes * 5 <= current * 6) {
        return;
    }
    uint32_t rounded = minutes >= BATTERY_ESTIMATE_HOURS_AT ? (minutes + 7) / 15 * 15
                       : minutes >= 95                      ? 95
                       : minutes < 5                        ? 5
                                                            : (minutes + 2) / 5 * 5;
    estimate->shown_minutes = (uint16_t)rounded;
    uint32_t hours = (minutes + 30) / 60;
    estimate->shown_hours = (uint8_t)(minutes < BATTERY_ESTIMATE_HOURS_AT ? 0
                                      : hours > 99                        ? 99
                                                                          : hours);
    estimate->effective_class = (uint8_t)load;
}

static void sample(BatteryEstimate *estimate, BatteryEstimateReading reading, uint32_t gap)
{
    uint32_t now = reading.now_ms;
    uint32_t mv = reading.millivolts;
    if (estimate->anchored && gap > GAP_MS) {
        settle(estimate, now, SETTLE_MS);
    }
    if (estimate->active_class == estimate->candidate_class) {
        uint32_t previous = estimate->last_mv;
        estimate->last_mv = (uint16_t)mv;
        if (estimate->anchored &&
            (mv >= previous + SPIKE_UP_MV || mv + SPIKE_DOWN_MV <= previous)) {
            settle(estimate, now, SETTLE_MS);
        }
        if (estimate->anchored || deadline_reached(now, estimate->anchor_ms)) {
            uint16_t level = battery_estimate_level_for(mv);
            estimate->settled_level = level;
            estimate->level_known = true;
            follow_window(estimate, now, level);
        }
    }
    predict(estimate, now);
}

void battery_estimate_update(BatteryEstimate *estimate, BatteryEstimateReading reading)
{
    bool valid = reading.known && reading.millivolts <= UINT16_MAX;
    if (!valid && estimate->on_usb == reading.usb_power) {
        return;
    }
    uint32_t now = reading.now_ms;
    uint32_t gap = now - estimate->last_sample_ms;
    if (!estimate->sampled) {
        reset_class(estimate, reading);
        estimate->effective_class = (uint8_t)reading.load;
        settle(estimate, now, SETTLE_MS);
    } else if (gap < BATTERY_ESTIMATE_SAMPLE_MS && estimate->on_usb == reading.usb_power) {
        return;
    }
    estimate->sampled = true;
    estimate->last_sample_ms = now;
    if (estimate->on_usb != reading.usb_power) {
        usb_changed(estimate, reading);
    }
    if (!reading.usb_power && valid) {
        change_class(estimate, reading);
        sample(estimate, reading, gap);
    }
}
