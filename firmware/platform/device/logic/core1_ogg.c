#include "device/core1_ogg.h"

#include "core/bytes.h"

Core1DecodeResult core1_ogg_fetch(FlacInput input, uint32_t position, uint8_t *packet,
                                  OggRecord *record)
{
    static const uint32_t KNOWN =
        OGG_RECORD_FIRST | OGG_RECORD_LAST | OGG_RECORD_END | OGG_RECORD_ORIGIN;
    uint8_t header[OGG_RECORD_BYTES];
    if (position > input.size || sizeof header > input.size - position) {
        return CORE1_FAILED;
    }
    if (!input.read(input.context, position, header, sizeof header)) {
        return CORE1_STARVED;
    }
    uint32_t bytes = bytes_le32(header);
    uint32_t flags = bytes_le32(header + 4);
    if ((flags & ~KNOWN) != 0 || bytes > OGG_PACKET_BYTES ||
        bytes > input.size - position - sizeof header) {
        return CORE1_FAILED;
    }
    if (bytes != 0 && !input.read(input.context, position + sizeof header, packet, bytes)) {
        return CORE1_STARVED;
    }
    *record = (OggRecord){
        .bytes = bytes,
        .flags = flags,
        .granule = (uint64_t)bytes_le32(header + 8) | (uint64_t)bytes_le32(header + 12) << 32,
    };
    return CORE1_DECODED;
}
