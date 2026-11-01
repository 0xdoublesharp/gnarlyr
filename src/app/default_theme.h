#pragma once

#include "ui/theme.h"

extern const Theme DEFAULT_THEME;

#ifdef ECHO_BOARD_ECHO
extern const Theme ECHO_THEME;
extern const Theme ECHO_NOTICE_THEME;
#define BOARD_THEME ECHO_THEME
#define NOTICE_THEME ECHO_NOTICE_THEME
#else
#define BOARD_THEME DEFAULT_THEME
#define NOTICE_THEME DEFAULT_THEME
#endif
