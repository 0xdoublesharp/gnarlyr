#include <string.h>

#include "battery_model.h"
#include "check.h"
#include "core/record.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "settings_screens.h"

static const BatteryEstimate LEARNED = {.show_time = true,
                                        .habit = 725,
                                        .rate = {124, 650, 1125, 1345},
                                        .learned = {1, 2, 3, 255},
                                        .dirty = true};

static void the_record_has_a_fixed_layout_and_round_trips_only_persisted_values(void)
{
    fake_hal_reset();
    BatteryEstimate estimate = LEARNED;
    CHECK(battery_model_save(&estimate));
    uint8_t bytes[BATTERY_MODEL_BYTES];
    CHECK(hal_internal_file_read(BATTERY_MODEL_PATH, bytes, sizeof bytes));
    const uint8_t fields[] = {'D', 'S', 'B', 'R', 1, 0, 1, 0,   213, 2, 124, 0, 138, 2,
                              101, 4,   65,  5,   1, 2, 3, 255, 0,   0, 0,   0, 0,   0};
    CHECK(memcmp(fields, bytes, sizeof fields) == 0);
    CHECK(record_sealed(bytes, 28));
    BatteryEstimate decoded = {.session_rate = 500, .shown_minutes = 90};
    CHECK(battery_model_decode(&decoded, bytes));
    CHECK_EQUAL(LEARNED.habit, decoded.habit);
    CHECK(decoded.show_time);
    CHECK(memcmp(LEARNED.rate, decoded.rate, sizeof decoded.rate) == 0);
    CHECK(memcmp(LEARNED.learned, decoded.learned, sizeof decoded.learned) == 0);
    CHECK_EQUAL(500, decoded.session_rate);
    CHECK_EQUAL(90, decoded.shown_minutes);
    CHECK(!decoded.dirty);
}

static void a_failed_write_is_retried_and_an_unchanged_model_is_not_written(void)
{
    fake_hal_reset();
    BatteryEstimate estimate = LEARNED;
    fake_hal_fail_internal_writes(BATTERY_MODEL_PATH);
    CHECK(!battery_model_save(&estimate));
    CHECK(estimate.dirty);
    fake_hal_fail_internal_writes(NULL);
    CHECK(battery_model_save(&estimate));
    CHECK(!estimate.dirty);
    int writes = fake_hal_write_count();
    CHECK(battery_model_save(&estimate));
    CHECK_EQUAL(writes, fake_hal_write_count());
}

static void a_zero_or_invalid_model_uses_the_legacy_percent_setting(void)
{
    fake_hal_reset();
    SettingsStore store;
    settings_store_load(&store);
    store.settings.battery_percent = true;
    CHECK(settings_store_save(&store));
    uint8_t legacy[SETTINGS_FILE_SIZE];
    CHECK(hal_internal_file_read(SETTINGS_STORE_PATH, legacy, sizeof legacy));
    CHECK_EQUAL(1, legacy[4]);
    CHECK(legacy[5] & 8);
    CHECK_EQUAL(HAL_RESERVE_CREATED,
                hal_internal_file_reserve(BATTERY_MODEL_PATH, BATTERY_MODEL_BYTES));
    settings_store_load(&store);
    CHECK(store.settings.battery_percent);
    CHECK(!store.battery.show_time);
    CHECK(!store.battery.dirty);
    store.battery = LEARNED;
    CHECK(battery_model_save(&store.battery));
    uint8_t bytes[BATTERY_MODEL_BYTES];
    CHECK(hal_internal_file_read(BATTERY_MODEL_PATH, bytes, sizeof bytes));
    bytes[28] ^= 1;
    CHECK(!battery_model_decode(&store.battery, bytes));
    CHECK_EQUAL(LEARNED.habit, store.battery.habit);
    CHECK(hal_internal_file_write(BATTERY_MODEL_PATH, bytes, sizeof bytes));
    settings_store_load(&store);
    CHECK(store.settings.battery_percent);
    CHECK(!store.battery.show_time);
    CHECK_EQUAL(0, store.battery.habit);
    uint8_t settings[SETTINGS_FILE_SIZE];
    CHECK(hal_internal_file_read(SETTINGS_STORE_PATH, settings, sizeof settings));
    CHECK(memcmp(legacy, settings, sizeof settings) == 0);
}

