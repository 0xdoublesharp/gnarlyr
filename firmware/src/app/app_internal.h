#pragma once

#include "player.h"

static const char LOCKED_HINT[] = "Hold Play+Power";

PlayerStatus app_player_status(void);
const char *app_toast_text(void);
const char *app_diagnostics_value(int index);
