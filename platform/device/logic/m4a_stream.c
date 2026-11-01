#include "device/m4a_stream.h"

static void put_length(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

bool m4a_stream_start(M4aStream *stream, FlacInput file, const Mp4Track *track,
                      uint32_t first_sample)
{
    stream->file = file;
    stream->track = *track;
    stream->position = 0;
    stream->record_done = 0;
    stream->head_size = 0;
    stream->body_size = 0;
    stream->configured = false;
    stream->ended = false;
    return mp4_cursor_seek(&stream->cursor, file, track, first_sample);
}

static void begin_config(M4aStream *stream)
{
    uint32_t size = 1u + stream->track.config_size;
    put_length(stream->head, size);
    stream->head[M4A_RECORD_HEADER_BYTES] = stream->track.codec;
    for (uint32_t index = 0; index < stream->track.config_size; index++) {
        stream->head[M4A_RECORD_HEADER_BYTES + 1 + index] = stream->track.config[index];
    }
    stream->head_size = M4A_RECORD_HEADER_BYTES + size;
    stream->body_size = 0;
    stream->configured = true;
}

static void begin_end(M4aStream *stream)
{
    put_length(stream->head, 0);
    stream->head_size = M4A_RECORD_HEADER_BYTES;
    stream->body_size = 0;
    stream->ended = true;
}

static bool begin_sample(M4aStream *stream)
{
    uint32_t offset;
    uint32_t size = 0;
    while (size == 0) {
        if (!mp4_cursor_next(&stream->cursor, stream->file, &stream->track, &offset, &size)) {
            begin_end(stream);
            return true;
        }
    }
    if (offset > stream->file.size || size > stream->file.size - offset) {
        return false;
    }
    put_length(stream->head, size);
    stream->head_size = M4A_RECORD_HEADER_BYTES;
    stream->body_size = size;
    stream->body_offset = offset;
    return true;
}

static bool begin_record(M4aStream *stream)
{
    stream->record_done = 0;
    if (!stream->configured) {
        begin_config(stream);
        return true;
    }
    return begin_sample(stream);
}

static uint32_t read_body(M4aStream *stream, uint32_t offset, uint8_t *out, uint32_t length)
{
    return stream->file.read(stream->file.context, offset, out, length) ? length : 0;
}

static uint32_t emit(M4aStream *stream, uint8_t *out, uint32_t length)
{
    if (stream->record_done < stream->head_size) {
        uint32_t left = stream->head_size - stream->record_done;
        uint32_t count = length < left ? length : left;
        for (uint32_t index = 0; index < count; index++) {
            out[index] = stream->head[stream->record_done + index];
        }
        return count;
    }
    uint32_t into = stream->record_done - stream->head_size;
    uint32_t left = stream->body_size - into;
    return read_body(stream, stream->body_offset + into, out, length < left ? length : left);
}

static void fill_zeros(uint8_t *out, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        out[index] = 0;
    }
}

bool m4a_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    M4aStream *stream = context;
    uint8_t *out = buffer;
    if (offset != stream->position) {
        return false;
    }
    while (length > 0) {
        if (stream->ended && stream->record_done == stream->head_size) {
            fill_zeros(out, length);
            stream->position += length;
            return true;
        }
        if (stream->record_done == stream->head_size + stream->body_size && !begin_record(stream)) {
            return false;
        }
        uint32_t count = emit(stream, out, length);
        if (count == 0) {
            return false;
        }
        stream->record_done += count;
        stream->position += count;
        out += count;
        length -= count;
    }
    return true;
}
