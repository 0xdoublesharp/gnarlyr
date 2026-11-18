#pragma once

#include <stdbool.h>

#include "player.h"
#include "settings_store.h"

bool audio_settings_set_gain(SettingsStore *store, Player *player, GainLevel gain);
bool audio_settings_set_filter(SettingsStore *store, Player *player, DacFilter filter);
