#include "settings_store.h"

#include "battery_model.h"

#include "hal/storage.h"

static void read_values(SettingsStore *store)
{
    uint8_t bytes[SETTINGS_FILE_SIZE] = {0};
    store->has_saved = false;
    if (hal_internal_file_read(SETTINGS_STORE_PATH, bytes, sizeof bytes) &&
        settings_decode(bytes, sizeof bytes, &store->settings)) {
        for (int index = 0; index < SETTINGS_FILE_SIZE; index++) {
            store->saved[index] = bytes[index];
        }
        store->has_saved = true;
    } else {
        store->settings = settings_defaults();
    }
}

static void read_buttons(SettingsStore *store)
{
    uint8_t buttons[SETTINGS_BUTTONS_FILE_SIZE];
    store->has_saved_buttons = false;
    if (hal_internal_file_read(SETTINGS_BUTTONS_PATH, buttons, sizeof buttons) &&
        settings_buttons_decode(buttons, sizeof buttons, &store->settings)) {
        for (int index = 0; index < SETTINGS_BUTTONS_FILE_SIZE; index++) {
            store->saved_buttons[index] = buttons[index];
        }
        store->has_saved_buttons = true;
    }
}

static void read_battery_model(BatteryEstimate *estimate)
{
    if (!estimate->loaded) {
        uint8_t bytes[BATTERY_MODEL_BYTES];
        if (hal_internal_file_read(BATTERY_MODEL_PATH, bytes, sizeof bytes)) {
            battery_model_decode(estimate, bytes);
        }
        estimate->loaded = true;
    }
}

void settings_store_read(SettingsStore *store)
{
    store->unread = false;
    read_values(store);
    read_buttons(store);
    read_battery_model(&store->battery);
}
