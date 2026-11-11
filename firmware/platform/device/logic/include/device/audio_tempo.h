#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    AUDIO_TEMPO_CHANNEL_BYTES = 3,
    AUDIO_TEMPO_FRAME_BYTES = 6
};

typedef struct AudioTempoBuffer {
    uint8_t *first;
    uint8_t *second;
    uint16_t first_frames;
    uint16_t second_frames;
} AudioTempoBuffer;

typedef struct AudioTempoConfig {
    AudioTempoBuffer buffer;
    uint32_t rate;
    uint16_t speed;
} AudioTempoConfig;

typedef struct AudioTempo {
    AudioTempoConfig config;
    uint64_t input_frames;
    uint64_t output_frames;
    uint64_t limit;
    uint64_t credit;
    uint16_t credit_remainder;
    uint32_t copy_frames;
    uint32_t mix_step;
    uint32_t remainder;
    int32_t period_error;
    int32_t period_fraction;
    uint16_t cached_period;
    uint16_t analysis_frames;
    uint16_t read;
    uint16_t write;
    uint16_t available;
    uint16_t capacity;
    uint16_t period;
    uint16_t job_index;
    uint8_t job;
    bool finished;
} AudioTempo;

uint32_t audio_tempo_buffer_frames(uint32_t rate);
bool audio_tempo_start(AudioTempo *tempo, AudioTempoConfig config);
bool audio_tempo_push(AudioTempo *tempo, int32_t left, int32_t right);
bool audio_tempo_pull(AudioTempo *tempo, int32_t *left, int32_t *right);
void audio_tempo_finish(AudioTempo *tempo);
bool audio_tempo_done(const AudioTempo *tempo);

enum {
    AUDIO_TEMPO_STAGE_SPARE_FRAMES = 669
};

bool audio_tempo_stage_start(uint8_t *buffer, uint32_t frames, uint32_t rate, uint16_t speed);
uint32_t audio_tempo_stage_push(const int32_t *samples, uint32_t frames);
uint32_t audio_tempo_stage_pull(int32_t *samples, uint32_t frames, int shift);
void audio_tempo_stage_finish(void);
uint32_t audio_tempo_stage_pending(uint32_t input);
