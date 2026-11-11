#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings/settings.h"

typedef struct WallNow {
    bool known;
    uint32_t local_s;
} WallNow;

WallNow wall_time_now(void);
bool wall_time_24h(const Settings *settings);
