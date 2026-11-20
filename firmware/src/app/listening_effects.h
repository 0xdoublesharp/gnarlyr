#pragma once

#include <stdbool.h>

#include "hal/audio.h"
#include "headphone_store.h"
#include "settings_store.h"

HalAudioEffects listening_effects_choose(const Settings *settings,
                                         const HeadphoneProfiles *profiles, bool spoken);
