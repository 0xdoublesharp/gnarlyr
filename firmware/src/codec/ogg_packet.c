#include "codec/ogg_packet.h"

#include <stddef.h>

enum {
    OGG_HEADER_BYTES = 27,
    OGG_CRC_OFFSET = 22,
    OGG_CHECK_CHUNK = 256,
    OGG_CONTINUED = 1,
    OGG_FIRST = 2,
    OGG_LAST = 4
};

static uint32_t little32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *bytes, uint32_t length)
{
    static const uint32_t TABLE[16] = {
        0x00000000u, 0x04C11DB7u, 0x09823B6Eu, 0x0D4326D9u, 0x130476DCu, 0x17C56B6Bu,
        0x1A864DB2u, 0x1E475005u, 0x2608EDB8u, 0x22C9F00Fu, 0x2F8AD6D6u, 0x2B4BCB61u,
        0x350C9B64u, 0x31CD86D3u, 0x3C8EA00Au, 0x384FBDBDu,
    };
    for (uint32_t index = 0; index < length; index++) {
        crc ^= (uint32_t)bytes[index] << 24;
        crc = (crc << 4) ^ TABLE[crc >> 28];
        crc = (crc << 4) ^ TABLE[crc >> 28];
    }
    return crc;
}

static OggPacketResult read_bytes(FlacInput input, uint32_t offset, void *bytes, uint32_t length)
{
    if (input.read == NULL || offset > input.size || length > input.size - offset) {
        return OGG_PACKET_INVALID;
    }
    if (length == 0) {
        return OGG_PACKET_READY;
    }
    return input.read(input.context, offset, bytes, length) ? OGG_PACKET_READY : OGG_PACKET_WAIT;
}

static OggPacketResult check_page(FlacInput input, uint32_t offset, uint32_t length,
                                  uint32_t expected)
{
    uint8_t buffer[OGG_CHECK_CHUNK];
    uint32_t crc = 0;
    for (uint32_t done = 0; done < length;) {
        uint32_t chunk = length - done < sizeof buffer ? length - done : (uint32_t)sizeof buffer;
        OggPacketResult result = read_bytes(input, offset + done, buffer, chunk);
        if (result != OGG_PACKET_READY) {
            return result;
        }
        if (done == 0) {
            for (uint32_t index = OGG_CRC_OFFSET; index < OGG_CRC_OFFSET + 4; index++) {
                buffer[index] = 0;
            }
        }
        crc = crc_update(crc, buffer, chunk);
        done += chunk;
    }
    return crc == expected ? OGG_PACKET_READY : OGG_PACKET_INVALID;
}

static bool valid_header(const OggPackets *reader, const uint8_t *header, bool continued)
{
    bool capture = header[0] == 'O' && header[1] == 'g' && header[2] == 'g' && header[3] == 'S';
    bool flags = header[4] == 0 && (header[5] & ~(OGG_FIRST | OGG_LAST | OGG_CONTINUED)) == 0 &&
                 ((header[5] & OGG_CONTINUED) != 0) == continued;
    if (!capture || !flags) {
        return false;
    }
    if (!reader->started) {
        return (header[5] & OGG_FIRST) != 0 && little32(header + 18) == 0;
    }
    return (header[5] & OGG_FIRST) == 0 && little32(header + 14) == reader->serial &&
           little32(header + 18) == reader->sequence + 1;
}

static OggPacketResult load_page(OggPackets *reader, bool continued)
{
    uint8_t header[OGG_HEADER_BYTES];
    OggPacketResult result = read_bytes(reader->input, reader->next_page, header, sizeof header);
    if (result != OGG_PACKET_READY || !valid_header(reader, header, continued)) {
        return result != OGG_PACKET_READY ? result : OGG_PACKET_INVALID;
    }
    uint32_t offset = reader->next_page + sizeof header;
    result = read_bytes(reader->input, offset, reader->laces, header[26]);
    if (result != OGG_PACKET_READY) {
        return result;
    }
    uint32_t size = 0;
    reader->last_complete = 0;
    for (uint32_t index = 0; index < header[26]; index++) {
        size += reader->laces[index];
        if (reader->laces[index] < 255) {
            reader->last_complete = (uint16_t)(index + 1);
        }
    }
    offset += header[26];
    if (offset > reader->input.size || size > reader->input.size - offset ||
        ((header[5] & OGG_LAST) != 0 && header[26] > 0 && reader->laces[header[26] - 1] == 255)) {
        return OGG_PACKET_INVALID;
    }
    uint32_t length = offset + size - reader->next_page;
    result =
        check_page(reader->input, reader->next_page, length, little32(header + OGG_CRC_OFFSET));
    if (result != OGG_PACKET_READY) {
        return result;
    }
    reader->body = offset;
    reader->next_page = offset + size;
    reader->serial = little32(header + 14);
    reader->sequence = little32(header + 18);
    reader->granule = (uint64_t)little32(header + 6) | (uint64_t)little32(header + 10) << 32;
    reader->flags = header[5];
    reader->segments = header[26];
    reader->segment = 0;
    reader->started = true;
    return OGG_PACKET_READY;
}

