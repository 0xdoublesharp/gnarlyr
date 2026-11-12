#pragma once

#include <stdbool.h>

#include "hal/audio.h"
#include "headphone_store.h"
#include "player.h"
#include "settings_store.h"

typedef struct ListeningEffectsContext {
    SettingsStore *settings;
    HeadphoneStore *headphones;
    Player *player;
} ListeningEffectsContext;

HalAudioEffects listening_effects_choose(const Settings *settings,
                                         const HeadphoneProfiles *profiles, bool spoken);
bool listening_effects_toggle_skip_silence(SettingsStore *store);
bool listening_effects_toggle_speech_boost(SettingsStore *store);
bool listening_effects_cycle_crossfeed(ListeningEffectsContext context);
