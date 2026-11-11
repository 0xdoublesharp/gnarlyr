#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/clock.h"

#define TIME_FILE_EARLIEST_UTC UINT32_C(1735689600)
#define TIME_FILE_LATEST_UTC UINT32_C(4102444799)

enum {
    TIME_FILE_BYTES = 32,
    TIME_FILE_VERSION = 1,
    TIME_FILE_OFFSET_WEST = -720,
    TIME_FILE_OFFSET_EAST = 840
};

extern const char TIME_FILE_PATH[];

bool time_file_parse(const uint8_t bytes[TIME_FILE_BYTES], HalWallTime *time);
