#pragma once

#include "device/fat.h"

void storage_reset(void);
FatVolume *board_music_volume(void);
FatVolume *board_route_volume(const char **path);
