#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/replay_gain.h"

enum {
    REPLAY_GAIN_TEXT_CAPACITY = 65
};

typedef enum ReplayGainField {
    REPLAY_GAIN_TRACK_GAIN,
    REPLAY_GAIN_ALBUM_GAIN,
    REPLAY_GAIN_TRACK_PEAK,
    REPLAY_GAIN_ALBUM_PEAK,
    REPLAY_GAIN_NO_FIELD
} ReplayGainField;

typedef struct ReplayGainTags {
    int16_t gain[2];
    uint32_t peak[2];
    uint8_t present;
} ReplayGainTags;

ReplayGainField replay_gain_field(const char *key, uint32_t length);
bool replay_gain_offer(ReplayGainTags *tags, ReplayGainField field, const char *text,
                       uint32_t length);
uint32_t replay_gain_amplitude(int16_t centi_db);
ReplayGain replay_gain_finish(const ReplayGainTags *tags);
