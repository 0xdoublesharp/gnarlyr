#pragma once

#include "core/record.h"

enum {
    VERSION = 1,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    COUNT_FIELD = 6,
    KEYS_FIELD = 8,
    KEY_SIZE = 4,
    CHECKSUM_SIZE = 4,
    BITS_PER_BYTE = 8
};

static const RecordKind KIND = {{'D', 'S', 'F', 'V'}, VERSION};
