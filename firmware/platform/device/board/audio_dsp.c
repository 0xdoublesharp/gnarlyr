#include "audio_dsp.h"

#include "core/seam.h"
#include "device/audio_dsp.h"
#include "overlay.h"

static const AudioDspEntry *entry(void)
{
    return (const AudioDspEntry *)(uintptr_t)(AUDIO_DSP_BASE + OVERLAY_HEADER_BYTES);
}

bool board_audio_dsp_prepare(void)
{
    const BoardOverlay overlay = {.image_offset = 0,
                                  .magic = AUDIO_DSP_MAGIC,
                                  .base = (uint8_t *)(uintptr_t)AUDIO_DSP_BASE,
                                  .capacity = AUDIO_DSP_BYTES,
                                  .module = AUDIO_DSP_NUMBER};
    return board_overlay_load(&overlay);
}

bool board_audio_dsp_pull(AudioResample *resample, int32_t *left, int32_t *right)
{
    return entry()->pull(resample, left, right);
}

CORE_SEAM bool mp4_probe(FlacInput input, Mp4Track *track)
{
    return board_audio_dsp_prepare() && entry()->probe(input, track);
}

CORE_SEAM bool mp4_cursor_seek(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track,
                               uint32_t sample)
{
    return entry()->seek(cursor, input, track, sample);
}

CORE_SEAM bool mp4_cursor_next(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track,
                               uint32_t *offset, uint32_t *size)
{
    return entry()->next(cursor, input, track, offset, size);
}
