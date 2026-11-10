#pragma once

#include "settings/settings.h"
#include "ui/theme.h"

extern const Theme ECHO_THEME;
extern const Theme ECHO_NOTICE_THEME;
void default_theme_set_appearance(Theme *theme, AppearanceTheme appearance);
void default_theme_invalidate(Theme *theme);
void default_theme_set_accent(Theme *theme, AccentColor accent);
void default_theme_set_text_size(Theme *theme, TextSize size);
void default_theme_set_orientation(Theme *theme, Orientation orientation);
#define BOARD_THEME ECHO_THEME
#define NOTICE_THEME ECHO_NOTICE_THEME
