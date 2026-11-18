#pragma once

#include <stdbool.h>

#include "player.h"
#include "settings_store.h"

enum {
    AUDIO_SETTINGS_HIGH_GAIN_RISE_DB = 6
};

typedef enum AudioSettingsResult {
    AUDIO_SETTINGS_SAVED,
    AUDIO_SETTINGS_NOT_SAVED,
    AUDIO_SETTINGS_NOT_APPLIED
} AudioSettingsResult;

AudioSettingsResult audio_settings_set_gain(SettingsStore *store, Player *player, GainLevel gain);
AudioSettingsResult audio_settings_set_filter(SettingsStore *store, Player *player,
                                              DacFilter filter);
