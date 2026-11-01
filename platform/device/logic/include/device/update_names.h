#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/updater.h"

enum {
    UPDATE_NAME_CAPACITY = 16
};

uint32_t update_name_count(UpdateBoard board);
bool update_name_at(UpdateBoard board, uint32_t index, char name[UPDATE_NAME_CAPACITY]);
