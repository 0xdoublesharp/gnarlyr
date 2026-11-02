#pragma once

#include "device/fat.h"

FatVolume *internal_volume_mount(void);
FatVolume *internal_volume_remount(void);
void internal_volume_reset(void);
