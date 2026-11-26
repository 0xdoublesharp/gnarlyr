#pragma once

#include "battery_model.h"

#include "core/bytes.h"
#include "core/record.h"

enum {
    SHOW_TIME_BYTE = 6,
    RESERVED_BYTE = 7,
    HABIT_FIELD = 8,
    RATE_FIELD = 10,
    LEARNED_FIELD = 18,
    PADDING_FIELD = 22,
    SEAL_FIELD = 28
};

static const RecordKind BATTERY_MODEL = {{'D', 'S', 'B', 'R'}, 1};

_Static_assert(LEARNED_FIELD == RATE_FIELD + 2 * BATTERY_LOAD_COUNT, "battery model rate layout");
_Static_assert(SEAL_FIELD + RECORD_SEAL_BYTES == BATTERY_MODEL_BYTES, "battery model size");
