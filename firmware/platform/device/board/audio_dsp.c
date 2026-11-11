#include "audio_dsp.h"

#include "core/seam.h"
#include "device/audio_dsp.h"
#include "device/audio_vorbis.h"
#include "device/core1_source_private.h"
#include "overlay.h"

static const AudioModuleEntry *entry(void)
{
    return (const AudioModuleEntry *)(uintptr_t)(AUDIO_DSP_BASE + OVERLAY_HEADER_BYTES);
}

static bool load(uint32_t number, uint32_t magic)
{
    const BoardOverlay overlay = {.image_offset = 0,
                                  .magic = magic,
                                  .base = (uint8_t *)(uintptr_t)AUDIO_DSP_BASE,
                                  .capacity = AUDIO_DSP_BYTES,
                                  .module = number};
    return board_overlay_load(&overlay);
}

bool board_audio_dsp_prepare(void)
{
    return entry()->magic == AUDIO_VORBIS_MAGIC ? load(AUDIO_VORBIS_NUMBER, AUDIO_VORBIS_MAGIC)
                                                : load(AUDIO_DSP_NUMBER, AUDIO_DSP_MAGIC);
}

CORE_SEAM bool board_audio_vorbis_prepare(void)
{
    return load(AUDIO_VORBIS_NUMBER, AUDIO_VORBIS_MAGIC);
}

bool board_audio_dsp_pull(AudioResample *resample, int32_t *left, int32_t *right)
{
    return entry()->pull(resample, left, right);
}

CORE_SEAM bool mp4_probe(FlacInput input, Mp4Track *track)
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

CORE_SEAM bool core1_open_vorbis(void *context, const char *path, uint32_t start_ms,
                                 AudioTrackInfo *track)
{
    const AudioVorbisEntry *vorbis = (const AudioVorbisEntry *)entry();
    return board_audio_vorbis_prepare() && vorbis->open(context, path, start_ms, track);
}

CORE_SEAM bool vorbis_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    const AudioVorbisEntry *vorbis = (const AudioVorbisEntry *)entry();
    return entry()->magic == AUDIO_VORBIS_MAGIC && vorbis->read(context, offset, buffer, length);
}
