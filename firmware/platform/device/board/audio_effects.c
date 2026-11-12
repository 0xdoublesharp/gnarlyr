#include "audio_effects.h"

#include <stddef.h>

#include "core/seam.h"
#include "device/audio_effects_module.h"
#include "overlay.h"

static bool ready;

static const AudioEffectsEntry *entry(void)
{
    return (const AudioEffectsEntry *)(uintptr_t)(AUDIO_EFFECTS_BASE + OVERLAY_HEADER_BYTES);
}

CORE_SEAM CORE_COLD bool board_audio_effects_prepare(void)
{
    const BoardOverlay overlay = {.image_offset = 0,
                                  .magic = AUDIO_EFFECTS_MAGIC,
                                  .base = (uint8_t *)(uintptr_t)AUDIO_EFFECTS_BASE,
                                  .capacity = AUDIO_EFFECTS_BYTES,
                                  .module = AUDIO_EFFECTS_NUMBER};
    ready = board_overlay_load(&overlay) && entry()->magic == AUDIO_EFFECTS_MAGIC;
    return ready;
}

void board_audio_effects_forget(void)
{
    ready = false;
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

#ifdef ECHO_DEBUG_LOOP
const void *board_audio_effects_debug_code(void)
{
    return ready || board_audio_effects_prepare() ? entry()->debug : NULL;
}
#endif
