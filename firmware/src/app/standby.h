#pragma once

#include "core/power_policy.h"
#include "player.h"
#include "settings/settings.h"

#include "core/standby.h"

void standby_follow(Standby *standby, PowerPolicy *power, const Settings *settings);
void standby_frame(Standby *standby);
