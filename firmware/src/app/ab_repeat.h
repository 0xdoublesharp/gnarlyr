#pragma once

#include <stdint.h>

#include "core/power_policy.h"
#include "player.h"
#include "ui/toast.h"

PlayerMark ab_repeat_step(Player *player, PowerPolicy *power, Toast *toast, uint32_t now_ms);