static void refreshing_settings_for_the_clock_keeps_learned_state_in_ram(void)
{
    fake_hal_reset();
    SettingsStore store;
    settings_store_load(&store);
    BatteryEstimate saved = LEARNED;
    CHECK(battery_model_save(&saved));
    settings_store_load(&store);
    CHECK_EQUAL(saved.habit, store.battery.habit);
    store.battery.habit++;
    store.battery.session_windows = 5;
    store.battery.dirty = true;
    settings_store_reload(&store);
    CHECK_EQUAL(saved.habit + 1, store.battery.habit);
    CHECK_EQUAL(5, store.battery.session_windows);
    CHECK(store.battery.dirty);
}

static void the_power_menu_explains_the_state_and_requires_a_reset_choice(void)
{
    static SettingsScreens screens;
    SettingsStore store;
    Toast toast = {0};
    fake_hal_reset();
    settings_store_load(&store);
    store.battery = LEARNED;
    store.battery.loaded = true;
    settings_screens_init(&screens, (SettingsContext){.store = &store, .toast = &toast},
                          &BOARD_THEME);
    MenuItem *row = &screens.items[SETTINGS_BATTERY_TIME_ITEM];
    fake_hal_set_battery((HalBattery){.known = true, .millivolts = 3758});
    CHECK(strcmp("Learning your usage", row->value(row->context)) == 0);
    store.battery.shown_minutes = 95;
    CHECK(strcmp("About 1 h 35 min left", row->value(row->context)) == 0);
    store.battery.shown_minutes = 35;
    CHECK(strcmp("About 35 min left", row->value(row->context)) == 0);
    fake_hal_set_battery((HalBattery){.usb_power = true, .charging = true});
    CHECK(strcmp("Charging", row->value(row->context)) == 0);
    fake_hal_set_battery((HalBattery){.usb_power = true, .charge_disabled = true});
    CHECK(strcmp("Running from USB", row->value(row->context)) == 0);
    ScreenAction action = row->select(row->context);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    CHECK(action.target == &screens.choice.screen);
    CHECK_EQUAL(0, screens.choice.config.current(screens.choice.config.context));
    screens.choice.config.choose(screens.choice.config.context, 0);
    CHECK_EQUAL(LEARNED.habit, store.battery.habit);
    screens.choice.config.choose(screens.choice.config.context, 1);
    CHECK_EQUAL(0, store.battery.habit);
    CHECK_EQUAL(0, store.battery.shown_minutes);
    CHECK(store.battery.show_time);
    CHECK(store.battery.loaded);
    CHECK(!store.battery.dirty);
    CHECK(toast.visible);
    settings_store_load(&store);
    CHECK(store.battery.show_time);
    CHECK_EQUAL(0, store.battery.habit);
    for (unsigned i = 0; i < BATTERY_LOAD_COUNT; i++) {
        CHECK_EQUAL(0, store.battery.learned[i]);
        CHECK_EQUAL(0, store.battery.rate[i]);
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_record_has_a_fixed_layout_and_round_trips_only_persisted_values),
        TEST_CASE(a_failed_write_is_retried_and_an_unchanged_model_is_not_written),
        TEST_CASE(a_zero_or_invalid_model_uses_the_legacy_percent_setting),
        TEST_CASE(refreshing_settings_for_the_clock_keeps_learned_state_in_ram),
        TEST_CASE(the_power_menu_explains_the_state_and_requires_a_reset_choice),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
