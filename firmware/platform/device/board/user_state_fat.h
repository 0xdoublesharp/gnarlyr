#pragma once

#include <stdint.h>

#include "device/fat.h"
#include "settings/user_state.h"

typedef struct UserStateDisk {
    FatVolume *internal;
    FatVolume *music;
} UserStateDisk;

UserStateIo user_state_fat_io(UserStateDisk *disk, uint8_t *buffer, uint32_t buffer_bytes);
