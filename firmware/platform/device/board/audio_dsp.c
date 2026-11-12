#include "audio_dsp.h"

#include "core/seam.h"
#include "device/audio_dsd.h"
#include "device/audio_dsp.h"
#include "device/audio_ogg.h"
#include "device/audio_source.h"
#include "device/core1_source_private.h"
#include "overlay.h"

static const AudioModuleEntry *entry(void)
{
    return (const AudioModuleEntry *)(uintptr_t)(AUDIO_DSP_BASE + OVERLAY_HEADER_BYTES);
}

static CORE_COLD bool load(uint32_t number, uint32_t magic)
{
    const BoardOverlay overlay = {.image_offset = 0,
                                  .magic = magic,
                                  .base = (uint8_t *)(uintptr_t)AUDIO_DSP_BASE,
                                  .capacity = AUDIO_DSP_BYTES,
                                  .module = number};
    return board_overlay_load(&overlay);
}

CORE_COLD bool board_audio_dsp_prepare(void)
{
    if (entry()->magic == AUDIO_DSD_MAGIC) {
        return load(AUDIO_DSD_NUMBER, AUDIO_DSD_MAGIC);
    }
    return entry()->magic == AUDIO_OGG_MAGIC ? load(AUDIO_OGG_NUMBER, AUDIO_OGG_MAGIC)
                                             : load(AUDIO_DSP_NUMBER, AUDIO_DSP_MAGIC);
}

CORE_SEAM CORE_COLD bool board_audio_ogg_prepare(void)
{
    return load(AUDIO_OGG_NUMBER, AUDIO_OGG_MAGIC);
}

uint32_t board_audio_dsp_render(AudioResample *resample, uint32_t *words, uint32_t frames)
{
    return entry()->render(resample, words, frames);
}

CORE_SEAM CORE_COLD bool mp4_probe(FlacInput input, Mp4Track *track)
{
    const AudioDspEntry *dsp = (const AudioDspEntry *)entry();
    return load(AUDIO_DSP_NUMBER, AUDIO_DSP_MAGIC) && dsp->probe(input, track);
}

CORE_SEAM bool mp4_cursor_seek(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track,
                               uint32_t sample)
{
    return ((const AudioDspEntry *)entry())->seek(cursor, input, track, sample);
}

CORE_SEAM bool mp4_cursor_next(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track,
                               uint32_t *offset, uint32_t *size)
{
    return ((const AudioDspEntry *)entry())->next(cursor, input, track, offset, size);
}

CORE_SEAM bool core1_open_ogg(void *context, const char *path, uint32_t start_ms,
                              AudioTrackInfo *track)
{
    const AudioOggEntry *ogg = (const AudioOggEntry *)entry();
    return board_audio_ogg_prepare() && ogg->open(context, path, start_ms, track);
}

CORE_SEAM bool ogg_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const AudioOggEntry *ogg = (const AudioOggEntry *)entry();
    return entry()->magic == AUDIO_OGG_MAGIC && ogg->read(context, offset, buffer, length);
}

CORE_SEAM CORE_COLD bool core1_open_flac(void *context, const char *path, uint32_t start_ms,
                                         AudioTrackInfo *track)
{
    const AudioSourceEntry *source = (const AudioSourceEntry *)entry();
    return load(AUDIO_SOURCE_NUMBER, AUDIO_SOURCE_MAGIC) &&
           source->open_flac(context, path, start_ms, track);
}

CORE_SEAM bool aiff_probe(FlacInput input, WavFormat *format)
{
    const AudioDspEntry *dsp = (const AudioDspEntry *)entry();
    return load(AUDIO_DSP_NUMBER, AUDIO_DSP_MAGIC) && dsp->aiff(input, format);
}

CORE_SEAM bool core1_open_dsd(void *context, const char *path, uint32_t start_ms,
                              AudioTrackInfo *track)
{
    const AudioDsdEntry *dsd = (const AudioDsdEntry *)entry();
    return load(AUDIO_DSD_NUMBER, AUDIO_DSD_MAGIC) && dsd->open(context, path, start_ms, track);
}

CORE_SEAM bool dsd_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const AudioDsdEntry *dsd = (const AudioDsdEntry *)entry();
    return entry()->magic == AUDIO_DSD_MAGIC && dsd->read(context, offset, buffer, length);
}
