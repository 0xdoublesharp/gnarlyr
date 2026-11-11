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
    if (ready && board_overlay_is_loaded(&overlay)) {
        return true;
    }
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

void audio_engine_stage_shape(AudioEngine *engine, uint32_t first, bool tail)
{
    if (ready) {
        entry()->shape(engine, first, tail);
    }
}

uint32_t audio_engine_stage_stretch(AudioEngine *engine, const int32_t *stage, uint32_t frames)
{
    return ready ? entry()->stretch(engine, stage, frames) : 0;
}

bool audio_tempo_stage_start(uint8_t *buffer, uint32_t frames, uint32_t rate, uint16_t speed)
{
    return ready && entry()->tempo_start(buffer, frames, rate, speed);
}

void audio_tempo_stage_finish(void)
{
    if (ready) {
        entry()->tempo_finish();
    }
}

uint32_t audio_tempo_stage_pending(uint32_t input)
{
    return ready ? entry()->tempo_pending(input) : 0;
}
