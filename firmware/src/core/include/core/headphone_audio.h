#pragma once

#include <stdint.h>

#include "core/replay_gain.h"

enum {
    HEADPHONE_AUDIO_VOLUME_MAX = 60
};

typedef struct HeadphoneAudio {
    uint32_t left;
    uint32_t right;
    uint8_t ceiling;
} HeadphoneAudio;

HeadphoneAudio headphone_audio_defaults(void);
uint8_t headphone_audio_limit(HeadphoneAudio profile, uint8_t requested);
uint8_t headphone_audio_switch_volume(HeadphoneAudio before, HeadphoneAudio after, uint8_t level);
uint8_t headphone_audio_rise_volume(HeadphoneAudio active, uint8_t level, uint8_t rise_db);
uint32_t headphone_audio_gain(uint32_t profile_gain, uint32_t input_gain);
int32_t headphone_audio_sample(int32_t sample, uint8_t bits, uint32_t gain);
uint32_t headphone_audio_word(uint32_t word, uint32_t gain);
