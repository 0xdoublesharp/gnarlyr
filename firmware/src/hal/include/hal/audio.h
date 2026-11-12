#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/dac_identity.h"
#include "core/headphone_audio.h"
#include "core/parametric.h"

enum {
    HAL_AUDIO_VOLUME_MAX = 60,
    HAL_AUDIO_EQUALIZER_BANDS = 10,
    HAL_AUDIO_UNITY_PRE_GAIN = 32768,
    HAL_AUDIO_CROSSFEED_LEVELS = 4
};

typedef enum HalAudioState {
    HAL_AUDIO_STOPPED,
    HAL_AUDIO_PLAYING,
    HAL_AUDIO_PAUSED
} HalAudioState;

typedef enum HalAudioGain {
    HAL_AUDIO_GAIN_LOW,
    HAL_AUDIO_GAIN_HIGH
} HalAudioGain;

typedef enum HalAudioFilter {
    HAL_AUDIO_FILTER_FAST_LOW_LATENCY,
    HAL_AUDIO_FILTER_FAST_PHASE_COMPENSATED,
    HAL_AUDIO_FILTER_SLOW_LOW_LATENCY,
    HAL_AUDIO_FILTER_SLOW_PHASE_COMPENSATED,
    HAL_AUDIO_FILTER_NON_OVERSAMPLING
} HalAudioFilter;

typedef enum HalAudioOutput {
    HAL_AUDIO_OUTPUT_AUTO,
    HAL_AUDIO_OUTPUT_SINGLE_ENDED,
    HAL_AUDIO_OUTPUT_BALANCED
} HalAudioOutput;

typedef enum HalAudioJack {
    HAL_AUDIO_JACK_NONE,
    HAL_AUDIO_JACK_SINGLE_ENDED,
    HAL_AUDIO_JACK_BALANCED
} HalAudioJack;

typedef enum HalAudioProblem {
    HAL_AUDIO_PROBLEM_NONE,
    HAL_AUDIO_PROBLEM_UNREADABLE,
    HAL_AUDIO_PROBLEM_FORMAT,
    HAL_AUDIO_PROBLEM_RATE,
    HAL_AUDIO_PROBLEM_AAC_ONLY,
    HAL_AUDIO_PROBLEM_APE,
    HAL_AUDIO_PROBLEM_WMA
} HalAudioProblem;

typedef struct HalAudioRefusal {
    HalAudioProblem problem;
    uint32_t sample_rate;
} HalAudioRefusal;

typedef enum HalAudioCodec {
    HAL_AUDIO_CODEC_UNKNOWN,
    HAL_AUDIO_CODEC_FLAC,
    HAL_AUDIO_CODEC_MP3,
    HAL_AUDIO_CODEC_WAV,
    HAL_AUDIO_CODEC_AAC,
    HAL_AUDIO_CODEC_ALAC,
    HAL_AUDIO_CODEC_VORBIS,
    HAL_AUDIO_CODEC_OPUS,
    HAL_AUDIO_CODEC_DSD,
    HAL_AUDIO_CODEC_AIFF,
    HAL_AUDIO_CODEC_APE,
    HAL_AUDIO_CODEC_WMA
} HalAudioCodec;

typedef struct HalAudioFormat {
    HalAudioCodec codec;
    uint32_t sample_rate;
    uint8_t bits_per_sample;
    uint8_t channels;
    uint32_t bitrate_kbps;
} HalAudioFormat;

typedef struct HalAudioStatus {
    HalAudioState state;
    uint32_t token;
    uint32_t position_ms;
    bool failed;
    bool idle;
    HalAudioFormat format;
} HalAudioStatus;

typedef struct HalAudioStats {
    uint32_t buffered_ms;
    uint32_t underruns;
    uint32_t output_underruns;
    uint32_t blocks;
    uint32_t decode_us;
    uint32_t decode_max_us;
    uint32_t decode_total_us;
    uint32_t read_us;
    uint32_t read_max_us;
    uint32_t read_total_us;
    uint32_t busy_us;
    uint32_t elapsed_us;
} HalAudioStats;

typedef enum HalToneResult {
    HAL_TONE_PLAYED,
    HAL_TONE_NO_DAC,
    HAL_TONE_SETUP_FAILED,
    HAL_TONE_NOT_READY,
    HAL_TONE_STALLED,
    HAL_TONE_NO_DMAC
} HalToneResult;

typedef struct HalToneDetail {
    uint8_t stage;
    uint8_t pending;
} HalToneDetail;

typedef struct HalDmaToneStatus {
    bool running;
    uint32_t position;
} HalDmaToneStatus;

typedef struct HalJackPin {
    bool high;
    uint8_t boot_function;
    bool boot_output;
    bool pull;
} HalJackPin;

typedef struct HalJackPins {
    HalJackPin single_ended;
    HalJackPin balanced;
} HalJackPins;

typedef struct HalAudioTrack {
    const char *path;
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t token;
    uint32_t replay_gain;
    uint32_t fade_ms;
} HalAudioTrack;

typedef struct HalAudioEqualizer {
    uint16_t band_hz[HAL_AUDIO_EQUALIZER_BANDS];
    int8_t gain_half_db[HAL_AUDIO_EQUALIZER_BANDS];
    uint8_t band_count;
    uint16_t pre_gain;
    bool is_parametric;
    ParametricEq parametric;
} HalAudioEqualizer;

typedef struct HalAudioEffects {
    uint8_t crossfeed;
    bool skip_silence;
    bool speech_boost;
} HalAudioEffects;

bool hal_audio_play(const HalAudioTrack *track, uint32_t offset_ms, const HalAudioTrack *next);
void hal_audio_queue_next(const HalAudioTrack *track);
void hal_audio_pause(void);
void hal_audio_resume(void);
void hal_audio_release(void);
void hal_audio_seek(uint32_t position_ms);
void hal_audio_set_volume(uint8_t level);
void hal_audio_set_bluetooth_volume(uint8_t level);
void hal_audio_set_headphones(HeadphoneAudio profile);
HeadphoneAudio hal_audio_headphones(void);
void hal_audio_set_gain(HalAudioGain gain);
void hal_audio_set_filter(HalAudioFilter filter);
void hal_audio_set_output(HalAudioOutput output);
void hal_audio_set_equalizer(const HalAudioEqualizer *equalizer);
void hal_audio_set_effects(HalAudioEffects effects);
HalAudioStatus hal_audio_status(void);
HalAudioStats hal_audio_stats(void);
void hal_audio_reset_stats(void);
void hal_audio_service(void);
HalToneResult hal_audio_test_tone(void);
HalToneDetail hal_audio_tone_detail(void);
DacIdentity hal_audio_dac_identity(int chip);
HalToneResult hal_audio_dma_tone_start(uint32_t sample_rate);
void hal_audio_dma_tone_stop(void);
HalDmaToneStatus hal_audio_dma_tone_status(void);
HalJackPins hal_audio_jack_pins(void);
HalAudioJack hal_audio_jack(void);
HalAudioRefusal hal_audio_refusal(void);

void hal_audio_set_replay_gain(uint32_t token, uint32_t gain);
void hal_audio_set_speed(uint16_t speed);
