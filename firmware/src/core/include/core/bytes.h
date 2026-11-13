#pragma once

#include <stdint.h>

static inline uint32_t bytes_le16(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
}

static inline uint32_t bytes_le32(const uint8_t *bytes)
{
    return bytes_le16(bytes) | bytes_le16(bytes + 2) << 16;
}

static inline void bytes_put_le16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static inline void bytes_put_le32(uint8_t *bytes, uint32_t value)
{
    bytes_put_le16(bytes, value);
    bytes_put_le16(bytes + 2, value >> 16);
}
