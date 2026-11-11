#include "device/audio_effects.h"

#include "device/audio_engine.h"

enum {
    SAMPLE_SHIFT = 8,
    GAIN_SHIFT = 15,
    GAIN_ROUNDING = 1 << (GAIN_SHIFT - 1)
};

void audio_skip_design(AudioSkip *skip, uint32_t rate)
{
    uint32_t fade = rate / AUDIO_SKIP_FADE_DIVISOR;
    fade = fade > 0 ? fade : 1;
    *skip = (AudioSkip){.keep = rate / AUDIO_SKIP_KEEP_DIVISOR,
                        .fade = fade,
                        .step = (AUDIO_SKIP_UNITY + fade - 1) / fade,
                        .gain = AUDIO_SKIP_UNITY};
}

static bool quiet_sample(uint32_t word)
{
    int32_t sample = (int32_t)word >> SAMPLE_SHIFT;
    return sample < AUDIO_SKIP_THRESHOLD && sample > -AUDIO_SKIP_THRESHOLD;
}

static bool note_drop(AudioSkip *skip, uint32_t at)
{
    if (skip->marks > 0 && skip->mark[skip->marks - 1].at == at) {
        skip->mark[skip->marks - 1].frames++;
        return true;
    }
    if (skip->marks == AUDIO_EFFECTS_MARKS) {
        return false;
    }
    skip->mark[skip->marks++] = (AudioSkipMark){.at = at, .frames = 1};
    return true;
}

static uint32_t shaped(uint32_t word, uint32_t gain)
{
    int32_t sample = (int32_t)word >> SAMPLE_SHIFT;
    int32_t value = (int32_t)(((int64_t)sample * gain + GAIN_ROUNDING) >> GAIN_SHIFT);
    return (uint32_t)value << SAMPLE_SHIFT;
}

static void follow_gain(AudioSkip *skip)
{
    if (skip->run + skip->fade > skip->keep) {
        skip->gain = skip->gain > skip->step ? skip->gain - skip->step : 0;
    } else if (skip->gain < AUDIO_SKIP_UNITY) {
        skip->gain =
            skip->gain + skip->step < AUDIO_SKIP_UNITY ? skip->gain + skip->step : AUDIO_SKIP_UNITY;
    }
}

static bool dropped(AudioSkip *skip, const uint32_t *frame, bool may_drop, uint32_t at)
{
    bool quiet = quiet_sample(frame[0]) && quiet_sample(frame[1]);
    skip->run = quiet ? skip->run + 1 : 0;
    if (skip->run > skip->keep && may_drop && note_drop(skip, at)) {
        skip->gain = 0;
        return true;
    }
    follow_gain(skip);
    return false;
}

uint32_t audio_skip_run(AudioSkip *skip, const AudioEffectsBlock *block)
{
    uint32_t read = block->first % block->ring_frames;
    uint32_t write = read;
    uint32_t kept = 0;
    bool may_drop = block->may_skip && block->buffered >= block->ring_frames / 2;
    for (uint32_t index = 0; index < block->frames; index++) {
        uint32_t *frame = &block->ring[AUDIO_EFFECTS_CHANNELS * read];
        read = read + 1 == block->ring_frames ? 0 : read + 1;
        if (dropped(skip, frame, may_drop, block->first + kept)) {
            continue;
        }
        uint32_t *target = &block->ring[AUDIO_EFFECTS_CHANNELS * write];
        bool full = skip->gain == AUDIO_SKIP_UNITY;
        target[0] = full ? frame[0] : shaped(frame[0], skip->gain);
        target[1] = full ? frame[1] : shaped(frame[1], skip->gain);
        write = write + 1 == block->ring_frames ? 0 : write + 1;
        kept++;
    }
    return kept;
}

static void credit(AudioSegment *segments, int count, AudioSkipMark mark)
{
    int owner = 0;
    for (int index = 1; index < count; index++) {
        owner = (int32_t)(mark.at - segments[index].start) > 0 ? index : owner;
    }
    if (count > 0 && (int32_t)(mark.at - segments[owner].start) >= 0) {
        segments[owner].skipped += mark.frames;
    }
}

void audio_skip_settle(AudioSkip *skip, AudioSegment *segments, int count, uint32_t consumed)
{
    while (skip->marks > 0 && (int32_t)(consumed - skip->mark[0].at) >= 0) {
        credit(segments, count, skip->mark[0]);
        for (uint32_t index = 1; index < skip->marks; index++) {
            skip->mark[index - 1] = skip->mark[index];
        }
        skip->marks--;
    }
}
