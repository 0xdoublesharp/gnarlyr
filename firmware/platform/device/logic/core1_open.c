#include "device/core1_source_private.h"
#include "device/dsd_stream.h"

bool core1_open(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    static bool (*const OPENS[])(void *, const char *, uint32_t, AudioTrackInfo *) = {
        [CORE1_FORMAT_FLAC] = core1_open_flac, [CORE1_FORMAT_MP3] = core1_open_mp3,
        [CORE1_FORMAT_M4A] = core1_open_m4a,   [CORE1_FORMAT_VORBIS] = core1_open_ogg,
        [CORE1_FORMAT_OPUS] = core1_open_ogg,  [CORE1_FORMAT_DSD] = core1_open_dsd,
        [CORE1_FORMAT_APE] = core1_open_ape,   [CORE1_FORMAT_WMA] = core1_open_wma,
    };
    const Core1Source *source = context;
    return OPENS[source->format](context, path, start_ms, track);
}

bool core1_ogg_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    return ogg_stream_read(context, offset, buffer, length);
}

bool core1_module_read(const Core1Source *source, void *buffer, uint32_t length)
{
    static bool (*const READS[])(void *, uint32_t, void *, uint32_t) = {
        [CORE1_FORMAT_M4A] = m4a_stream_read,
        [CORE1_FORMAT_DSD] = dsd_stream_read,
        [CORE1_FORMAT_APE] = ape_stream_read,
        [CORE1_FORMAT_WMA] = asf_stream_read,
    };
    return READS[source->format](source->input.context, source->written, buffer, length);
}
