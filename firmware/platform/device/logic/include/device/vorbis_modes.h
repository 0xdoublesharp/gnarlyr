#pragma once

#include <stdint.h>

typedef struct VorbisModes {
    uint32_t short_block;
    uint32_t long_block;
    uint32_t count;
    uint32_t long_modes[2];
} VorbisModes;
