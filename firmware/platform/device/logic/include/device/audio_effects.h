#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/audio.h"

enum {
    AUDIO_EFFECTS_MARKS = 4,
    AUDIO_EFFECTS_CHANNELS = 2,
    AUDIO_CROSSFEED_FRACTION_BITS = 30,
    AUDIO_CROSSFEED_STATE_BITS = 6,
    AUDIO_SKIP_THRESHOLD = 40000,
    AUDIO_SKIP_KEEP_DIVISOR = 4,
    AUDIO_SKIP_FADE_DIVISOR = 200,
    AUDIO_SKIP_UNITY = 32768,
    AUDIO_BOOST_UNITY = 65536,
    AUDIO_BOOST_MAX_GAIN = 4 * AUDIO_BOOST_UNITY,
    AUDIO_BOOST_FLOOR = 8192,
    AUDIO_BOOST_TARGET_BITS = 22,
    AUDIO_BOOST_CEILING = 7864320,
    AUDIO_BOOST_RELEASE_SHIFT = 9
};

typedef struct AudioCrossfeed {
    int32_t direct;
    int32_t direct_previous;
    int32_t direct_pole;
    int32_t cross;
    int32_t cross_pole;
    int32_t input[AUDIO_EFFECTS_CHANNELS];
    int32_t high[AUDIO_EFFECTS_CHANNELS];
    int32_t low[AUDIO_EFFECTS_CHANNELS];
    uint8_t level;
    bool running;
} AudioCrossfeed;

typedef struct AudioSkipMark {
    uint32_t at;
    uint32_t frames;
} AudioSkipMark;

typedef struct AudioSkip {
    uint32_t keep;
    uint32_t fade;
    uint32_t step;
    uint32_t run;
    uint32_t gain;
    uint32_t marks;
    AudioSkipMark mark[AUDIO_EFFECTS_MARKS];
} AudioSkip;

typedef struct AudioBoost {
    uint32_t level;
    uint32_t gain;
    uint8_t shift;
} AudioBoost;

typedef struct AudioEffects {
    HalAudioEffects settings;
    uint32_t rate;
    AudioCrossfeed crossfeed;
    AudioSkip skip;
    AudioBoost boost;
} AudioEffects;

typedef struct AudioEffectsBlock {
    uint32_t *ring;
    uint32_t ring_frames;
    uint32_t first;
    uint32_t frames;
    uint32_t buffered;
    bool stereo;
    bool may_skip;
} AudioEffectsBlock;

typedef struct AudioSegment AudioSegment;

void audio_crossfeed_design(AudioCrossfeed *crossfeed, uint8_t level, uint32_t rate);
void audio_crossfeed_run(AudioCrossfeed *crossfeed, uint32_t *words, uint32_t frames);
void audio_skip_design(AudioSkip *skip, uint32_t rate);
uint32_t audio_skip_run(AudioSkip *skip, const AudioEffectsBlock *block);
void audio_skip_settle(AudioSkip *skip, AudioSegment *segments, int count, uint32_t consumed);
void audio_boost_design(AudioBoost *boost, uint32_t rate);
void audio_boost_run(AudioBoost *boost, uint32_t *words, uint32_t frames);
uint32_t audio_boost_target(uint32_t level);

void audio_effects_design(AudioEffects *effects, uint32_t rate);
uint32_t audio_effects_run(AudioEffects *effects, const AudioEffectsBlock *block);
void audio_effects_settle(AudioEffects *effects, AudioSegment *segments, int count,
                          uint32_t consumed);
