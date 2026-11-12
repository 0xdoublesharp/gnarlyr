#include "device/asf_stream.h"

#include <stddef.h>

static void put(uint8_t *bytes, uint32_t value)
{
    for (uint32_t index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint32_t take(AsfStream *stream, uint32_t count)
{
    if (stream->cursor > stream->packet_end || count > stream->packet_end - stream->cursor) {
        stream->failed = true;
        return 0;
    }
    uint32_t value = 0;
    for (uint32_t index = 0; index < count; index++) {
        value |= (uint32_t)stream->packet[stream->cursor++] << (index * 8);
    }
    return value;
}

static uint32_t field(AsfStream *stream, uint32_t type)
{
    static const uint8_t BYTES[] = {0, 1, 2, 4};
    return take(stream, BYTES[type & 3u]);
}

static bool packet(AsfStream *stream)
{
    if (stream->packet_index >= stream->track.packets) {
        return false;
    }
    uint32_t offset = stream->track.data_offset + stream->packet_index * stream->track.packet_bytes;
    if (!stream->input.read(stream->input.context, offset, stream->packet,
                            stream->track.packet_bytes)) {
        stream->failed = true;
        return false;
    }
    stream->packet_index++;
    stream->cursor = 0;
    stream->packet_end = stream->track.packet_bytes;
    if ((stream->packet[0] & 0x80) != 0) {
        if (stream->packet[0] != 0x82) {
            stream->failed = true;
            return false;
        }
        stream->cursor = 3;
    }
    uint32_t flags = take(stream, 1);
    stream->properties = take(stream, 1);
    uint32_t length = field(stream, flags >> 5);
    (void)field(stream, flags >> 1);
    uint32_t padding = field(stream, flags >> 3);
    (void)take(stream, 4);
    (void)take(stream, 2);
    length = length == 0 ? stream->track.packet_bytes : length;
    if (length > stream->track.packet_bytes || padding > length ||
        stream->cursor > length - padding || stream->properties >> 6 != 1) {
        stream->failed = true;
        return false;
    }
    stream->packet_end = length - padding;
    stream->multiple = (flags & 1) != 0;
    uint32_t payload = stream->multiple ? take(stream, 1) : 1;
    stream->payloads = payload & 0x3F;
    stream->payload_length_type = payload >> 6;
    if (stream->payloads == 0 || (stream->multiple && stream->payload_length_type == 0)) {
        stream->failed = true;
    }
    return !stream->failed;
}

static bool compressed(AsfStream *stream)
{
    uint32_t cursor = stream->compressed_cursor;
    uint32_t bytes = stream->packet[cursor++];
    if (bytes == 0 || bytes > stream->compressed_end - cursor) {
        stream->failed = true;
        return false;
    }
    for (uint32_t index = 0; index < bytes; index++) {
        stream->object[index] = stream->packet[cursor + index];
    }
    stream->object_bytes = bytes;
    stream->object_expected = bytes;
    stream->has_timestamp = true;
    stream->object_timestamp = stream->compressed_timestamp;
    stream->compressed_timestamp += stream->compressed_delta;
    stream->compressed_cursor = cursor + bytes;
    return true;
}

static bool payload(AsfStream *stream)
{
    uint32_t id = take(stream, 1) & 0x7F;
    uint32_t number = field(stream, stream->properties >> 4);
    uint32_t offset = field(stream, stream->properties >> 2);
    uint32_t replicated = field(stream, stream->properties);
    uint32_t size = 0;
    uint32_t delta = 0;
    stream->has_timestamp = replicated >= 8;
    if (replicated > stream->packet_end - stream->cursor) {
        stream->failed = true;
        return false;
    }
    if (replicated >= 8) {
        size = take(stream, 4);
        stream->object_timestamp = take(stream, 4);
        stream->cursor += replicated - 8;
    } else if (replicated == 1) {
        delta = take(stream, 1);
    } else {
        stream->cursor += replicated;
    }
    uint32_t bytes = stream->multiple ? field(stream, stream->payload_length_type)
                                      : stream->packet_end - stream->cursor;
    if (stream->failed || bytes > stream->packet_end - stream->cursor) {
        stream->failed = true;
        return false;
    }
    uint32_t start = stream->cursor;
    stream->cursor += bytes;
    stream->payloads--;
    if (id != stream->track.stream) {
        return true;
    }
    if (replicated == 1) {
        if (stream->object_bytes != 0 || bytes == 0) {
            stream->failed = true;
            return false;
        }
        stream->compressed_timestamp = offset;
        stream->compressed_delta = delta;
        stream->compressed_cursor = start;
        stream->compressed_end = start + bytes;
        return compressed(stream);
    }
    if (replicated != 0 && replicated < 8) {
        stream->failed = true;
        return false;
    }
    if (stream->seeking && offset != 0) {
        return true;
    }
    if (stream->object_bytes == 0) {
        stream->object_number = number;
        stream->object_expected = size == 0 ? bytes : size;
    }
    if (number != stream->object_number || offset != stream->object_bytes ||
        stream->object_expected > ASF_PACKET_BYTES ||
        bytes > stream->object_expected - stream->object_bytes) {
        stream->failed = true;
        return false;
    }
    for (uint32_t index = 0; index < bytes; index++) {
        stream->object[stream->object_bytes + index] = stream->packet[start + index];
    }
    stream->object_bytes += bytes;
    stream->seeking = false;
    return true;
}

static bool next_object(AsfStream *stream)
{
    stream->object_bytes = 0;
    stream->object_expected = 0;
    stream->object_position = 0;
    for (;;) {
        if (stream->compressed_cursor < stream->compressed_end) {
            if (!compressed(stream)) {
                return false;
            }
        } else {
            if (stream->payloads == 0 && !packet(stream)) {
                stream->failed = stream->failed || stream->object_bytes != 0;
                return false;
            }
            if (!payload(stream)) {
                return false;
            }
        }
        if (stream->object_bytes != 0 && stream->object_bytes == stream->object_expected) {
            if (stream->object_bytes % stream->track.block_align != 0) {
                stream->failed = true;
                return false;
            }
            return true;
        }
    }
}

static uint32_t packet_time(FlacInput input, const AsfTrack *track, uint32_t index)
{
    uint8_t header[32];
    uint32_t at = track->data_offset + index * track->packet_bytes;
    if (track->packet_bytes < sizeof header ||
        !input.read(input.context, at, header, sizeof header)) {
        return UINT32_MAX;
    }
    uint32_t cursor = (header[0] & 0x80) != 0 ? 3u : 0u;
    if (cursor != 0 && header[0] != 0x82) {
        return UINT32_MAX;
    }
    uint32_t flags = header[cursor];
    cursor += 2;
    static const uint8_t BYTES[] = {0, 1, 2, 4};
    cursor += (uint32_t)BYTES[(flags >> 5) & 3] + BYTES[(flags >> 1) & 3] + BYTES[(flags >> 3) & 3];
    uint32_t value = 0;
    for (uint32_t byte = 0; byte < 4; byte++) {
        value |= (uint32_t)header[cursor + byte] << (byte * 8);
    }
    return value;
}

static bool seek_packet(AsfStream *stream, uint32_t start_ms)
{
    if (start_ms <= 512) {
        return true;
    }
    uint32_t target = start_ms - 512;
    uint32_t low = 0;
    uint32_t high = stream->track.packets;
    while (low + 1 < high) {
        uint32_t middle = low + (high - low) / 2;
        uint32_t time = packet_time(stream->input, &stream->track, middle);
        if (time == UINT32_MAX) {
            return false;
        }
        if (time < target) {
            low = middle;
        } else {
            high = middle;
        }
    }
    stream->packet_index = low > 0 ? low - 1 : 0;
    stream->seeking = stream->packet_index != 0;
    return true;
}

bool asf_stream_start_at(AsfStream *stream, FlacInput input, const AsfTrack *track,
                         uint32_t start_ms)
{
    if (stream == NULL || track == NULL || track->block_align == 0 ||
        track->block_align > WMA_PACKET_BYTES || track->packet_bytes > ASF_PACKET_BYTES) {
        return false;
    }
    *stream = (AsfStream){.input = input, .track = *track, .record_bytes = WMA_CONFIG_BYTES};
    if (!seek_packet(stream, start_ms)) {
        return false;
    }
    bool skipped = stream->packet_index != 0;
    if (!next_object(stream)) {
        return false;
    }
    if (skipped && !stream->has_timestamp) {
        *stream = (AsfStream){.input = input, .track = *track, .record_bytes = WMA_CONFIG_BYTES};
        if (!next_object(stream)) {
            return false;
        }
    }
    uint32_t time = stream->object_timestamp > track->preroll_ms
                        ? stream->object_timestamp - track->preroll_ms
                        : 0;
    uint64_t frames = ((uint64_t)time * track->rate + track->frame_samples * 500u) /
                      (track->frame_samples * 1000u);
    if (frames * track->frame_samples > UINT32_MAX) {
        return false;
    }
    stream->first_sample = (uint32_t)(frames * track->frame_samples);
    put(stream->header, track->codec);
    put(stream->header + 4, track->channels);
    put(stream->header + 8, track->rate);
    put(stream->header + 12, track->bitrate);
    put(stream->header + 16, track->block_align);
    put(stream->header + 20, track->extra_bytes);
    for (uint32_t index = 0; index < track->extra_bytes; index++) {
        stream->header[24 + index] = track->extra[index];
    }
    return true;
}

bool asf_stream_start(AsfStream *stream, FlacInput input, const AsfTrack *track)
{
    return asf_stream_start_at(stream, input, track, 0);
}

static bool next_record(AsfStream *stream)
{
    if (stream->configured) {
        stream->object_position += stream->track.block_align;
    }
    stream->configured = true;
    stream->record_position = 0;
    stream->record_bytes = 4;
    if (stream->object_position == stream->object_bytes && !next_object(stream)) {
        if (stream->failed) {
            return false;
        }
        put(stream->header, 0);
        stream->ended = true;
        return true;
    }
    put(stream->header, stream->track.block_align);
    stream->record_bytes += stream->track.block_align;
    return true;
}

bool asf_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    AsfStream *stream = context;
    if (stream == NULL || stream->failed || offset != stream->position ||
        offset > WMA_VIRTUAL_END || length > WMA_VIRTUAL_END - offset ||
        (buffer == NULL && length != 0)) {
        return false;
    }
    uint8_t *out = buffer;
    while (length != 0) {
        if (!stream->ended && stream->record_position == stream->record_bytes &&
            !next_record(stream)) {
            stream->failed = true;
            return false;
        }
        uint32_t header = stream->configured ? 4u : WMA_CONFIG_BYTES;
        uint32_t boundary = stream->record_position < header ? header : stream->record_bytes;
        uint32_t count =
            stream->record_position < boundary ? boundary - stream->record_position : 0;
        count = count < length ? count : length;
        if (count == 0 && stream->ended) {
            count = length;
            for (uint32_t index = 0; index < count; index++) {
                out[index] = 0;
            }
        } else {
            const uint8_t *from =
                stream->record_position < header
                    ? stream->header + stream->record_position
                    : stream->object + stream->object_position + stream->record_position - header;
            for (uint32_t index = 0; index < count; index++) {
                out[index] = from[index];
            }
        }
        if (stream->record_position < stream->record_bytes) {
            stream->record_position += count;
        }
        stream->position += count;
        out += count;
        length -= count;
    }
    return true;
}
