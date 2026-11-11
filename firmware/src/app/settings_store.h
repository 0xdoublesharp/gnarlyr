#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "parametric_store.h"
#include "player.h"
#include "settings/settings.h"

extern const char SETTINGS_STORE_PATH[];
extern const char SETTINGS_BUTTONS_PATH[];

typedef struct SettingsStore {
    Settings settings;
    ParametricStore parametric;
    int8_t equalizer_slot;
    uint8_t saved[SETTINGS_FILE_SIZE];
    bool has_saved;
    uint8_t saved_buttons[SETTINGS_BUTTONS_FILE_SIZE];
    bool has_saved_buttons;
} SettingsStore;

void settings_store_load(SettingsStore *store);
bool settings_store_save(SettingsStore *store);
bool settings_store_set_clock_offset(SettingsStore *store, int16_t offset);
void settings_store_capture(SettingsStore *store, PlayerStatus playback);
void settings_store_restore_playback(const SettingsStore *store, Player *player, uint32_t seed);
void settings_store_apply_hardware(const Settings *settings);
void settings_store_apply_equalizer(const Settings *settings);
