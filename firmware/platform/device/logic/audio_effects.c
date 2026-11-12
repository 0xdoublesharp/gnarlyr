#include "device/audio_effects.h"

#include "core/seam.h"

CORE_SEAM void audio_effects_design(AudioEffects *effects, uint32_t rate)
{
    effects->rate = rate;
    audio_crossfeed_design(&effects->crossfeed, effects->settings.crossfeed, rate);
    audio_skip_design(&effects->skip, rate);
    audio_boost_design(&effects->boost, rate);
}

static void run_piece(AudioEffects *effects, bool crossfeed, uint32_t *words, uint32_t frames)
{
    if (crossfeed) {
        audio_crossfeed_run(&effects->crossfeed, words, frames);
    } else {
        audio_boost_run(&effects->boost, words, frames);
    }
}

static void run_pieces(AudioEffects *effects, bool crossfeed, const AudioEffectsBlock *block,
                       uint32_t frames)
{
    uint32_t start = block->first % block->ring_frames;
    uint32_t piece = block->ring_frames - start;
    piece = piece < frames ? piece : frames;
    run_piece(effects, crossfeed, &block->ring[AUDIO_EFFECTS_CHANNELS * start], piece);
    if (piece < frames) {
        run_piece(effects, crossfeed, block->ring, frames - piece);
    }
}

static void crossfeed_block(AudioEffects *effects, const AudioEffectsBlock *block)
{
    uint8_t level = effects->settings.crossfeed;
    if (level != effects->crossfeed.level) {
        audio_crossfeed_design(&effects->crossfeed, level, effects->rate);
    }
    if (level != 0 && block->stereo && effects->crossfeed.direct != 0) {
        run_pieces(effects, true, block, block->frames);
    } else {
        effects->crossfeed.running = false;
    }
}

CORE_SEAM uint32_t audio_effects_run(AudioEffects *effects, const AudioEffectsBlock *block)
{
    if (block->frames == 0 || block->ring_frames == 0) {
        return block->frames;
    }
    crossfeed_block(effects, block);
    uint32_t frames = block->frames;
    if (effects->settings.skip_silence) {
        frames = audio_skip_run(&effects->skip, block);
    } else {
        effects->skip.run = 0;
        effects->skip.gain = AUDIO_SKIP_UNITY;
    }
    if (effects->settings.speech_boost) {
        run_pieces(effects, false, block, frames);
    } else {
        effects->boost.level = 0;
        effects->boost.gain = AUDIO_BOOST_UNITY;
    }
    return frames;
}

CORE_SEAM void audio_effects_settle(AudioEffects *effects, AudioSegment *segments, int count,
                                    uint32_t consumed)
{
    audio_skip_settle(&effects->skip, segments, count, consumed);
}
