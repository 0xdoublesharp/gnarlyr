#pragma once

#include "core/i18n.h"
#include "player.h"
#include "settings/settings.h"

typedef struct SettingsPreview {
    StringId choice;
    int index;
} SettingsPreview;

struct SettingsScreens;

const Settings *settings_preview_shown(struct SettingsScreens *screens, bool active);
void settings_preview_apply(Settings *settings, SettingsPreview preview);
void settings_preview_follow(SettingsPreview *applied, SettingsPreview next, const Settings *saved,
                             Player *player);
