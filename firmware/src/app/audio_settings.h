#pragma once

#include <stdbool.h>

#include "player.h"
#include "settings_store.h"

enum {
    AUDIO_SETTINGS_HIGH_GAIN_RISE_DB = 6
};

bool audio_settings_set_gain(SettingsStore *store, Player *player, GainLevel gain);
bool audio_settings_set_filter(SettingsStore *store, Player *player, DacFilter filter);
