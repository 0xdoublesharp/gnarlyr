#pragma once

#include "ui/theme.h"

extern const Theme ECHO_THEME;
extern const Theme ECHO_NOTICE_THEME;
void echo_theme_apply(Theme *theme, uint8_t appearance);
#define BOARD_THEME ECHO_THEME
#define NOTICE_THEME ECHO_NOTICE_THEME