static OggPacketResult packet_page(OggPackets *reader, bool continued)
{
    while (reader->segment == reader->segments) {
        if (reader->ended || (reader->started && (reader->flags & OGG_LAST) != 0)) {
            reader->ended = true;
            return continued ? OGG_PACKET_INVALID : OGG_PACKET_END;
        }
        OggPacketResult result = load_page(reader, continued);
        if (result != OGG_PACKET_READY) {
            return result;
        }
    }
    return OGG_PACKET_READY;
}

typedef struct PacketOutput {
    uint8_t *buffer;
    uint32_t capacity;
    bool prefix;
} PacketOutput;

static OggPacketResult copy_segment(OggPackets *reader, PacketOutput output, uint32_t bytes,
                                    uint32_t chunk)
{
    uint32_t left = bytes < output.capacity ? output.capacity - bytes : 0;
    if (!output.prefix && chunk > left) {
        return OGG_PACKET_TOO_LARGE;
    }
    uint32_t copy = chunk < left ? chunk : left;
    return copy == 0 ? OGG_PACKET_READY
                     : read_bytes(reader->input, reader->body, output.buffer + bytes, copy);
}

static OggPacketResult assemble_packet(OggPackets *reader, PacketOutput output, OggPacket *packet)
{
    uint32_t bytes = 0;
    bool continued = false;
    for (;;) {
        OggPacketResult result = packet_page(reader, continued);
        if (result != OGG_PACKET_READY) {
            return result;
        }
        uint32_t chunk = reader->laces[reader->segment];
        if (chunk > UINT32_MAX - bytes) {
            return OGG_PACKET_INVALID;
        }
        result = copy_segment(reader, output, bytes, chunk);
        if (result != OGG_PACKET_READY) {
            return result;
        }
        reader->body += chunk;
        reader->segment++;
        bytes += chunk;
        if (chunk < 255) {
            *packet = (OggPacket){
                .bytes = bytes,
                .granule = reader->segment == reader->last_complete ? reader->granule : UINT64_MAX,
                .first = !reader->emitted,
                .last = reader->segment == reader->segments && (reader->flags & OGG_LAST) != 0,
            };
            reader->emitted = true;
            return OGG_PACKET_READY;
        }
        continued = true;
    }
}

void ogg_packets_init(OggPackets *reader, FlacInput input)
{
    *reader = (OggPackets){.input = input};
}

static OggPacketResult read_packet(OggPackets *reader, PacketOutput output, OggPacket *packet)
{
    if ((output.buffer == NULL && output.capacity != 0) || packet == NULL) {
        return OGG_PACKET_INVALID;
    }
    OggPackets candidate = *reader;
    OggPacket result;
    OggPacketResult outcome = assemble_packet(&candidate, output, &result);
    if (outcome == OGG_PACKET_READY || outcome == OGG_PACKET_END) {
        *reader = candidate;
    }
    if (outcome == OGG_PACKET_READY) {
        *packet = result;
    }
    return outcome;
}

OggPacketResult ogg_packets_next(OggPackets *reader, uint8_t *buffer, uint32_t capacity,
                                 OggPacket *packet)
{
    return read_packet(reader, (PacketOutput){buffer, capacity, false}, packet);
}

OggPacketResult ogg_packets_prefix(OggPackets *reader, uint8_t *buffer, uint32_t capacity,
                                   OggPacket *packet)
{
    return read_packet(reader, (PacketOutput){buffer, capacity, true}, packet);
}
