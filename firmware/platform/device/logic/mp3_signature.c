#include "device/mp3_probe.h"

bool mp3_header_valid(const uint8_t *bytes)
{
    uint32_t version = (uint32_t)(bytes[1] >> 3) & 3u;
    uint32_t layer_bits = (uint32_t)(bytes[1] >> 1) & 3u;
    uint32_t bitrate_index = (uint32_t)bytes[2] >> 4;
    uint32_t rate_index = (uint32_t)(bytes[2] >> 2) & 3u;
    return bytes[0] == 0xFF && (bytes[1] & 0xE0) == 0xE0 && version != 1 && layer_bits != 0 &&
           bitrate_index != 0 && bitrate_index != 15 && rate_index != 3;
}

bool mp3_signature(const uint8_t bytes[MP3_SIGNATURE_BYTES])
{
    return (bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3') || mp3_header_valid(bytes);
}
