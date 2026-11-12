#pragma once

#include "playback/listening_log.h"

enum {
    RECORD_SEQUENCE = 0,
    RECORD_TRACK = 4,
    RECORD_ALBUM = 8,
    RECORD_ARTIST = 12,
    RECORD_LENGTH = 16,
    RECORD_LISTENED = 20,
    RECORD_START = 24,
    RECORD_END = 28,
    RECORD_UPTIME = 32,
    RECORD_CLOCK = 36,
    RECORD_BOOT = 40,
    RECORD_INDEX = 44,
    RECORD_ENDED = 46,
    RECORD_OUTPUT = 47,
    RECORD_FORMAT = 48,
    RECORD_CLOCK_FLAGS = 49,
    RECORD_OFFSET = 50,
    RECORD_CHECK = 60
};

enum {
    CLOCK_UTC = 0x01
};

static inline uint32_t listening_record_offset(uint32_t sequence)
{
    return LISTENING_RING_OFFSET + sequence % LISTENING_LOG_CAPACITY * LISTENING_RECORD_BYTES;
}

static inline bool listening_kinds_valid(uint8_t end, uint8_t output, uint8_t format)
{
    return end < LISTENING_END_COUNT && output < LISTENING_OUTPUT_COUNT &&
           format < LISTENING_FORMAT_COUNT;
}
