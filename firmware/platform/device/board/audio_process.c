#include "audio_process.h"

#include <stddef.h>

#include "audio_effects.h"

#include "core/seam.h"
#include "device/audio_process.h"
#include "overlay.h"

static const AudioProcessEntry *active;

CORE_SEAM CORE_COLD bool board_audio_process_prepare(void)
{
    const BoardOverlay overlay = {.image_offset = 0,
                                  .magic = AUDIO_PROCESS_MAGIC,
                                  .base = (uint8_t *)(uintptr_t)AUDIO_PROCESS_BASE,
                                  .capacity = AUDIO_PROCESS_BYTES,
                                  .module = AUDIO_PROCESS_NUMBER};
    if (active == NULL || !board_overlay_is_loaded(&overlay)) {
        const AudioProcessEntry *entry =
            (const void *)(uintptr_t)(AUDIO_PROCESS_BASE + OVERLAY_HEADER_BYTES);
        active = board_overlay_load(&overlay) && entry->magic == AUDIO_PROCESS_MAGIC ? entry : NULL;
    }
    return active != NULL && board_audio_effects_prepare();
}

CORE_COLD void board_audio_process_forget(void)
{
    active = NULL;
    board_audio_effects_forget();
}

CORE_COLD bool equalizer_design(Equalizer *equalizer, const HalAudioEqualizer *curve, uint32_t rate)
{
    if (active == NULL) {
        *equalizer = (Equalizer){0};
        return false;
    }
    return active->design(equalizer, curve, rate);
}

CORE_COLD bool equalizer_retune(Equalizer *equalizer, EqualizerChange change, uint32_t rate)
{
    return active != NULL ? active->retune(equalizer, change, rate)
                          : equalizer_design(equalizer, change.after, rate);
}

void equalizer_run(Equalizer *equalizer, uint32_t *words, uint32_t frames)
{
    if (active != NULL) {
        active->run(equalizer, words, frames);
    }
}

void audio_engine_stage_shape(AudioEngine *engine, uint32_t first, bool tail)
{
    if (active != NULL) {
        active->shape(engine, first, tail);
    }
}

uint32_t audio_engine_stage_stretch(AudioEngine *engine, const int32_t *stage, uint32_t frames)
{
    return active != NULL ? active->stretch(engine, stage, frames) : 0;
}

bool audio_tempo_stage_start(uint8_t *buffer, uint32_t frames, uint32_t rate, uint16_t speed)
{
    return active != NULL && active->tempo_start(buffer, frames, rate, speed);
}

void audio_tempo_stage_finish(void)
{
    if (active != NULL) {
        active->tempo_finish();
    }
}

uint32_t audio_tempo_stage_pending(uint32_t input)
{
    return active != NULL ? active->tempo_pending(input) : 0;
}

void audio_engine_set_sleep_remaining(AudioEngine *engine, uint32_t remaining_ms)
{
    if (active != NULL) {
        active->sleep(engine, remaining_ms);
    }
}

CORE_SEAM uint8_t audio_engine_repeat_mark(AudioEngine *engine, bool clear)
{
    return active != NULL ? active->repeat_mark(engine, clear) : 0;
}
