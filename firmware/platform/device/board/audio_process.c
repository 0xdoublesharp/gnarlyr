#include "audio_process.h"

#include "core/seam.h"
#include "device/audio_process.h"
#include "overlay.h"

static bool ready;

static const AudioProcessEntry *entry(void)
{
    return (const AudioProcessEntry *)(uintptr_t)(AUDIO_PROCESS_BASE + OVERLAY_HEADER_BYTES);
}

CORE_SEAM bool board_audio_process_prepare(void)
{
    const BoardOverlay overlay = {.image_offset = 0,
                                  .magic = AUDIO_PROCESS_MAGIC,
                                  .base = (uint8_t *)(uintptr_t)AUDIO_PROCESS_BASE,
                                  .capacity = AUDIO_PROCESS_BYTES,
                                  .module = AUDIO_PROCESS_NUMBER};
    ready = board_overlay_load(&overlay) && entry()->magic == AUDIO_PROCESS_MAGIC;
    return ready;
}

void board_audio_process_forget(void)
{
    ready = false;
}

bool equalizer_design(Equalizer *equalizer, const HalAudioEqualizer *curve, uint32_t rate)
{
    if (!ready) {
        *equalizer = (Equalizer){0};
        return false;
    }
    return entry()->design(equalizer, curve, rate);
}

bool equalizer_retune(Equalizer *equalizer, EqualizerChange change, uint32_t rate)
{
    return ready ? entry()->retune(equalizer, change, rate)
                 : equalizer_design(equalizer, change.after, rate);
}

void equalizer_run(Equalizer *equalizer, uint32_t *words, uint32_t frames)
{
    if (ready) {
        entry()->run(equalizer, words, frames);
    }
}

void audio_fade_in(AudioFadeBlock block, AudioFadeRamp *ramp)
{
    if (ready) {
        entry()->fade_in(block, ramp);
    }
}

void audio_fade_out(AudioFadeBlock block, AudioFadeRamp *ramp, int64_t remaining)
{
    if (ready) {
        entry()->fade_out(block, ramp, remaining);
    }
}

void audio_shape_block(AudioEngine *engine, uint32_t first, bool tail)
{
    if (ready) {
        entry()->shape(engine, first, tail);
    }
}

void audio_engine_set_sleep_remaining(AudioEngine *engine, uint32_t remaining_ms)
{
    if (ready) {
        entry()->sleep(engine, remaining_ms);
    }
}

CORE_SEAM uint8_t audio_engine_repeat_mark(AudioEngine *engine, bool clear)
{
    return ready ? entry()->repeat_mark(engine, clear) : 0;
}

void audio_convert_pending(const AudioEngine *engine, uint32_t *words, uint32_t frames, int shift)
{
    if (ready) {
        entry()->convert(engine, words, frames, shift);
    }
}
