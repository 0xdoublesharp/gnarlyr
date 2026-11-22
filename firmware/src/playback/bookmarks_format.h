#pragma once

#include "core/record.h"

enum {
    VERSION = 1,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    COUNT_FIELD = 6,
    RECORDS_FIELD = 12,
    KEY_OFFSET = 0,
    TRACK_OFFSET = 4,
    POSITION_OFFSET = 8,
    CHAPTER_OFFSET = 12,
    FLAGS_OFFSET = 14,
    CHECKSUM_SIZE = 4,
    BITS_PER_BYTE = 8
};

static const RecordKind KIND = {{'D', 'S', 'B', 'K'}, VERSION};
