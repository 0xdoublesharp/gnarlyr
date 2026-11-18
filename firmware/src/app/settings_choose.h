#pragma once

#include <stdbool.h>

#include "core/debug_script.h"
#include "settings_screens.h"

typedef struct SettingsChooseTarget {
    SettingsScreens *screens;
    const SettingsChooseTables *tables;
} SettingsChooseTarget;

bool settings_choose(const SettingsChooseTarget *target, DebugSpan title, DebugSpan option);
bool settings_choose_button(const ButtonChoices *table, ButtonSettings *buttons, DebugSpan title,
                            DebugSpan option);
