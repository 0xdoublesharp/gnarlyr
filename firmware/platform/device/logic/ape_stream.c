#include "device/ape_stream.h"

#include <stddef.h>

static void put(uint8_t *out, uint32_t value)
{
    for (uint32_t index = 0; index < 4; index++) {
        out[index] = (uint8_t)(value >> (index * 8));
    }
}

bool ape_stream_start(ApeStream *stream, FlacInput input, const ApeTrack *track, uint32_t frame)
{
    if (stream == NULL || track == NULL || frame >= track->frames || !ape_track_allowed(track)) {
        return false;
    }
    *stream = (ApeStream){
        .input = input, .track = *track, .frame = frame, .record_bytes = APE_CONFIG_BYTES};
    put(stream->header, track->version);
    put(stream->header + 4, track->level);
    put(stream->header + 8, track->bits);
    put(stream->header + 12, track->channels);
    put(stream->header + 16, track->rate);
    return true;
}

static bool next_record(ApeStream *stream)
{
    stream->configured = true;
    stream->record_position = 0;
    stream->record_bytes = APE_RECORD_BYTES;
    if (stream->frame == stream->track.frames) {
        for (uint32_t index = 0; index < APE_RECORD_BYTES; index++) {
            stream->header[index] = 0;
        }
        stream->ended = true;
        return true;
    }
    if (!ape_track_frame(stream->input, &stream->track, stream->frame, &stream->current)) {
        return false;
    }
    put(stream->header, stream->current.bytes);
    put(stream->header + 4, stream->current.samples);
    put(stream->header + 8, stream->current.skip);
    put(stream->header + 12, stream->frame * stream->track.frame_samples);
    stream->record_bytes += stream->current.bytes;
    stream->frame++;
    return true;
}

static bool read_part(ApeStream *stream, uint8_t *out, uint32_t length)
{
    uint32_t header = stream->configured ? APE_RECORD_BYTES : APE_CONFIG_BYTES;
    if (stream->record_position < header) {
        for (uint32_t index = 0; index < length; index++) {
            out[index] = stream->header[stream->record_position + index];
        }
        return true;
    }
    uint32_t at = stream->current.offset + stream->record_position - header;
    uint32_t available = at < stream->input.size ? stream->input.size - at : 0;
    uint32_t real = length < available ? length : available;
    if (real != 0 && !stream->input.read(stream->input.context, at, out, real)) {
        return false;
    }
    for (uint32_t index = real; index < length; index++) {
        out[index] = 0;
    }
    return true;
}

bool ape_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    ApeStream *stream = context;
    if (stream == NULL || stream->failed || offset != stream->position ||
        offset > APE_VIRTUAL_END || length > APE_VIRTUAL_END - offset ||
        (buffer == NULL && length != 0)) {
        return false;
    }
    uint8_t *out = buffer;
    while (length != 0) {
        if (stream->record_position == stream->record_bytes && !stream->ended &&
            !next_record(stream)) {
            stream->failed = true;
            return false;
        }
        uint32_t boundary = stream->configured ? APE_RECORD_BYTES : APE_CONFIG_BYTES;
        boundary = stream->record_position < boundary ? boundary : stream->record_bytes;
        uint32_t part = stream->record_position < boundary ? boundary - stream->record_position : 0;
        part = part < length ? part : length;
        if (stream->ended && part == 0) {
            part = length;
            for (uint32_t index = 0; index < part; index++) {
                out[index] = 0;
            }
        } else if (!read_part(stream, out, part)) {
            stream->failed = true;
            return false;
        }
        if (stream->record_position < stream->record_bytes) {
            stream->record_position += part;
        }
        stream->position += part;
        out += part;
        length -= part;
    }
    return true;
}
