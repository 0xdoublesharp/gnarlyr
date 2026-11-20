#include "audio_dsp.h"

#include "core/seam.h"
#include "device/audio_ape_wma.h"
#include "device/audio_dsd.h"
#include "device/audio_dsp.h"
#include "device/audio_ogg.h"
#include "device/audio_source.h"
#include "device/core1_source_private.h"
#include "overlay.h"

enum {
    ROUTE_NUMBER = 0x0F,
    ROUTE_SECOND = 0x10,
    ROUTE_OPEN_SHIFT = 4
};

static const uint8_t ROUTES[] = {
    [CORE1_FORMAT_FLAC] = AUDIO_SOURCE_NUMBER,
    [CORE1_FORMAT_MP3] = AUDIO_SOURCE_NUMBER | ROUTE_SECOND,
    [CORE1_FORMAT_M4A] = AUDIO_DSP_NUMBER,
    [CORE1_FORMAT_VORBIS] = AUDIO_OGG_NUMBER,
    [CORE1_FORMAT_OPUS] = AUDIO_OGG_NUMBER,
    [CORE1_FORMAT_DSD] = AUDIO_DSD_NUMBER,
    [CORE1_FORMAT_APE] = AUDIO_APE_WMA_NUMBER,
    [CORE1_FORMAT_WMA] = AUDIO_APE_WMA_NUMBER | ROUTE_SECOND,
};

static const AudioModuleEntry *entry(void)
{
    return (const AudioModuleEntry *)(uintptr_t)(AUDIO_DSP_BASE + OVERLAY_HEADER_BYTES);
}

static const AudioDspEntry *dsp(void)
{
    return (const AudioDspEntry *)entry();
}

static CORE_COLD bool load(uint32_t number)
{
    const BoardOverlay overlay = {.image_offset = 0,
                                  .magic = AUDIO_MODULE_MAGIC(number),
                                  .base = (uint8_t *)(uintptr_t)AUDIO_DSP_BASE,
                                  .capacity = AUDIO_DSP_BYTES,
                                  .module = number};
    return board_overlay_load(&overlay);
}

CORE_COLD bool board_audio_dsp_prepare(void)
{
    uint32_t magic = entry()->magic;
    bool keep =
        magic == AUDIO_APE_WMA_MAGIC || magic == AUDIO_DSD_MAGIC || magic == AUDIO_OGG_MAGIC;
    return load(keep ? magic >> 24 : AUDIO_DSP_NUMBER);
}

uint32_t board_audio_dsp_render(AudioResample *resample, uint32_t *words, uint32_t frames)
{
    return entry()->render(resample, words, frames);
}

CORE_COLD bool core1_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    uint32_t route = ROUTES[((const Core1Source *)context)->format];
    return load(route & ROUTE_NUMBER) &&
           entry()->open[route >> ROUTE_OPEN_SHIFT](context, path, start_ms, track);
}

CORE_SEAM CORE_COLD bool mp4_probe(FlacInput input, Mp4Track *track)
{
    track->unsupported = false;
    return load(AUDIO_DSP_NUMBER) && dsp()->probe(input, track);
}

CORE_SEAM bool mp4_cursor_seek(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track,
                               uint32_t sample)
{
    return dsp()->seek(cursor, input, track, sample);
}

CORE_SEAM bool mp4_cursor_next(Mp4Cursor *cursor, FlacInput input, const Mp4Track *track,
                               uint32_t *offset, uint32_t *size)
{
    return dsp()->next(cursor, input, track, offset, size);
}

bool m4a_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    return entry()->magic == AUDIO_DSP_MAGIC && entry()->read(context, offset, buffer, length);
}

CORE_SEAM bool ogg_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    return entry()->magic == AUDIO_OGG_MAGIC && entry()->read(context, offset, buffer, length);
}

CORE_SEAM bool aiff_probe(FlacInput input, WavFormat *format)
{
    return load(AUDIO_DSP_NUMBER) && dsp()->aiff(input, format);
}
