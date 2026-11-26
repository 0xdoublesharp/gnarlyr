#include "battery_model_format.h"

static bool valid_rate(uint32_t rate)
{
    return rate >= BATTERY_ESTIMATE_MIN_RATE && rate <= BATTERY_ESTIMATE_MAX_RATE;
}

static bool valid_model(const uint8_t *bytes)
{
    if (!record_is(bytes, &BATTERY_MODEL) || !record_sealed(bytes, SEAL_FIELD) ||
        bytes[SHOW_TIME_BYTE] > 1 || bytes[RESERVED_BYTE] != 0) {
        return false;
    }
    for (unsigned i = PADDING_FIELD; i < SEAL_FIELD; i++) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

bool battery_model_decode(BatteryEstimate *estimate, const uint8_t bytes[BATTERY_MODEL_BYTES])
{
    if (!valid_model(bytes)) {
        return false;
    }
    uint32_t habit = bytes_le16(bytes + HABIT_FIELD);
    if (habit != 0 && !valid_rate(habit)) {
        return false;
    }
    BatteryEstimate saved = {.habit = (uint16_t)habit, .show_time = bytes[SHOW_TIME_BYTE] != 0};
    for (unsigned i = 0; i < BATTERY_LOAD_COUNT; i++) {
        uint32_t rate = bytes_le16(bytes + RATE_FIELD + i * 2);
        uint8_t learned = bytes[LEARNED_FIELD + i];
        if ((learned == 0 && rate != 0) || (learned != 0 && (habit == 0 || !valid_rate(rate)))) {
            return false;
        }
        saved.rate[i] = (uint16_t)rate;
        saved.learned[i] = learned;
    }
    estimate->habit = saved.habit;
    estimate->show_time = saved.show_time;
    for (unsigned i = 0; i < BATTERY_LOAD_COUNT; i++) {
        estimate->rate[i] = saved.rate[i];
        estimate->learned[i] = saved.learned[i];
    }
    return true;
}
