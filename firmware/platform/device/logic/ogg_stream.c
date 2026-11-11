#include "device/ogg_stream.h"

#include <stddef.h>

static void put32(uint8_t *bytes, uint32_t value)
{
    for (uint32_t index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

bool ogg_stream_start(OggStream *stream, FlacInput file)
{
    ogg_packets_init(&stream->packets, file);
    stream->position = 0;
    stream->record_done = 0;
    stream->record_size = 0;
    stream->headers = 0;
    stream->seek_offset = 0;
    stream->serial = 0;
    stream->origin = 0;
    stream->origin_sent = false;
    stream->seeked = false;
    stream->ended = false;
    stream->failed = file.read == NULL;
    return !stream->failed;
}

static bool valid_header(const OggStream *stream, const OggPacket *packet)
{
    static const uint8_t SIGNATURE[] = {'v', 'o', 'r', 'b', 'i', 's'};
    if (packet->bytes < 7 || packet->last || packet->first != (stream->headers == 0) ||
        stream->body[0] != 1u + 2u * stream->headers) {
        return false;
    }
    for (uint32_t index = 0; index < sizeof SIGNATURE; index++) {
        if (stream->body[index + 1] != SIGNATURE[index]) {
            return false;
        }
    }
    return stream->headers != 1 || packet->bytes >= 16;
}

static bool make_header(OggStream *stream, OggPacket *packet)
{
    if (!valid_header(stream, packet)) {
        return false;
    }
    if (stream->headers == 1) {
        static const uint8_t COMMENT[] = {3, 'v', 'o', 'r', 'b', 'i', 's', 0,
                                          0, 0,   0,   0,   0,   0,   0,   1};
        for (uint32_t index = 0; index < sizeof COMMENT; index++) {
            stream->body[index] = COMMENT[index];
        }
        packet->bytes = sizeof COMMENT;
    }
    stream->headers++;
    return true;
}

static bool begin_record(OggStream *stream)
{
    if (stream->headers == 3 && stream->origin != 0 && !stream->origin_sent) {
        put32(stream->head, 0);
        put32(stream->head + 4, OGG_RECORD_ORIGIN);
        put32(stream->head + 8, stream->origin);
        put32(stream->head + 12, 0);
        stream->origin_sent = true;
        stream->record_done = 0;
        stream->record_size = OGG_RECORD_BYTES;
        return true;
    }
    if (stream->headers == 3 && stream->seek_offset != 0 && !stream->seeked) {
        FlacInput file = stream->packets.input;
        if (ogg_packets_resume(&stream->packets, file, stream->seek_offset, stream->serial) !=
            OGG_PACKET_READY) {
            stream->failed = true;
            return false;
        }
        stream->seeked = true;
    }
    OggPacket packet = {0};
    OggPacketResult result =
        stream->headers == 1
            ? ogg_packets_prefix(&stream->packets, stream->body, 7, &packet)
            : ogg_packets_next(&stream->packets, stream->body, sizeof stream->body, &packet);
    uint32_t flags = 0;
    if (result == OGG_PACKET_END && stream->headers == 3) {
        stream->ended = true;
        flags = OGG_RECORD_END;
    } else {
        if (result != OGG_PACKET_READY || packet.bytes == 0 ||
            (stream->headers < 3 && !make_header(stream, &packet))) {
            stream->failed = true;
            return false;
        }
        flags = (uint32_t)(packet.first ? OGG_RECORD_FIRST : 0) |
                (uint32_t)(packet.last ? OGG_RECORD_LAST : 0);
    }
    put32(stream->head, packet.bytes);
    put32(stream->head + 4, flags);
    put32(stream->head + 8, (uint32_t)packet.granule);
    put32(stream->head + 12, (uint32_t)(packet.granule >> 32));
    stream->record_done = 0;
    stream->record_size = OGG_RECORD_BYTES + packet.bytes;
    return true;
}

static uint32_t emit(OggStream *stream, uint8_t *out, uint32_t length)
{
    uint32_t end = stream->record_size;
    const uint8_t *source = stream->body;
    uint32_t offset = stream->record_done - OGG_RECORD_BYTES;
    if (stream->record_done < OGG_RECORD_BYTES) {
        end = OGG_RECORD_BYTES;
        source = stream->head;
        offset = stream->record_done;
    }
    uint32_t left = end - stream->record_done;
    uint32_t count = length < left ? length : left;
    for (uint32_t index = 0; index < count; index++) {
        out[index] = source[offset + index];
    }
    stream->record_done += count;
    return count;
}

bool ogg_stream_read(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    OggStream *stream = context;
    uint8_t *out = buffer;
    if (stream->failed || offset != stream->position || offset > OGG_VIRTUAL_END ||
        length > OGG_VIRTUAL_END - offset || (length != 0 && out == NULL)) {
        return false;
    }
    while (length > 0) {
        uint32_t count = length;
        if (stream->ended && stream->record_done == stream->record_size) {
            for (uint32_t index = 0; index < count; index++) {
                out[index] = 0;
            }
        } else {
            if (stream->record_done == stream->record_size && !begin_record(stream)) {
                return false;
            }
            count = emit(stream, out, length);
        }
        stream->position += count;
        out += count;
        length -= count;
    }
    return true;
}

bool ogg_stream_seek(OggStream *stream, FlacInput file, const OggTrack *track, uint32_t sample)
{
    uint32_t offset;
    if (!ogg_track_seek(file, track, sample, &offset) || !ogg_stream_start(stream, file)) {
        return false;
    }
    stream->seek_offset = offset;
    stream->serial = track->serial;
    stream->origin = track->origin;
    return true;
}

bool ogg_stream_prepare_vorbis(OggStream *stream, OggTrack *track, const VorbisModes *modes,
                               uint32_t sample)
{
    if (stream->failed || stream->ended || stream->headers != 3 ||
        stream->position != track->header_bytes || stream->record_done != stream->record_size) {
        return false;
    }
    OggTrack candidate = *track;
    uint32_t offset;
    if (!vorbis_track_origin(&stream->packets, modes, &candidate) ||
        !ogg_track_seek(stream->packets.input, &candidate, sample, &offset)) {
        return false;
    }
    stream->seek_offset = offset;
    stream->serial = candidate.serial;
    stream->origin = candidate.origin;
    *track = candidate;
    return true;
}
